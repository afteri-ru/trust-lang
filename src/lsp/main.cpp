// src/lsp/main.cpp
// trust-lsp: Language Server для отображения и синхронизации Trust ↔ C++ кода

#include "lsp/lsp_protocol.h"
#include "lsp/trust_lsp.h"
#include "utils/backtrace.hpp"

#include <iostream>
#include <memory>

int main(int argc, const char* argv[]) {
    trust::utils::install_fault_handler();
    LspOptions opts = parseLspOptions(argc, argv);

    if (opts.help) {
        printLspUsage(argv[0]);
        return 0;
    }

    // ═══ Server mode ═══
    if (opts.port > 0) {
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
                std::cerr << "trust-lsp: FATAL ERROR (tcp): " << e.what() << "\n";
            } catch (...) {
                std::cerr << "trust-lsp: FATAL ERROR (tcp): unknown exception\n";
            }
        }

        return 0;
    }

    // ═══ Interactive mode (stdin/stdout) ═══
    std::cerr << "trust-lsp: starting in interactive mode\n"
              << "  project-dir: " << (opts.projectDir.empty() ? "(cwd)" : opts.projectDir) << "\n";

    auto transport = std::make_unique<trust::transport::StdioTransport>();
    TrustLsp server(*transport, opts);

    while (server.isRunning()) {
        try {
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
            std::cerr << "trust-lsp: FATAL ERROR (interactive): " << e.what() << "\n";
            // Продолжаем цикл — сервер не должен упасть
        } catch (...) {
            std::cerr << "trust-lsp: FATAL ERROR (interactive): unknown exception\n";
        }
    }

    std::cerr << "trust-lsp: exiting\n";
    return 0;
}