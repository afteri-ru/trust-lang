// src/playground/main.cpp
// trust-playground: единый бинарник для двух режимов.
//   --playground          — балансировщик (принимает запросы сайта, диспетчеризация)
//   (без аргумента)       — исполнительный VPS (reverse long-poll к балансировщику)
//
// Воркер НЕ требует root/установки: читает trust-playground.conf рядом с бинарником;
// если файла нет — нужны CLI-опции --playground-url и --token. При корректном запуске
// с CLI-опциями они сохраняются в файл как настройки по умолчанию (или по --save-config).

#include "playground/config.h"
#include "playground/server.h"
#include "playground/worker.h"

#include "utils/io.hpp"

#include <algorithm>
#include <csignal>
#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <signal.h>
#include <string>

namespace {

trust::playground::PlaygroundServer* g_master = nullptr;
trust::playground::PlaygroundWorker* g_worker = nullptr;
volatile std::sig_atomic_t g_stop = 0;

void handleSignal(int) {
    g_stop = 1;
    if (g_master != nullptr) {
        g_master->requestStop();
    }
    if (g_worker != nullptr) {
        g_worker->requestStop();
    }
}

// Устанавливает обработчики SIGINT/SIGTERM без SA_RESTART: блокирующие вызовы
// (accept/poll/connect) прерываются EINTR, и циклы могут перепроверить stop_.
// std::signal() на glibc ставит SA_RESTART, из-за чего accept() перезапускается
// после обработчика и не возвращает EINTR — балансировщик не останавливался по Ctrl+C.
void installSignalHandlers() {
    struct sigaction sa{};
    sa.sa_handler = handleSignal;
    ::sigemptyset(&sa.sa_mask);
    sa.sa_flags = 0; // БЕЗ SA_RESTART
    ::sigaction(SIGINT, &sa, nullptr);
    ::sigaction(SIGTERM, &sa, nullptr);
}

void printUsage(const char* prog) {
    trust::errs() << "Usage: " << prog << " [options]\n"
                  << "\n"
                  << "trust-playground: playground (balancer) or executor (worker).\n"
                  << "  --playground                Run as playground (balancer) server\n"
                  << "  --config <path>             Config file (default: <binary dir>/trust-playground.conf)\n"
                  << "  --playground-url <url>      Balancer URL (worker; saved to config)\n"
                  << "  --token <hex>               Worker token (worker; saved to config)\n"
                  << "  --max-parallel <n>          Parallel tasks (worker)\n"
                  << "  --lsp <path>                Path to trust-lsp binary (worker)\n"
                  << "  --save-config               Save effective worker settings to config file\n"
                  << "  --gen-token [n]             Generate n (default 1) worker tokens (64 hex)\n"
                  << "  --help                      Show this help\n";
}

// Возвращает каталог, в котором находится бинарник (для дефолтного пути конфига).
std::string binaryDir(const char* argv0) {
    const std::string path = argv0 != nullptr ? argv0 : "";
    const size_t slash = path.find_last_of('/');
    if (slash == std::string::npos) {
        return std::string(".");
    }
    if (slash == 0) {
        return std::string("/");
    }
    return path.substr(0, slash);
}

bool fileExists(const std::string& path) {
    FILE* f = std::fopen(path.c_str(), "r");
    if (f != nullptr) {
        std::fclose(f);
        return true;
    }
    return false;
}

} // namespace

int main(int argc, const char* argv[]) {
    bool playground_mode = false;
    bool gen_token = false;
    int gen_count = 1;
    std::string config_path = binaryDir(argv[0]) + "/trust-playground.conf";
    std::string lsp_bin, playground_url, token;
    int max_parallel = -1;
    bool save_config = false;
    bool help = false;

    auto next_arg = [&](int& i) -> std::string {
        if (++i >= argc) {
            trust::errs() << "Error: " << argv[i - 1] << " requires an argument\n";
            std::exit(1);
        }
        return argv[i];
    };

    for (int i = 1; i < argc; ++i) {
        if (std::strcmp(argv[i], "--playground") == 0) {
            playground_mode = true;
        } else if (std::strcmp(argv[i], "--config") == 0) {
            config_path = next_arg(i);
        } else if (std::strcmp(argv[i], "--playground-url") == 0) {
            playground_url = next_arg(i);
        } else if (std::strcmp(argv[i], "--token") == 0) {
            token = next_arg(i);
        } else if (std::strcmp(argv[i], "--max-parallel") == 0) {
            max_parallel = std::stoi(next_arg(i));
        } else if (std::strcmp(argv[i], "--lsp") == 0) {
            lsp_bin = next_arg(i);
        } else if (std::strcmp(argv[i], "--save-config") == 0) {
            save_config = true;
        } else if (std::strcmp(argv[i], "--gen-token") == 0) {
            gen_token = true;
            // Необязательный счётчик: потребляем следующий аргумент, только если это число.
            if (i + 1 < argc) {
                const std::string next = argv[i + 1];
                if (!next.empty() && next.find_first_not_of("0123456789") == std::string::npos) {
                    try {
                        gen_count = std::clamp(std::stoi(next), 1, 1000);
                        ++i;
                    } catch (const std::exception&) {
                        gen_count = 1;
                    }
                }
            }
        } else if (std::strcmp(argv[i], "--help") == 0) {
            help = true;
        } else {
            trust::errs() << "Error: unknown option '" << argv[i] << "'\n";
            return 1;
        }
    }

    if (help) {
        printUsage(argv[0]);
        return 0;
    }

    // --gen-token: генерация токенов воркера (независимо от режима).
    if (gen_token) {
        for (int i = 0; i < gen_count; ++i) {
            const std::string tok = trust::playground::generateToken();
            if (tok.empty()) {
                trust::errs() << "trust-playground: cannot generate token: /dev/urandom unavailable\n";
                return 1;
            }
            std::printf("%s\n", tok.c_str());
        }
        return 0;
    }

    trust::playground::PlaygroundConfig cfg;
    std::string error;
    const bool cfg_exists = fileExists(config_path);
    if (cfg_exists) {
        if (!trust::playground::loadConfig(config_path, cfg, error)) {
            trust::errs() << "trust-playground: " << error << "\n";
            return 1;
        }
    }

    // CLI-опции переопределяют конфиг.
    if (!playground_url.empty()) {
        cfg.playgroundUrl = playground_url;
    }
    if (!token.empty()) {
        cfg.token = token;
    }
    if (max_parallel > 0) {
        cfg.maxParallel = max_parallel;
    }
    if (!lsp_bin.empty()) {
        cfg.lspBin = lsp_bin;
    }

    if (!playground_mode) {
        // ── Воркер: без root, конфиг рядом с бинарником, CLI-опции сохраняются ──
        bool runnable = true;
        if (cfg.token.empty()) {
            trust::errs() << "trust-playground: worker requires a token to run.\n"
                          << "  Provide it via config file (" << config_path << ") or CLI: --token <hex>\n";
            runnable = false;
        } else if (!trust::playground::isValidToken(cfg.token)) {
            trust::errs() << "trust-playground: worker.token must be 64 hex chars\n";
            runnable = false;
        } else if (cfg.lspBin.empty()) {
            trust::errs() << "trust-playground: worker.lsp_bin is required (--lsp <path>)\n";
            runnable = false;
        }

        // --save-config сохраняет конфиг даже без playground-url/token (можно заранее
        // задать остальные worker-настройки; url/token добавить позже вручную в конфиг).
        // Автосохранение при первом запуске — только когда параметров достаточно для запуска.
        if (save_config || (!cfg_exists && runnable)) {
            std::string serr;
            if (trust::playground::saveWorkerConfig(config_path, cfg, serr)) {
                trust::errs() << "trust-playground: settings saved to " << config_path << " as defaults\n";
            } else {
                trust::errs() << "trust-playground: warning: " << serr << "\n";
            }
        }

        if (!runnable) {
            return 1;
        }

        trust::playground::PlaygroundWorker worker(cfg);
        g_worker = &worker;
        installSignalHandlers();
        return worker.run();
    }

    trust::playground::PlaygroundServer server(cfg);
    g_master = &server;
    installSignalHandlers();
    if (!cfg.statsToken.empty()) {
        trust::errs() << "trust-playground (playground): stats: http://" << cfg.listen << ":" << cfg.port << "/stats?token=" << cfg.statsToken << "\n";
    }
    return server.run();
}
