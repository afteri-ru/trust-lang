#include "lsp/lsp_protocol.h"
#include <iostream>
#include "utils/io.hpp"

// -- LSP Protocol helpers --

using json = nlohmann::json;

json readLspPacket(trust::transport::Transport& transport) {
    std::string body = transport.readPacket();
    if (body.empty()) {
        return json();
    }

    try {
        return json::parse(body);
    } catch (const json::parse_error& e) {
        trust::errs() << "LSP JSON parse error: " << e.what() << "\n"
                      << "Raw input: " << body << "\n";
        return json();
    }
}

void sendLspResponse(trust::transport::Transport& transport, const json& id, const json& result) {
    json resp = {{"jsonrpc", "2.0"}, {"id", id}, {"result", result}};
    std::string payload = resp.dump();
    transport.send(payload);
}

void sendLspError(trust::transport::Transport& transport, const json& id, int code, const std::string& message) {
    json resp = {{"jsonrpc", "2.0"}, {"id", id}, {"error", {{"code", code}, {"message", message}}}};
    std::string payload = resp.dump();
    transport.send(payload);
}

void sendLspNotification(trust::transport::Transport& transport, const std::string& method, const json& params) {
    json notif = {{"jsonrpc", "2.0"}, {"method", method}, {"params", params}};
    std::string payload = notif.dump();
    transport.send(payload);
}

void sendLspRequest(trust::transport::Transport& transport, const std::string& method, const json& params) {
    static int requestId = 0;
    json req = {{"jsonrpc", "2.0"}, {"id", ++requestId}, {"method", method}, {"params", params}};
    std::string payload = req.dump();
    transport.send(payload);
}

// -- TCP server helpers (делегированы в trust::transport) --

// -- CLI parsing --

void printLspUsage(const char* prog) {
    trust::errs() << "Usage: " << prog << " [options] [input]\n"
                  << "\n"
                  << "LSP server for Trust language source mapping.\n"
                  << "By default runs in interactive mode (stdin/stdout).\n"
                  << "\n"
                  << "Options:\n"
                  << "  --help                  Show this help\n"
                  << "  server[=<port>]         TCP server mode on given port (default: " << LSP_DEFAULT_PORT << ")\n"
                  << "  --project-dir <path>    Project working directory (default: cwd)\n"
                  << "  --temp-dir <path>       Directory for temporary transpiled .cpp files (default: none)\n"
                  << "  --emit-build-dir <dir>  With --json: also build a tar.gz of the build dir (Makefile,\n"
                  << "                          build.conf, .cppt, trust/) into <dir> without compiling (default: none)\n"
                  << "  --trace                 Enable LSP protocol tracing\n"
                  << "\n"
                  << "Playground output modes (Trust → C++ + source map):\n"
                  << "  --json [input]          Print {source,cpp,trustToCpp,cppToTrust} as JSON to stdout\n"
                  << "                          (input = .src file, or omitted/'-' to read stdin)\n"
                  << "  --html [input]          Print godbolt-style HTML (two Monaco editors + JS navigation)\n"
                  << "  --html-full             Wrap --html output into a complete HTML page\n"
                  << "  --monaco-url <url>      Monaco AMD 'vs' base URL for --html (default: jsdelivr CDN)\n"
                  << "  --server-url <url>      Live-run endpoint (POST Trust text → JSON) for --html\n"
                  << "  --examples-dir <dir>    Dir with *.src embedded into --html as example combobox\n";
}

LspOptions parseLspOptions(int argc, const char* argv[]) {
    LspOptions opts;

    for (int i = 1; i < argc; ++i) {
        if (std::strcmp(argv[i], "--help") == 0) {
            opts.help = true;
            return opts;
        }

        // server[=port] - TCP server mode
        if (std::strncmp(argv[i], "server", 6) == 0) {
            opts.mode = LspMode::Server;
            const char* eq = std::strchr(argv[i], '=');
            if (eq != nullptr) {
                opts.port = std::stoi(eq + 1);
            } else {
                opts.port = LSP_DEFAULT_PORT;
            }
            continue;
        }

        auto nextArg = [&]() -> std::string {
            if (++i >= argc) {
                trust::errs() << "Error: " << argv[i - 1] << " requires an argument\n";
                std::exit(1);
            }
            return argv[i];
        };

        if (std::strcmp(argv[i], "--json") == 0) {
            opts.mode = LspMode::Json;
            if (i + 1 < argc && argv[i + 1][0] != '-') {
                opts.inputFile = nextArg();
            }
        } else if (std::strcmp(argv[i], "--html") == 0) {
            opts.mode = LspMode::Html;
            if (i + 1 < argc && argv[i + 1][0] != '-') {
                opts.inputFile = nextArg();
            }
        } else if (std::strcmp(argv[i], "--html-full") == 0) {
            opts.htmlFull = true;
        } else if (std::strcmp(argv[i], "--monaco-url") == 0) {
            opts.monacoUrl = nextArg();
        } else if (std::strcmp(argv[i], "--server-url") == 0) {
            opts.serverUrl = nextArg();
        } else if (std::strcmp(argv[i], "--examples-dir") == 0) {
            opts.examplesDir = nextArg();
        } else if (std::strcmp(argv[i], "--project-dir") == 0) {
            opts.projectDir = nextArg();
        } else if (std::strcmp(argv[i], "--temp-dir") == 0) {
            opts.tempDir = nextArg();
        } else if (std::strcmp(argv[i], "--emit-build-dir") == 0) {
            opts.emitBuildDir = nextArg();
        } else if (std::strcmp(argv[i], "--trace") == 0) {
            opts.trace = true;
        } else if (std::strncmp(argv[i], "-W", 2) == 0) {
            // Опции диагностики (-W<name>=<status>) - пробрасываются в pipeline.
            opts.pipelineArgs.push_back(argv[i]);
        } else {
            trust::errs() << "Error: unknown option '" << argv[i] << "'\n";
            std::exit(1);
        }
    }

    return opts;
}