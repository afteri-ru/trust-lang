// test/playground_integration/main.cpp
// Интеграционный тест полной цепочки playground:
//   запрос (POST /run) -> балансировщик (trust-playground --playground)
//   -> воркер -> trust-lsp --json -> воркер -> балансировщик -> ответ.
//
// НЕ сервис и не установка: запускаются реальные бинарники из _build/ с
// временным конфигом в _build (минимальное взаимодействие с системой).
//
// Проверяются:
//   - успешная цепочка для НЕСКОЛЬКИХ разных примеров (все *.src из каталога);
//   - ОДНОВРЕМЕННЫЙ запуск нескольких /run (пул воркеров + очередь);
//   - обработка ошибок: нет воркеров -> 503 {unavailable,instructionsUrl},
//     неверный токен воркера -> 403, ошибка транспиляции -> ok:false + диагностика.

#include "playground/http.h"

#include <nlohmann/json.hpp>

#include <arpa/inet.h>
#include <netinet/in.h>
#include <sys/socket.h>
#include <sys/wait.h>
#include <unistd.h>

#include <algorithm>
#include <chrono>
#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <filesystem>
#include <fstream>
#include <iostream>
#include <sstream>
#include <string>
#include <thread>
#include <vector>

namespace {

int g_failures = 0;

#define CHECK(cond)                                                        \
    do {                                                                   \
        if (!(cond)) {                                                     \
            std::cerr << "FAIL (line " << __LINE__ << "): " #cond << "\n"; \
            ++g_failures;                                                  \
        }                                                                  \
    } while (0)

std::string readFile(const std::string& path) {
    std::ifstream f(path, std::ios::binary);
    if (!f) {
        return std::string();
    }
    std::ostringstream ss;
    ss << f.rdbuf();
    return ss.str();
}

// Список *.src в каталоге (сортированный) - несколько разных файлов для проверки.
std::vector<std::string> listSrcFiles(const std::string& dir) {
    namespace fs = std::filesystem;
    std::vector<std::string> out;
    std::error_code ec;
    for (auto it = fs::directory_iterator(dir, ec); it != fs::directory_iterator() && !ec; it.increment(ec)) {
        const auto& e = *it;
        if (e.is_regular_file(ec) && !ec && e.path().extension() == ".src") {
            out.push_back(e.path().string());
        }
    }
    std::sort(out.begin(), out.end());
    return out;
}

int getFreePort() {
    const int fd = ::socket(AF_INET, SOCK_STREAM, 0);
    if (fd < 0) {
        return -1;
    }
    struct sockaddr_in addr{};
    addr.sin_family = AF_INET;
    addr.sin_addr.s_addr = htonl(INADDR_LOOPBACK);
    addr.sin_port = 0;
    if (::bind(fd, reinterpret_cast<struct sockaddr*>(&addr), sizeof(addr)) < 0) {
        ::close(fd);
        return -1;
    }
    socklen_t len = sizeof(addr);
    if (::getsockname(fd, reinterpret_cast<struct sockaddr*>(&addr), &len) < 0) {
        ::close(fd);
        return -1;
    }
    const int port = static_cast<int>(ntohs(addr.sin_port));
    ::close(fd);
    return port;
}

pid_t spawn(const std::vector<std::string>& args) {
    const pid_t pid = ::fork();
    if (pid == 0) {
        std::vector<char*> argv;
        for (const std::string& a : args) {
            argv.push_back(const_cast<char*>(a.c_str()));
        }
        argv.push_back(nullptr);
        ::execv(args[0].c_str(), argv.data());
        ::_exit(127);
    }
    return pid;
}

bool waitReady(const std::string& url, int timeout_sec) {
    const auto deadline = std::chrono::steady_clock::now() + std::chrono::seconds(timeout_sec);
    while (std::chrono::steady_clock::now() < deadline) {
        const trust::playground::HttpResult r = trust::playground::httpPost(url + "/run", "x", 2);
        if (r.status != 0) {
            return true;
        }
        std::this_thread::sleep_for(std::chrono::milliseconds(200));
    }
    return false;
}

// POST /run с ретраями: возвращает первый ответ со статусом != 503 (503 = воркер
// ещё не зарегистрировался). attempts попыток с интервалом 500 мс.
trust::playground::HttpResult runWithRetry(const std::string& url, const std::string& src, int attempts) {
    for (int i = 0; i < attempts; ++i) {
        const trust::playground::HttpResult r = trust::playground::httpPost(url + "/run", src, 15);
        if (r.status != 503) {
            return r;
        }
        std::this_thread::sleep_for(std::chrono::milliseconds(500));
    }
    return trust::playground::HttpResult();
}

} // namespace

int main(int argc, const char* argv[]) {
    if (argc < 5) {
        std::cerr << "usage: playground_integration <trust-playground> <trust-lsp> <examples-dir> <tmp-dir>\n";
        return 2;
    }
    const std::string pg_bin = argv[1];
    const std::string lsp_bin = argv[2];
    const std::string examples_dir = argv[3];
    const std::string tmp_dir = argv[4];

    // Несколько разных примеров для проверки цепочки.
    const std::vector<std::string> files = listSrcFiles(examples_dir);
    CHECK(!files.empty());
    if (files.empty()) {
        std::cerr << "no *.src files in " << examples_dir << "\n";
        return 2;
    }

    const int port = getFreePort();
    if (port <= 0) {
        std::cerr << "no free port\n";
        return 2;
    }
    const std::string base = "http://127.0.0.1:" + std::to_string(port);
    const std::string token(64, 'a');

    // Временный конфиг - в _build (tmp_dir), не в /tmp.
    const std::string cfg_path = tmp_dir + "/trust_pg_int_" + std::to_string(::getpid()) + ".conf";
    {
        FILE* f = std::fopen(cfg_path.c_str(), "w");
        if (f == nullptr) {
            std::cerr << "cannot write config: " << cfg_path << "\n";
            return 2;
        }
        std::fprintf(f, "playground.listen=127.0.0.1\n");
        std::fprintf(f, "playground.port=%d\n", port);
        std::fprintf(f, "playground.poll_timeout=10\n");
        std::fprintf(f, "playground.job_timeout=10\n");
        std::fprintf(f, "playground.rate_limit_per_ip=100000\n");
        std::fprintf(f, "testworker=%s\n", token.c_str());
        std::fprintf(f, "worker.playground_url=%s\n", base.c_str());
        std::fprintf(f, "worker.token=%s\n", token.c_str());
        std::fprintf(f, "worker.lsp_bin=%s\n", lsp_bin.c_str());
        std::fprintf(f, "worker.max_parallel=2\n");
        std::fclose(f);
    }

    std::vector<pid_t> children;

    // 1. Запуск балансировщика.
    const pid_t server = spawn({pg_bin, "--playground", "--config", cfg_path});
    CHECK(server > 0);
    children.push_back(server);
    CHECK(waitReady(base, 15));

    // 2. Ошибка: нет воркеров -> 503 + unavailable.
    {
        const std::string src = readFile(files[0]);
        const trust::playground::HttpResult r = trust::playground::httpPost(base + "/run", src, 5);
        CHECK(r.status == 503);
        if (r.status == 503) {
            const auto j = nlohmann::json::parse(r.body);
            CHECK(j.value("unavailable", false) == true);
            CHECK(!j.value("instructionsUrl", std::string()).empty());
        }
    }

    // 3. Ошибка: неверный токен воркера -> 403.
    {
        const std::string poll = "{\"token\":\"deadbeef\",\"capacity\":1}";
        const trust::playground::HttpResult r = trust::playground::httpPost(base + "/poll", poll, 5);
        CHECK(r.status == 403);
    }

    // 4. Запуск воркера.
    const pid_t worker = spawn({pg_bin, "--config", cfg_path});
    CHECK(worker > 0);
    children.push_back(worker);

    // 5. Успешная цепочка для нескольких разных примеров.
    for (const std::string& file : files) {
        const std::string src = readFile(file);
        CHECK(!src.empty());
        const trust::playground::HttpResult r = runWithRetry(base, src, 40);
        CHECK(r.status == 200);
        if (r.status == 200) {
            const auto j = nlohmann::json::parse(r.body);
            CHECK(j.value("source", std::string()) == src);
            CHECK(j.contains("cpp"));
            CHECK(j.contains("trustToCpp"));
            CHECK(j.contains("cppToTrust"));
        }
    }

    // 5b. Ленивое скачивание build-архива: POST /download - отдельный запрос, заново
    //     обрабатывает файл и сразу отдаёт gzip-архив (без кеша на балансировщике).
    {
        const std::string src = readFile(files[0]);
        const trust::playground::HttpResult r = trust::playground::httpPost(base + "/download", src, 30);
        CHECK(r.status == 200);
        CHECK(r.body.size() >= 2);
        if (r.body.size() >= 2) {
            // Магическое число gzip (1f 8b) - подтверждает, что вернулся tar.gz.
            CHECK(static_cast<unsigned char>(r.body[0]) == 0x1f && static_cast<unsigned char>(r.body[1]) == 0x8b);
        }
    }

    // 6. Одновременный запуск нескольких /run (пул воркеров + очередь).
    {
        const size_t n = std::min<size_t>(4, files.size());
        std::vector<trust::playground::HttpResult> results(n);
        std::vector<std::thread> threads;
        for (size_t i = 0; i < n; ++i) {
            threads.emplace_back([&, i] {
                const std::string src = readFile(files[i]);
                results[i] = runWithRetry(base, src, 40);
            });
        }
        for (std::thread& t : threads) {
            t.join();
        }
        for (size_t i = 0; i < n; ++i) {
            CHECK(results[i].status == 200);
            if (results[i].status == 200) {
                const auto j = nlohmann::json::parse(results[i].body);
                CHECK(j.value("source", std::string()) == readFile(files[i]));
            }
        }
    }

    // 7. Ошибка транспиляции -> 200 {ok:false, error: диагностика}.
    {
        const std::string bad_src = "@main():={\n    undefined_fn_zz();\n}\n";
        const trust::playground::HttpResult r = runWithRetry(base, bad_src, 40);
        CHECK(r.status == 200);
        if (r.status == 200) {
            const auto j = nlohmann::json::parse(r.body);
            CHECK(j.value("ok", false) == false);
            CHECK(!j.value("error", std::string()).empty());
        }
    }

    // 8. Очистка (процессы). Временный конфиг НЕ удаляется - остаётся в _build.
    for (const pid_t c : children) {
        ::kill(c, SIGKILL);
        int status = 0;
        ::waitpid(c, &status, 0);
    }

    if (g_failures != 0) {
        std::cerr << "playground_integration: " << g_failures << " failure(s)\n";
        return 1;
    }
    std::cerr << "playground_integration: OK (server->worker->lsp->worker->server, " << files.size() << " files, concurrent)" << "\n";
    return 0;
}
