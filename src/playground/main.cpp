// src/playground/main.cpp
// trust-playground: единый бинарник для двух режимов.
//   --playground          - балансировщик (принимает запросы сайта, диспетчеризация)
//   (без аргумента)       - исполнительный VPS (reverse long-poll к балансировщику)
//
// Воркер НЕ требует root/установки: читает trust-playground.conf рядом с бинарником;
// если файла нет - нужны CLI-опции --playground-url и --token. При корректном запуске
// с CLI-опциями они сохраняются в файл как настройки по умолчанию (или по --save-config).

#include "playground/config.h"
#include "playground/server.h"
#include "playground/worker.h"

#include "pipeline/cli.hpp"
#include "utils/io.hpp"

#include <algorithm>
#include <csignal>
#include <cstdio>
#include <signal.h>
#include <stdexcept>
#include <string>
#include <vector>

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
// после обработчика и не возвращает EINTR - балансировщик не останавливался по Ctrl+C.
void installSignalHandlers() {
    struct sigaction sa{};
    sa.sa_handler = handleSignal;
    ::sigemptyset(&sa.sa_mask);
    sa.sa_flags = 0; // БЕЗ SA_RESTART
    ::sigaction(SIGINT, &sa, nullptr);
    ::sigaction(SIGTERM, &sa, nullptr);
}

// -- Единый арity-aware парсер драйвера (см. pipeline/cli.hpp) --
// Опции playground объявлены таблицей DriverOption - единый источник для парсера и справки.

enum class PlaygroundOptId {
    Help,
    Config,
    Playground,
    PlaygroundUrl,
    Token,
    MaxParallel,
    Lsp,
    SaveConfig,
    GenToken,
};

std::vector<trust::DriverOption> playgroundTable() {
    using namespace trust;
    return {
        {int(PlaygroundOptId::Help), "help", "h", CliOpt::Flag, "", "Show this help message", CliCategory::General},
        {int(PlaygroundOptId::Config), "config", "", CliOpt::Value, "path", "Config file (default: <binary dir>/trust-playground.conf)",
         CliCategory::InputOutput},
        {int(PlaygroundOptId::Playground), "playground", "", CliOpt::Flag, "", "Run as playground (balancer) server", CliCategory::ProjectSpecific},
        {int(PlaygroundOptId::PlaygroundUrl), "playground-url", "", CliOpt::Value, "url", "Balancer URL (worker; saved to config)",
         CliCategory::ProjectSpecific},
        {int(PlaygroundOptId::Token), "token", "", CliOpt::Value, "hex", "Worker token (worker; saved to config)", CliCategory::ProjectSpecific},
        {int(PlaygroundOptId::MaxParallel), "max-parallel", "", CliOpt::Value, "n", "Parallel tasks (worker)", CliCategory::ProjectSpecific},
        {int(PlaygroundOptId::Lsp), "lsp", "", CliOpt::Value, "path", "Path to trust-lsp binary (worker)", CliCategory::ProjectSpecific},
        {int(PlaygroundOptId::SaveConfig), "save-config", "", CliOpt::Flag, "", "Save effective worker settings to config file", CliCategory::ProjectSpecific},
        {int(PlaygroundOptId::GenToken), "gen-token", "", CliOpt::OptionalValue, "n", "Generate worker token(s); --gen-token=10 for 10 (default 1)",
         CliCategory::ProjectSpecific},
    };
}

void printUsage(const char* prog) {
    trust::errs() << "Usage: " << prog << " [options]\n\n"
                  << "trust-playground: playground (balancer) or executor (worker).\n\n";
    trust::errs() << trust::driverHelp(playgroundTable());
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

    // Единый арity-aware парсер драйвера (см. pipeline/cli.hpp). Позиционных и `-W` у playground
    // нет (input_file/diag/diag_help остаются пустыми). `--gen-token` - OptionalValue: значение
    // задаётся ТОЛЬКО через `=` (`--gen-token=10`), следующий токен не потребляется.
    std::vector<std::string> args;
    args.reserve(static_cast<std::size_t>(argc));
    for (int i = 0; i < argc; ++i) {
        args.emplace_back(argv[i]);
    }
    const auto table = playgroundTable();
    std::vector<std::string> diag;
    std::string input_file;
    bool diag_help = false;
    auto apply = [&](int id, const std::string& v) -> bool {
        switch (static_cast<PlaygroundOptId>(id)) {
        case PlaygroundOptId::Help:
            help = true;
            break;
        case PlaygroundOptId::Config:
            config_path = trust::playground::unquote(v);
            break;
        case PlaygroundOptId::Playground:
            playground_mode = true;
            break;
        case PlaygroundOptId::PlaygroundUrl:
            playground_url = trust::playground::unquote(v);
            break;
        case PlaygroundOptId::Token:
            token = trust::playground::unquote(v);
            break;
        case PlaygroundOptId::MaxParallel:
            try {
                max_parallel = std::stoi(v);
            } catch (const std::exception&) {
                trust::errs() << "Error: invalid integer for --max-parallel: '" << v << "'\n";
                return false;
            }
            break;
        case PlaygroundOptId::Lsp:
            lsp_bin = trust::playground::unquote(v);
            break;
        case PlaygroundOptId::SaveConfig:
            save_config = true;
            break;
        case PlaygroundOptId::GenToken:
            gen_token = true;
            gen_count = 1;
            if (!v.empty()) {
                try {
                    gen_count = std::clamp(std::stoi(v), 1, 1000);
                } catch (const std::exception&) {
                    gen_count = 1;
                }
            }
            break;
        }
        return true;
    };
    const std::string err = trust::parseDriverArgs(args, table, apply, diag, input_file, diag_help);
    if (!err.empty()) {
        trust::errs() << "Error: " << err << "\n";
        return 1;
    }
    if (!input_file.empty()) {
        trust::errs() << "Error: unexpected argument '" << input_file << "'\n";
        return 1;
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
        // -- Воркер: без root, конфиг рядом с бинарником, CLI-опции сохраняются --
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
        // Автосохранение при первом запуске - только когда параметров достаточно для запуска.
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
        // Токен не передаётся в URL (убрано ?token=). Доступ к статистике - через форму
        // входа /stats/login (cookie-сессия) или заголовок X-Stats-Token (скрипты).
        trust::errs() << "trust-playground (playground): stats: http://" << cfg.listen << ":" << cfg.port << "/stats/login\n";
    }
    return server.run();
}
