// src/lsp/main.cpp
// trust-lsp: Language Server для отображения и синхронизации Trust ↔ C++ кода

#include "lsp/trust_lsp.h"
#include "lsp/lsp_protocol.h"

#include <iostream>
#include <memory>

int main(int argc, const char *argv[]) {
    LspOptions opts = parseLspOptions(argc, argv);

    if (opts.help) {
        printLspUsage(argv[0]);
        return 0;
    }

    // ═══ Server mode ═══
    if (opts.port > 0) {
        int serverFd = createTcpLspServer(opts.port);
        if (serverFd < 0) {
            return 1;
        }

        int clientFd = acceptLspConnection(serverFd);
        ::close(serverFd);

        if (clientFd < 0) {
            return 1;
        }

        auto transport = std::make_unique<TcpLspTransport>(clientFd);
        TrustLsp server(*transport, opts);

        while (server.isRunning()) {
            auto req = readLspPacket(*transport);
            if (req.is_null() || req.empty()) {
                break;
            }
            if (req.contains("id")) {
                server.handleRequest(req);
            } else {
                server.handleNotification(req);
            }
        }

        return 0;
    }

    // ═══ Interactive mode (stdin/stdout) ═══
    std::cerr << "trust-lsp: starting in interactive mode\n"
              << "  project-dir: " << (opts.projectDir.empty() ? "(cwd)" : opts.projectDir) << "\n";

    auto transport = std::make_unique<StdioLspTransport>();
    TrustLsp server(*transport, opts);

    while (server.isRunning()) {
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
    }

    std::cerr << "trust-lsp: exiting\n";
    return 0;
}