// src/playground/worker.cpp
// trust-playground: реализация исполнителя (см. include/playground/worker.h).
//
// Reverse long-poll: каждый слот держит исходящее HTTP-соединение к балансировщику
// (POST /poll блокирует до появления задачи или таймаута), выполняет транспиляцию
// через trust-lsp --json в субпроцессе с лимитами (setrlimit + wall-clock таймаут)
// и отправляет результат (POST /result). Поддержка http (сырые сокеты) и https
// (через curl, системный TLS-стек).

#include "playground/worker.h"

#include "playground/http.h"
#include "trust/version.h"
#include "utils/io.hpp"

#include <nlohmann/json.hpp>

#include <arpa/inet.h>
#include <cerrno>
#include <cctype>
#include <csignal>
#include <cstdlib>
#include <cstring>
#include <fcntl.h>
#include <netinet/in.h>
#include <poll.h>
#include <sys/resource.h>
#include <sys/socket.h>
#include <sys/stat.h>
#include <sys/statvfs.h>
#include <sys/sysinfo.h>
#include <sys/types.h>
#include <sys/wait.h>
#include <unistd.h>

#include <chrono>
#include <filesystem>
#include <fstream>
#include <sstream>
#include <string>
#include <thread>
#include <vector>

namespace trust {
namespace playground {

// Возвращает минимальный JSON-контракт с ошибкой (на случай сбоя транспиляции).
std::string makeErrorJson(const std::string& code, const std::string& reason) {
    nlohmann::json j{
        {"source", code}, {"cpp", ""}, {"ok", false}, {"error", reason}, {"trustToCpp", nlohmann::json::array()}, {"cppToTrust", nlohmann::json::array()}};
    return j.dump();
}

// Читает fd до EOF или таймаута.
void readFdUntilEof(int fd, std::string& out, int timeout_sec) {
    char tmp[8192];
    struct pollfd p{fd, POLLIN, 0};
    const auto deadline = std::chrono::steady_clock::now() + std::chrono::seconds(timeout_sec);
    while (true) {
        const auto remain = std::chrono::duration_cast<std::chrono::milliseconds>(deadline - std::chrono::steady_clock::now());
        if (remain.count() <= 0) {
            return;
        }
        const int pr = ::poll(&p, 1, static_cast<int>(remain.count()));
        if (pr < 0) {
            if (errno == EINTR) {
                continue;
            }
            return;
        }
        if (pr == 0) {
            return;
        }
        const ssize_t n = ::read(fd, tmp, sizeof(tmp));
        if (n <= 0) {
            return;
        }
        out.append(tmp, static_cast<size_t>(n));
    }
}

// Ждёт завершения дочернего процесса с таймаутом. Возвращает true, если завершился.
bool waitPidTimeout(pid_t pid, int timeout_sec) {
    const auto deadline = std::chrono::steady_clock::now() + std::chrono::seconds(timeout_sec);
    while (true) {
        int status = 0;
        const pid_t r = ::waitpid(pid, &status, WNOHANG);
        if (r == pid) {
            return true;
        }
        const auto remain = std::chrono::duration_cast<std::chrono::milliseconds>(deadline - std::chrono::steady_clock::now());
        if (remain.count() <= 0) {
            return false;
        }
        std::this_thread::sleep_for(std::chrono::milliseconds(50));
    }
}

// Читает бинарный файл целиком.
std::string readFileBytes(const std::string& path) {
    std::ifstream f(path, std::ios::binary);
    if (!f) {
        return std::string();
    }
    std::ostringstream ss;
    ss << f.rdbuf();
    return ss.str();
}

// Возвращает base64 build-архива, собранного trust-lsp --emit-build-dir. Архив ищем по
// маске *.tar.gz в emit_dir (имя формирует trust-lsp: trust-lang-<версия>-generated.tar.gz),
// чтобы не зависеть от совпадения версий бинарников воркера и trust-lsp (TRUST_VERSION_FULL
// содержит git-хэш). В out_name кладём фактическое имя файла.
std::string readArchiveBase64(const std::string& emit_dir, std::string& out_name) {
    out_name.clear();
    if (emit_dir.empty()) {
        return std::string();
    }
    std::error_code ec;
    std::string path;
    for (auto it = std::filesystem::directory_iterator(emit_dir, ec); it != std::filesystem::directory_iterator() && !ec; it.increment(ec)) {
        if (it->is_regular_file(ec) && !ec) {
            // path::extension() даёт только ".gz"; проверяем полный суффикс ".tar.gz".
            const std::string fn = it->path().filename().string();
            if (fn.size() > 7 && fn.ends_with(".tar.gz")) {
                path = it->path().string();
                out_name = fn;
                break;
            }
        }
    }
    if (path.empty()) {
        return std::string();
    }
    const std::string bytes = readFileBytes(path);
    if (bytes.empty()) {
        return std::string();
    }
    return trust::playground::base64Encode(bytes);
}

// Запускает trust-lsp --json в субпроцессе с лимитами; возвращает stdout (JSON-контракт).
// build_archive=true — дополнительно передаём --emit-build-dir: trust-lsp сам собирает
// build-архив (build-каталог pipeline, без компиляции), воркер читает tar.gz и встраивает
// в результат как base64 (используется ленивым POST /download).
std::string runTrustLsp(const std::string& code, const PlaygroundConfig& cfg, int64_t job_id, bool build_archive) {
    // build_archive: trust-lsp собирает build-архив во временный каталог (чистим по выходе).
    std::string emit_dir;
    if (build_archive) {
        char tmpl[] = "/tmp/trust-build-XXXXXX";
        char* dir = ::mkdtemp(tmpl);
        if (dir != nullptr) {
            emit_dir = dir;
        }
    }
    // RAII: удалить временный каталог при выходе (в т.ч. по ошибке/таймауту).
    struct TmpDirGuard {
        std::string path;
        ~TmpDirGuard() {
            if (!path.empty()) {
                std::error_code ec;
                std::filesystem::remove_all(path, ec);
            }
        }
    } tmp_guard{emit_dir};

    int stdin_pipe[2];
    int stdout_pipe[2];
    int stderr_pipe[2];
    if (::pipe(stdin_pipe) != 0 || ::pipe(stdout_pipe) != 0 || ::pipe(stderr_pipe) != 0) {
        return makeErrorJson(code, "worker: cannot create pipe");
    }
    const pid_t pid = ::fork();
    if (pid < 0) {
        ::close(stdin_pipe[0]);
        ::close(stdin_pipe[1]);
        ::close(stdout_pipe[0]);
        ::close(stdout_pipe[1]);
        ::close(stderr_pipe[0]);
        ::close(stderr_pipe[1]);
        return makeErrorJson(code, "worker: fork failed");
    }

    if (pid == 0) {
        ::close(stdin_pipe[1]);
        ::close(stdout_pipe[0]);
        ::close(stderr_pipe[0]);
        ::dup2(stdin_pipe[0], STDIN_FILENO);
        ::dup2(stdout_pipe[1], STDOUT_FILENO);
        ::dup2(stderr_pipe[1], STDERR_FILENO);
        const rlim_t as_bytes = static_cast<rlim_t>(cfg.maxMemoryMb) * 1024 * 1024;
        struct rlimit rl_as{as_bytes, as_bytes};
        ::setrlimit(RLIMIT_AS, &rl_as);
        const rlim_t cpu_sec = static_cast<rlim_t>(cfg.workerJobTimeoutSec);
        struct rlimit rl_cpu{cpu_sec, cpu_sec};
        ::setrlimit(RLIMIT_CPU, &rl_cpu);
        const rlim_t fs_bytes = static_cast<rlim_t>(cfg.maxOutputKb) * 1024;
        struct rlimit rl_fs{fs_bytes, fs_bytes};
        ::setrlimit(RLIMIT_FSIZE, &rl_fs);

        std::vector<std::string> args;
        args.push_back(cfg.lspBin);
        args.push_back("--json");
        if (!cfg.projectDir.empty()) {
            args.push_back("--project-dir");
            args.push_back(cfg.projectDir);
        }
        if (build_archive && !emit_dir.empty()) {
            args.push_back("--emit-build-dir");
            args.push_back(emit_dir);
        }
        // Дополнительные опции, которые всегда передаются в trust-lsp.
        for (const std::string& o : cfg.lspOpts) {
            args.push_back(o);
        }
        std::vector<char*> argv;
        for (std::string& a : args) {
            argv.push_back(const_cast<char*>(a.c_str()));
        }
        argv.push_back(nullptr);
        ::execv(cfg.lspBin.c_str(), argv.data());
        ::_exit(127);
    }

    ::close(stdin_pipe[0]);
    ::close(stdout_pipe[1]);
    ::close(stderr_pipe[1]);
    ::write(stdin_pipe[1], code.data(), code.size());
    ::close(stdin_pipe[1]);

    std::string out;
    readFdUntilEof(stdout_pipe[0], out, cfg.workerJobTimeoutSec);
    ::close(stdout_pipe[0]);

    std::string err;
    readFdUntilEof(stderr_pipe[0], err, cfg.workerJobTimeoutSec);
    ::close(stderr_pipe[0]);

    if (!waitPidTimeout(pid, cfg.workerJobTimeoutSec)) {
        ::kill(pid, SIGKILL);
        int status = 0;
        ::waitpid(pid, &status, 0);
        nlohmann::json j{{"source", code},
                         {"cpp", ""},
                         {"ok", false},
                         {"error", "worker transpile timed out"},
                         {"trustToCpp", nlohmann::json::array()},
                         {"cppToTrust", nlohmann::json::array()}};
        if (!err.empty()) {
            j["error"] = err;
            j["log"] = err;
        }
        return j.dump();
    }
    if (out.empty()) {
        nlohmann::json j{{"source", code},
                         {"cpp", ""},
                         {"ok", false},
                         {"error", err.empty() ? "worker transpile failed" : err},
                         {"trustToCpp", nlohmann::json::array()},
                         {"cppToTrust", nlohmann::json::array()}};
        if (!err.empty()) {
            j["log"] = err;
        }
        return j.dump();
    }

    // Обогащаем контракт логом (stderr trust-lsp) и, для задач «собрать архив» (/download),
    // build-архивом, собранным самим trust-lsp (build-каталог pipeline).
    try {
        nlohmann::json j = nlohmann::json::parse(out);
        if (!err.empty()) {
            j["log"] = err;
        }
        if (build_archive) {
            std::string arch_name;
            const std::string archive = readArchiveBase64(emit_dir, arch_name);
            if (!archive.empty() && !arch_name.empty()) {
                j["archive"] = archive;
                j["archiveName"] = arch_name; // имя файла, как его сформировал trust-lsp
            }
        }
        return j.dump();
    } catch (const std::exception&) {
        return out;
    }
}
PlaygroundWorker::PlaygroundWorker(const PlaygroundConfig& cfg)
: cfg_(cfg) {
}

void PlaygroundWorker::requestStop() {
    stop_.store(true);
}

// Печатает переход состояния подключения к балансировщику однократно (при смене).
void PlaygroundWorker::reportConnection(bool up) {
    const int desired = up ? 2 : 0;
    int cur = connState_.load();
    if (cur == desired) {
        return;
    }
    if (connState_.compare_exchange_strong(cur, desired)) {
        if (up) {
            trust::errs() << "trust-playground (worker): connected to balancer " << cfg_.playgroundUrl << "\n";
        } else {
            trust::errs() << "trust-playground (worker): connection to balancer lost, retrying...\n";
        }
    }
}

void PlaygroundWorker::slotLoop(int slotIndex) {
    const int backoff_ms = 1000;
    while (!stop_.load()) {
        nlohmann::json poll_body{{"token", cfg_.token}, {"capacity", cfg_.maxParallel}, {"load", 0}, {"metrics", collectMetrics()}};
        const HttpResult r = httpPost(cfg_.playgroundUrl + "/poll", poll_body.dump(), cfg_.pollTimeoutSec + 5);
        if (r.status == 0) {
            connected_.store(false);
            reportConnection(false);
            trust::errs() << "trust-playground (worker): slot " << slotIndex << ": balancer unreachable, retrying\n";
            std::this_thread::sleep_for(std::chrono::milliseconds(backoff_ms));
            continue;
        }
        connected_.store(true);
        reportConnection(true);
        if (r.status == 403) {
            trust::errs() << "trust-playground (worker): slot " << slotIndex << ": unauthorized (bad token)\n";
            std::this_thread::sleep_for(std::chrono::seconds(5));
            continue;
        }
        if (r.status == 204) {
            continue; // задач нет — сразу переполлить
        }
        if (r.status == 200) {
            busySlots_.fetch_add(1);
            try {
                const auto job = nlohmann::json::parse(r.body);
                const int64_t job_id = job.value("jobId", int64_t(0));
                const std::string code = job.value("code", std::string());
                const bool build_archive = job.value("buildArchive", false);
                const std::string result = runTrustLsp(code, cfg_, job_id, build_archive);
                try {
                    const auto jr = nlohmann::json::parse(result);
                    if (jr.value("ok", false)) {
                        jobsDone_.fetch_add(1);
                    } else {
                        jobsFailed_.fetch_add(1);
                    }
                } catch (const std::exception&) {
                    jobsFailed_.fetch_add(1);
                }
                nlohmann::json rb{{"token", cfg_.token}, {"jobId", job_id}, {"result", result}};
                httpPost(cfg_.playgroundUrl + "/result", rb.dump(), 10);
            } catch (const std::exception&) {
                jobsFailed_.fetch_add(1);
                trust::errs() << "trust-playground: slot " << slotIndex << ": bad job payload\n";
            }
            busySlots_.fetch_sub(1);
            continue;
        }
        trust::errs() << "trust-playground: slot " << slotIndex << ": unexpected status " << r.status << "\n";
        std::this_thread::sleep_for(std::chrono::milliseconds(backoff_ms));
    }
}

nlohmann::json PlaygroundWorker::collectMetrics() {
    nlohmann::json m;
    const auto uptime = std::chrono::duration_cast<std::chrono::seconds>(std::chrono::steady_clock::now() - startTime_).count();
    m["uptime_sec"] = uptime;
    m["cpu_cores"] = static_cast<int>(::sysconf(_SC_NPROCESSORS_ONLN));
    m["slots_busy"] = busySlots_.load();
    m["slots_total"] = cfg_.maxParallel;
    m["jobs_done"] = jobsDone_.load();
    m["jobs_failed"] = jobsFailed_.load();
    m["connected"] = connected_.load();

    // Память: /proc/meminfo.
    {
        std::ifstream f("/proc/meminfo");
        std::string line;
        long long total_kb = 0, avail_kb = 0;
        while (std::getline(f, line)) {
            if (line.rfind("MemTotal:", 0) == 0) {
                total_kb = std::stoll(line.substr(9));
            } else if (line.rfind("MemAvailable:", 0) == 0) {
                avail_kb = std::stoll(line.substr(13));
            }
        }
        m["mem_total"] = total_kb * 1024;
        m["mem_free"] = avail_kb * 1024;
        const long long cur_mem = (total_kb - avail_kb) * 1024;
        m["mem_used"] = cur_mem;
        // Пик используемой памяти (с момента старта).
        long long prev_mem = memUsedMax_.load();
        while (cur_mem > prev_mem && !memUsedMax_.compare_exchange_weak(prev_mem, cur_mem)) {
        }
        m["mem_used_max"] = memUsedMax_.load();
    }

    // Диск: statvfs.
    {
        struct statvfs vfs{};
        const std::string root = cfg_.projectDir.empty() ? "/" : cfg_.projectDir;
        if (::statvfs(root.c_str(), &vfs) == 0) {
            const long long bytes = static_cast<long long>(vfs.f_bsize);
            m["disk_total"] = static_cast<long long>(vfs.f_blocks) * bytes;
            m["disk_free"] = static_cast<long long>(vfs.f_bavail) * bytes;
        }
    }

    // Нагрузка: /proc/loadavg.
    {
        std::ifstream f("/proc/loadavg");
        double l1 = 0, l5 = 0, l15 = 0;
        f >> l1 >> l5 >> l15;
        m["load1"] = l1;
        m["load5"] = l5;
        m["load15"] = l15;
        // Пик нагрузки CPU (load1) с момента старта.
        double prev_load = loadMax_.load();
        while (l1 > prev_load && !loadMax_.compare_exchange_weak(prev_load, l1)) {
        }
        m["load_max"] = loadMax_.load();
    }

    // Сеть: /proc/net/dev (суммарно по интерфейсам).
    {
        std::ifstream f("/proc/net/dev");
        std::string line;
        long long rx = 0, tx = 0;
        while (std::getline(f, line)) {
            const size_t colon = line.find(':');
            if (colon == std::string::npos) {
                continue;
            }
            std::istringstream ss(line.substr(colon + 1));
            long long r = 0, t = 0;
            if ((ss >> r >> std::ws) && (ss >> std::ws)) {
                // После rx-байт идёт пакеты/errs/drop/fifo/frame/compressed/multicast, затем tx.
                long long dummy = 0;
                for (int i = 0; i < 6; ++i) {
                    ss >> dummy;
                }
                ss >> t;
            }
            rx += r;
            tx += t;
        }
        m["net_rx_bytes"] = rx;
        m["net_tx_bytes"] = tx;
    }
    return m;
}

void PlaygroundWorker::statsLoop() {
    const auto start = std::chrono::steady_clock::now();
    const int interval_ms = cfg_.statsIntervalMs > 0 ? cfg_.statsIntervalMs : 10000;
    while (!stop_.load()) {
        for (int i = 0; i < 20 && !stop_.load(); ++i) {
            std::this_thread::sleep_for(std::chrono::milliseconds(interval_ms / 20));
        }
        const auto elapsed = std::chrono::duration_cast<std::chrono::seconds>(std::chrono::steady_clock::now() - start).count();
        trust::errs() << "worker: uptime=" << elapsed << "s slots_busy=" << busySlots_.load() << "/" << cfg_.maxParallel << " jobs_done=" << jobsDone_.load()
                      << " jobs_failed=" << jobsFailed_.load() << " connected=" << (connected_.load() ? "yes" : "no") << "\n";
    }
}

int PlaygroundWorker::run() {
    if (cfg_.token.empty()) {
        trust::errs() << "trust-playground: worker.token is required\n";
        return 1;
    }
    if (cfg_.lspBin.empty()) {
        trust::errs() << "trust-playground: worker.lsp_bin is required\n";
        return 1;
    }
    if (cfg_.playgroundUrl.empty()) {
        trust::errs() << "trust-playground: worker.master_url is required\n";
        return 1;
    }
    {
        // TLS: для не-loopback хоста обязателен https:// (воркер передаёт свой auth-токен
        // и полезную нагрузку задач; через открытый http они были бы видны/подменяемы).
        std::string url_err;
        if (!trust::playground::validateWorkerPlaygroundUrl(cfg_.playgroundUrl, url_err)) {
            trust::errs() << "trust-playground: " << url_err << "\n";
            return 1;
        }
    }
    trust::errs() << "trust-playground (worker): connecting to " << cfg_.playgroundUrl << " ...\n";
    startTime_ = std::chrono::steady_clock::now();

    std::thread stats(&PlaygroundWorker::statsLoop, this);
    std::vector<std::thread> threads;
    for (int i = 0; i < cfg_.maxParallel; ++i) {
        threads.emplace_back(&PlaygroundWorker::slotLoop, this, i);
    }

    while (!stop_.load()) {
        std::this_thread::sleep_for(std::chrono::milliseconds(100));
    }
    for (std::thread& t : threads) {
        t.join();
    }
    stats.join();
    return 0;
}

} // namespace playground
} // namespace trust
