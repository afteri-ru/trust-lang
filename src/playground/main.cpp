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

#include <csignal>
#include <cstdio>
#include <cstdlib>
#include <cstring>
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
        if (cfg.playgroundUrl.empty() || cfg.token.empty()) {
            trust::errs() << "trust-playground: worker requires playground-url and token.\n"
                          << "  Provide them via config file (" << config_path << ") or CLI:\n"
                          << "    " << argv[0] << " --playground-url <url> --token <hex> [--lsp <path>]\n";
            return 1;
        }
        if (!trust::playground::isValidToken(cfg.token)) {
            trust::errs() << "trust-playground: worker.token must be 64 hex chars\n";
            return 1;
        }
        if (cfg.lspBin.empty()) {
            trust::errs() << "trust-playground: worker.lsp_bin is required (--lsp <path>)\n";
            return 1;
        }

        if (save_config || !cfg_exists) {
            std::string serr;
            if (trust::playground::saveWorkerConfig(config_path, cfg, serr)) {
                trust::errs() << "trust-playground: settings saved to " << config_path << " as defaults\n";
            } else {
                trust::errs() << "trust-playground: warning: " << serr << "\n";
            }
        }

        trust::playground::PlaygroundWorker worker(cfg);
        g_worker = &worker;
        std::signal(SIGINT, handleSignal);
        std::signal(SIGTERM, handleSignal);
        return worker.run();
    }

    trust::playground::PlaygroundServer server(cfg);
    g_master = &server;
    std::signal(SIGINT, handleSignal);
    std::signal(SIGTERM, handleSignal);
    if (!cfg.statsToken.empty()) {
        trust::errs() << "trust-playground (playground): stats: http://" << cfg.listen << ":" << cfg.port << "/stats?token=" << cfg.statsToken << "\n";
    }
    return server.run();
}
