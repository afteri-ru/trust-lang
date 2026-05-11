// -----------------------------------------------------------------------
// trust-dap — DAP-сервер для отладки trust-lang (точка входа)
//
// Запускается VSCode, читает JSON-RPC из stdin (interactive) или из TCP
// (server), пишет в stdout / TCP.
//
// Использует GdbDebug (GDB/MI) для управления отладкой и trust::SourceMapReader
// для трансляции позиций между trust- и C++-файлами.
// -----------------------------------------------------------------------

#include "debug/dap_handler.hpp"
#include "debug/dap_transport.h"

#include <nlohmann/json.hpp>

#include <iostream>
#include <memory>

using json = nlohmann::json;

// ── Main ──
int main(int argc, const char* argv[]) {
    DapOptions opts = parseDapOptions(argc, argv);

    if (opts.help) {
        printUsage(argv[0]);
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
        DapHandler handler(*transport, opts);

        while (handler.isRunning()) {
            json req = readDapPacket(*transport);
            if (req.is_null() || req.empty()) {
                break;
            }
            if (req.value("type", "") != "request") {
                continue;
            }
            handler.handleRequest(req);
        }

        return 0;
    }

    // ═══ Interactive mode (stdin/stdout) ═══
    std::cerr << "trust-dap: starting in interactive mode\n"
              << "  project-dir: " << (opts.projectDir.empty() ? "(cwd)" : opts.projectDir) << "\n";

    auto transport = std::make_unique<trust::transport::StdioTransport>();
    DapHandler handler(*transport, opts);

    while (handler.isRunning()) {
        json req = readDapPacket(*transport);
        if (req.is_null() || req.empty()) {
            break;
        }
        if (req.value("type", "") != "request") {
            continue;
        }
        handler.handleRequest(req);
    }

    std::cerr << "trust-dap: exiting\n";
    return 0;
}
