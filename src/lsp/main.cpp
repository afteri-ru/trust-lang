// src/lsp/main.cpp
// trust-lsp: Language Server для отображения и синхронизации Trust ↔ C++ кода

#include "lsp/lsp_protocol.h"
#include "lsp/trust_lsp.h"
#include "lsp/html_emit.h"
#include "pipeline/pipeline.hpp"
#include "utils/backtrace.hpp"
#include "utils/file_io.hpp"
#include "utils/io.hpp"

#include <iostream>
#include <iterator>
#include <memory>
#include "utils/io.hpp"

int main(int argc, const char* argv[]) {
    trust::utils::install_fault_handler();
    LspOptions opts = parseLspOptions(argc, argv);

    if (opts.help) {
        printLspUsage(argv[0]);
        return 0;
    }

    // ═══ Playground output modes (--json / --html) ═══
    // In-process транспиляция Trust → C++ + построчный source-map,
    // результат — JSON (live-контракт) или godbolt-стиль HTML-фрагмент.
    if (opts.mode == LspMode::Json || opts.mode == LspMode::Html) {
        std::string code;
        std::string fileName = opts.inputFile;
        if (opts.inputFile.empty() || opts.inputFile == "-") {
            code.assign(std::istreambuf_iterator<char>(std::cin), std::istreambuf_iterator<char>());
            fileName = "stdin.src";
        } else {
            auto data = trust::utils::FileIO::read<std::vector<char>>(opts.inputFile);
            if (!data) {
                trust::errs() << "trust-lsp: cannot open file: " << opts.inputFile << "\n";
                return 1;
            }
            code.assign(data->data(), data->size());
        }

        auto result = trust::lsp::transpileToResult(code, fileName, opts);
        if (opts.mode == LspMode::Json) {
            // --emit-build-dir: дополнительно собрать tar.gz build-каталога (без компиляции)
            // для скачиваемого архива. JSON-контракт в stdout не меняется; архив остаётся
            // на диске по пути <dir>/trust-lang-<версия>-generated.tar.gz.
            if (!opts.emitBuildDir.empty()) {
                std::string err;
                const std::string archive = trust::emitBuildDirArchive(code, opts.emitBuildDir, err);
                if (archive.empty()) {
                    trust::errs() << "trust-lsp: --emit-build-dir failed: " << err << "\n";
                    return 1;
                }
            }
            std::cout << trust::lsp::resultToJson(result) << "\n";
        } else {
            if (!opts.examplesDir.empty()) {
                opts.examples = trust::lsp::loadExamplesFromDir(opts.examplesDir);
            }
            const std::string monacoUrl = opts.monacoUrl.empty() ? trust::lsp::kDefaultMonacoUrl : opts.monacoUrl;
            std::cout << trust::lsp::resultToHtml(result, opts, monacoUrl, opts.serverUrl, opts.htmlFull);
        }
        return result.ok ? 0 : 1;
    }

    // ═══ Server mode ═══
    if (opts.mode == LspMode::Server) {
        int serverFd = trust::transport::createTcpServer(opts.port);
        if (serverFd < 0) {
            return 1;
        }

        int clientFd = trust::transport::acceptConnection(serverFd);
        ::close(serverFd);

        if (clientFd < 0) {
            return 1;
        }

        auto transport = std::make_unique<trust::transport::TcpTransport>(clientFd);
        TrustLsp server(*transport, opts);

        while (server.isRunning()) {
            try {
                // Сброс отложенных (debounce) пере-транспиляций
                server.flushPendingTranspile();
                // Ожидание ввода с таймаутом (чтобы периодически сбрасывать debounce)
                int r = transport->waitInput(50);
                if (r < 0) {
                    break;
                }
                if (r == 0) {
                    continue;
                }
                auto req = readLspPacket(*transport);
                if (req.is_null() || req.empty()) {
                    break;
                }
                if (req.contains("id")) {
                    server.handleRequest(req);
                } else {
                    server.handleNotification(req);
                }
            } catch (const std::exception& e) {
                trust::errs() << "trust-lsp: FATAL ERROR (tcp): " << e.what() << "\n";
            } catch (...) {
                trust::errs() << "trust-lsp: FATAL ERROR (tcp): unknown exception\n";
            }
        }

        return 0;
    }

    // ═══ Interactive mode (stdin/stdout) ═══
    trust::errs() << "trust-lsp: starting in interactive mode\n"
                  << "  project-dir: " << (opts.projectDir.empty() ? "(cwd)" : opts.projectDir) << "\n";

    auto transport = std::make_unique<trust::transport::StdioTransport>();
    TrustLsp server(*transport, opts);

    while (server.isRunning()) {
        try {
            // Сброс отложенных (debounce) пере-транспиляций
            server.flushPendingTranspile();
            // Ожидание ввода с таймаутом (чтобы периодически сбрасывать debounce)
            int r = transport->waitInput(50);
            if (r < 0) {
                break;
            }
            if (r == 0) {
                continue;
            }

            auto req = readLspPacket(*transport);
            if (req.is_null() || req.empty()) {
                break;
            }

            // requests have "id", notifications don't
            if (req.contains("id")) {
                server.handleRequest(req);
            } else {
                server.handleNotification(req);
            }
        } catch (const std::exception& e) {
            trust::errs() << "trust-lsp: FATAL ERROR (interactive): " << e.what() << "\n";
            // Продолжаем цикл — сервер не должен упасть
        } catch (...) {
            trust::errs() << "trust-lsp: FATAL ERROR (interactive): unknown exception\n";
        }
    }

    trust::errs() << "trust-lsp: exiting\n";
    return 0;
}