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

// Конкурентно читает оба канала (stdout/stderr) до EOF или общего таймаута.
// В отличие от последовательного чтения не блокируется на полном канале одного fd,
// пока trust-lsp дописывает другой (иначе - дедлок/ложный таймаут, когда оба канала
// заполняются, например при большом объёме диагностик в stderr и большом C++ в stdout).
void drainTwoFds(int fd_a, int fd_b, std::string& out_a, std::string& out_b, int timeout_sec) {
    const auto deadline = std::chrono::steady_clock::now() + std::chrono::seconds(timeout_sec);
    bool a_eof = false;
    bool b_eof = false;
    char tmp[8192];
    while (!(a_eof && b_eof)) {
        struct pollfd fds[2];
        fds[0] = {a_eof ? -1 : fd_a, POLLIN, 0};
        fds[1] = {b_eof ? -1 : fd_b, POLLIN, 0};
        const auto remain = std::chrono::duration_cast<std::chrono::milliseconds>(deadline - std::chrono::steady_clock::now());
        if (remain.count() <= 0) {
            return; // общий таймаут
        }
        const int pr = ::poll(fds, 2, static_cast<int>(remain.count()));
        if (pr < 0) {
            if (errno == EINTR) {
                continue;
            }
            return;
        }
        if (pr == 0) {
            return; // таймаут
        }
        for (int i = 0; i < 2; ++i) {
            if (fds[i].revents == 0) {
                continue;
            }
            const ssize_t n = ::read(fds[i].fd, tmp, sizeof(tmp));
            if (n > 0) {
                (i == 0 ? out_a : out_b).append(tmp, static_cast<size_t>(n));
            } else {
                // EOF (0) или ошибка чтения - считаем канал завершённым (EINTR/EAGAIN -
                // временная ситуация, продолжаем цикл).
                const int e = errno;
                if (n < 0 && (e == EINTR || e == EAGAIN)) {
                    continue;
                }
                if (i == 0) {
                    a_eof = true;
                } else {
                    b_eof = true;
                }
            }
        }
    }
}

// Ждёт завершения дочернего процесса с таймаутом. Возвращает true, если завершился.
// Если out_status != nullptr и процесс завершился - сохраняет в него wait-статус
// (WEXITSTATUS/WTERMSIG для диагностики).
bool waitPidTimeout(pid_t pid, int timeout_sec, int* out_status = nullptr) {
    const auto deadline = std::chrono::steady_clock::now() + std::chrono::seconds(timeout_sec);
    while (true) {
        int status = 0;
        const pid_t r = ::waitpid(pid, &status, WNOHANG);
        if (r == pid) {
            if (out_status != nullptr) {
                *out_status = status;
            }
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

// Человекочитаемое описание wait-статуса дочернего процесса: код выхода или сигнал.
std::string describeExit(int status) {
    if (WIFEXITED(status)) {
        return "exited with code " + std::to_string(WEXITSTATUS(status));
    }
    if (WIFSIGNALED(status)) {
        const int sig = WTERMSIG(status);
        std::string s = "killed by signal " + std::to_string(sig);
        if (sig == SIGKILL) {
            s += " (SIGKILL - возможно, превышен лимит памяти/CPU: worker.max_memory_mb/max_output_kb)";
        }
        return s;
    }
    return "terminated unexpectedly";
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

// Корень временных файлов воркера: worker.tmp_dir или /tmp.
static std::string workerTmpRoot(const PlaygroundConfig& cfg) {
    const std::string root = cfg.tmpDir;
    return root.empty() ? std::string("/tmp") : root;
}

// Свободное место в корне tmp (МБ); -1 при ошибке statvfs.
static long long tmpFreeMb(const std::string& root) {
    struct statvfs vfs{};
    if (::statvfs(root.c_str(), &vfs) != 0) {
        return -1;
    }
    return (static_cast<long long>(vfs.f_bsize) * static_cast<long long>(vfs.f_bavail)) / (1024 * 1024);
}

// Удаляет осиротевшие временные каталоги/файлы воркера (trust-build-*, trust-worker-*)
// старше cfg.tmpTtlSec в корне tmp и в /tmp (curlHttpPost пишет в /tmp). Вызывается при
// старте и периодически - защита от переполнения диска после SIGKILL/сбоя, когда RAII
// TmpDirGuard не успевает удалить временный каталог.
static void cleanupOrphanTmp(const PlaygroundConfig& cfg) {
    const std::string root = workerTmpRoot(cfg);
    const int ttl = cfg.tmpTtlSec > 0 ? cfg.tmpTtlSec : 3600;
    std::vector<std::string> roots{root};
    if (root != "/tmp") {
        roots.push_back("/tmp");
    }
    std::error_code ec;
    for (const std::string& r : roots) {
        for (auto it = std::filesystem::directory_iterator(r, ec); it != std::filesystem::directory_iterator() && !ec; it.increment(ec)) {
            const std::string fn = it->path().filename().string();
            if (fn.rfind("trust-build-", 0) != 0 && fn.rfind("trust-worker-", 0) != 0) {
                continue;
            }
            std::error_code mtime_ec;
            const auto mt = std::filesystem::last_write_time(it->path(), mtime_ec);
            if (!mtime_ec) {
                const auto cutoff = std::filesystem::file_time_type::clock::now() - std::chrono::seconds(ttl);
                if (mt < cutoff) {
                    std::error_code rm_ec;
                    std::filesystem::remove_all(it->path(), rm_ec);
                }
            }
        }
    }
}

// Запускает trust-lsp --json в субпроцессе с лимитами; возвращает stdout (JSON-контракт).
// build_archive=true - дополнительно передаём --emit-build-dir: trust-lsp сам собирает
// build-архив (build-каталог pipeline, без компиляции), воркер читает tar.gz и встраивает
// в результат как base64 (используется ленивым POST /download).
std::string runTrustLsp(const std::string& code, const PlaygroundConfig& cfg, int64_t job_id, bool build_archive) {
    // build_archive: trust-lsp собирает build-архив во временный каталог (чистим по выходе).
    std::string emit_dir;
    if (build_archive) {
        // Гейт по свободному месту в tmp (защита от переполнения диска).
        const std::string root = workerTmpRoot(cfg);
        const long long free_mb = tmpFreeMb(root);
        if (cfg.diskFreeMinMb > 0 && (free_mb < 0 || free_mb < cfg.diskFreeMinMb)) {
            return makeErrorJson(code, "worker: insufficient disk space in " + root +
                                           (free_mb < 0 ? " (statvfs failed)" : " (" + std::to_string(free_mb) + " MB free)"));
        }
        std::string tmpl = root + "/trust-build-XXXXXX";
        char* dir = ::mkdtemp(&tmpl[0]);
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
        // Дополнительные лимиты субпроцесса: исчерпание fd и дампы ядра.
        // ВНИМАНИЕ: RLIMIT_NPROC НЕ ставим - он считается на весь UID (а не на дерево
        // процесса), и низкое значение ломает сборку build-архива (trust-lsp форкает tar),
        // а при высокой загрузке пользователя может уронить и чужие процессы.
        struct rlimit rl_nofile{256, 256};
        ::setrlimit(RLIMIT_NOFILE, &rl_nofile);
        struct rlimit rl_core{0, 0};
        ::setrlimit(RLIMIT_CORE, &rl_core);

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
        // Дочерний процесс: execv не удался (бинарник отсутствует/не исполняемый/нет
        // библиотек/ENOMEM и т.п.). Пишем причину в stderr - родитель отдаёт её в контракт
        // (log/error) и на сайт, вместо немого дефолта "worker transpile failed".
        {
            const int exec_err = errno;
            const std::string msg = "trust-playground (worker): cannot exec trust-lsp '" + cfg.lspBin + "': " + std::strerror(exec_err) + "\n";
            ::write(STDERR_FILENO, msg.data(), msg.size());
        }
        ::_exit(127);
    }

    ::close(stdin_pipe[0]);
    ::close(stdout_pipe[1]);
    ::close(stderr_pipe[1]);
    ::write(stdin_pipe[1], code.data(), code.size());
    ::close(stdin_pipe[1]);

    std::string out;
    std::string err;
    drainTwoFds(stdout_pipe[0], stderr_pipe[0], out, err, cfg.workerJobTimeoutSec);
    ::close(stdout_pipe[0]);
    ::close(stderr_pipe[0]);

    int exit_status = 0;
    if (!waitPidTimeout(pid, cfg.workerJobTimeoutSec, &exit_status)) {
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
        // trust-lsp --json пишет JSON в stdout даже при неудачной транспиляции (main.cpp:
        // resultToJson возвращается всегда, код выхода 0/1), поэтому пустой stdout означает
        // провал execv, жёсткий крах или OOM/сигнальный килл - без вывода диагностики.
        // Даём пользователю причину (код выхода/сигнал), а не немой дефолт.
        const std::string exit_desc = describeExit(exit_status);
        nlohmann::json j{{"source", code},
                         {"cpp", ""},
                         {"ok", false},
                         {"error", err.empty() ? ("worker transpile failed (" + exit_desc + ")") : err},
                         {"trustToCpp", nlohmann::json::array()},
                         {"cppToTrust", nlohmann::json::array()}};
        if (!err.empty()) {
            j["log"] = err;
        } else {
            j["log"] = "trust-lsp produced no output and " + exit_desc;
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
            } else if (!j.contains("error")) {
                // trust-lsp не создал архив (stderr - реальная причина: "tar failed",
                // "cannot locate trust-runtime..." и т.п.). Доносим причину до клиента,
                // чтобы вместо немого "archive not produced" была видна диагностика.
                std::string reason = "archive not produced: trust-lsp --emit-build-dir returned no *.tar.gz";
                if (!err.empty()) {
                    reason += " (" + err + ")";
                }
                j["error"] = reason;
                if (!j.contains("log")) {
                    j["log"] = err;
                }
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
            continue; // задач нет - сразу переполлить
        }
        if (r.status == 200) {
            busySlots_.fetch_add(1);
            try {
                const auto job = nlohmann::json::parse(r.body);
                const int64_t job_id = job.value("jobId", int64_t(0));
                const std::string code = job.value("code", std::string());
                const bool build_archive = job.value("buildArchive", false);
                // Маскируем токен воркера в результате: если он ошибочно попал в stderr
                // trust-lsp (например, как "unknown option '<токен>'"), публичный ответ
                // песочницы не должен его содержать.
                const std::string result = redactToken(runTrustLsp(code, cfg_, job_id, build_archive), cfg_.token);
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
        // Периодическая очистка осиротевших временных каталогов (защита от переполнения /tmp).
        cleanupOrphanTmp(cfg_);
        const auto elapsed = std::chrono::duration_cast<std::chrono::seconds>(std::chrono::steady_clock::now() - start).count();
        trust::errs() << "worker: uptime=" << elapsed << "s slots_busy=" << busySlots_.load() << "/" << cfg_.maxParallel << " jobs_done=" << jobsDone_.load()
                      << " jobs_failed=" << jobsFailed_.load() << " connected=" << (connected_.load() ? "yes" : "no") << "\n";
    }
}

// Заменяет вхождения токена воркера в тексте на "<redacted>" (токен воркера не должен
// попадать в публичный ответ песочницы). Токен - 64 hex; маскируем только точные
// совпадения, чтобы не трогать посторонние 64-hex значения в исходниках пользователя.
std::string redactToken(const std::string& text, const std::string& token) {
    if (token.empty() || token.size() < 4) {
        return text;
    }
    std::string out;
    out.reserve(text.size());
    size_t pos = 0;
    while (pos < text.size()) {
        const size_t hit = text.find(token, pos);
        if (hit == std::string::npos) {
            out.append(text, pos, std::string::npos);
            break;
        }
        out.append(text, pos, hit - pos);
        out += "<redacted>";
        pos = hit + token.size();
    }
    return out;
}

// Проверка настроек воркера на этапе запуска (fail-fast вместо поздних ошибок на
// каждой задаче). Покрывает: исполняемый файл trust-lsp, рабочий каталог, формат
// lsp_opts и здравые числовые лимиты.
std::string validateWorkerSettings(const PlaygroundConfig& cfg) {
    if (cfg.maxParallel < 1) {
        return "worker.max_parallel must be >= 1 (got " + std::to_string(cfg.maxParallel) + ")";
    }
    if (cfg.workerJobTimeoutSec <= 0) {
        return "worker.job_timeout must be > 0 (got " + std::to_string(cfg.workerJobTimeoutSec) + ")";
    }
    if (cfg.maxMemoryMb <= 0) {
        return "worker.max_memory_mb must be > 0 (got " + std::to_string(cfg.maxMemoryMb) + ")";
    }
    if (cfg.maxOutputKb <= 0) {
        return "worker.max_output_kb must be > 0 (got " + std::to_string(cfg.maxOutputKb) + ")";
    }
    // trust-lsp запускается через execv(cfg.lspBin), который НЕ ищет в PATH, поэтому
    // проверяем путь так же напрямую: файл существует и исполняемый.
    if (!cfg.lspBin.empty()) {
        if (::access(cfg.lspBin.c_str(), X_OK) != 0) {
            return "worker.lsp_bin is not an executable file: '" + cfg.lspBin + "' (" + std::strerror(errno) + ")";
        }
    }
    if (!cfg.projectDir.empty()) {
        std::error_code ec;
        if (!std::filesystem::exists(cfg.projectDir, ec) || !std::filesystem::is_directory(cfg.projectDir, ec)) {
            return "worker.project_dir does not exist or is not a directory: '" + cfg.projectDir + "'";
        }
    }
    // lsp_opts всегда передаются в trust-lsp как отдельные аргументы; каждый НЕПУСТОЙ
    // должен быть опцией (начинаться с '-'). Ловим случайный токен/позиционный аргумент,
    // который иначе дал бы trust-lsp "unknown option '<токен>'" (и утёк бы на сайт).
    // Пустой список/пустые элементы допустимы: worker.lsp_opts может не быть настроен.
    for (const std::string& o : cfg.lspOpts) {
        if (!o.empty() && o[0] != '-') {
            return "worker.lsp_opts entry must be an option (start with '-'): '" + o + "'";
        }
    }
    return std::string();
}

// Самопроверка при запуске: прогоняет реальные сценарии работы (транспиляция + сборка
// build-архива) тем же путём, что и балансировщик, ДО подключения к нему. Если trust-lsp
// или окружение не могут выполнить хотя бы один сценарий (напр. нет tar, не находится
// trust-runtime и т.п.), воркер не регистрируется, а выдаёт диагностику и завершается.
// Возвращает пустую строку при успехе или описание первой проблемы.
static std::string runStartupSelfCheck(const PlaygroundConfig& cfg) {
    const std::string code = "print(\"trust-playground worker self-check\");\n";

    // 1) Транспиляция (build_archive=false) - сценарий POST /run.
    {
        const std::string res = runTrustLsp(code, cfg, /*job_id=*/0, /*build_archive=*/false);
        try {
            const nlohmann::json j = nlohmann::json::parse(res);
            if (!j.value("ok", false) || j.value("cpp", std::string()).empty()) {
                return "startup self-check: transpile failed: " + j.value("error", res);
            }
        } catch (const std::exception&) {
            return "startup self-check: transpile produced invalid result: " + res;
        }
    }

    // 2) Сборка build-архива (build_archive=true) - сценарий POST /download.
    {
        const std::string res = runTrustLsp(code, cfg, /*job_id=*/0, /*build_archive=*/true);
        try {
            const nlohmann::json j = nlohmann::json::parse(res);
            const std::string arch = j.value("archive", std::string());
            const std::string name = j.value("archiveName", std::string());
            if (arch.empty() || name.empty()) {
                return "startup self-check: archive build failed: " + j.value("error", res);
            }
        } catch (const std::exception&) {
            return "startup self-check: archive build produced invalid result: " + res;
        }
    }

    return std::string();
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
    // Fail-fast: проверяем настройки (рабочий каталог, исполняемый trust-lsp, lsp_opts,
    // лимиты) до подключения к балансировщику, чтобы не регистрироваться с конфигом,
    // который не сможет выполнить ни одной задачи (каждая задача падала бы с ошибкой).
    {
        const std::string verr = trust::playground::validateWorkerSettings(cfg_);
        if (!verr.empty()) {
            trust::errs() << "trust-playground: " << verr << "\n";
            return 1;
        }
    }
    // Самопроверка реальных сценариев (транспиляция + сборка build-архива) ДО подключения
    // к балансировщику: воркер не регистрируется, если trust-lsp/окружение их не выполняют.
    {
        const std::string serr = runStartupSelfCheck(cfg_);
        if (!serr.empty()) {
            trust::errs() << "trust-playground: " << serr << "\n";
            return 1;
        }
        trust::errs() << "trust-playground (worker): startup self-check OK (transpile + build archive)\n";
    }
    // Чистим осиротевшие временные каталоги предыдущих запусков (защита от переполнения /tmp).
    cleanupOrphanTmp(cfg_);
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
