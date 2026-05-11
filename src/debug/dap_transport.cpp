#include "debug/dap_transport.h"

// ── DAP Protocol helpers ──

static int dapSeq = 0;

int nextDapSeq() {
    return ++dapSeq;
}

using json = nlohmann::json;

json readDapPacket(trust::transport::Transport& transport) {
    std::string body = transport.readPacket();
    if (body.empty()) {
        return json();
    }

    try {
        return json::parse(body);
    } catch (const json::parse_error& e) {
        std::cerr << "JSON parse error: " << e.what() << "\n"
                  << "Raw input: " << body << "\n";
        return json();
    }
}

void sendDapResponse(trust::transport::Transport& transport, int requestSeq, const json& body, bool success) {
    json resp = {{"type", "response"}, {"seq", nextDapSeq()}, {"request_seq", requestSeq}, {"command", body.value("command", "")}, {"success", success}};
    if (body.contains("body")) {
        resp["body"] = body["body"];
    }
    if (!success) {
        json msg = body.contains("message") ? body["message"] : json("request failed");
        resp["message"] = msg;
    }
    std::string payload = resp.dump();
    transport.send(payload);
}

void sendDapEvent(trust::transport::Transport& transport, const std::string& eventName, const json& body) {
    json evt = {{"type", "event"}, {"event", eventName}, {"seq", nextDapSeq()}};
    // Всегда добавляем body, даже пустой — DAP spec этого не запрещает,
    // а VSCode ожидает наличие поля
    evt["body"] = body;
    std::string payload = evt.dump();
    transport.send(payload);
}

void sendDapOutput(trust::transport::Transport& transport, const std::string& category, const std::string& output) {
    sendDapEvent(transport, "output", {{"category", category}, {"output", output}});
}

void sendBreakpointEvent(trust::transport::Transport& transport, const std::string& srcPath, int line, int bpId, bool verified) {
    sendDapEvent(
        transport, "breakpoint",
        {{"reason", verified ? "changed" : "new"}, {"breakpoint", {{"id", bpId}, {"verified", verified}, {"source", {{"path", srcPath}}}, {"line", line}}}});
}

// ── TCP server helpers (делегированы в trust::transport) ──

// ── CLI parsing ──

void printUsage(const char* prog) {
    std::cerr << "Usage: " << prog << " [options]\n"
              << "\n"
              << "DAP server for debugging Trust language programs.\n"
              << "By default runs in interactive mode (stdin/stdout).\n"
              << "\n"
              << "Options:\n"
              << "  --help                  Show this help\n"
              << "  server[=<port>]         TCP server mode on given port (default: " << DAP_DEFAULT_PORT << ")\n"
              << "  --project-dir <path>    Project working directory (default: cwd)\n"
              << "  --gdb <path>            Path to gdb binary (default: gdb)\n";
}

DapOptions parseDapOptions(int argc, const char* argv[]) {
    DapOptions opts;

    for (int i = 1; i < argc; ++i) {
        if (std::strcmp(argv[i], "--help") == 0) {
            opts.help = true;
            return opts;
        }

        // server[=port] — TCP server mode
        if (std::strncmp(argv[i], "server", 6) == 0) {
            const char* eq = std::strchr(argv[i], '=');
            if (eq != nullptr) {
                opts.port = std::stoi(eq + 1);
            } else {
                opts.port = DAP_DEFAULT_PORT;
            }
            continue;
        }

        auto nextArg = [&]() -> std::string {
            if (++i >= argc) {
                std::cerr << "Error: " << argv[i - 1] << " requires an argument\n";
                std::exit(1);
            }
            return argv[i];
        };

        if (std::strcmp(argv[i], "--project-dir") == 0) {
            opts.projectDir = nextArg();
        } else if (std::strcmp(argv[i], "--gdb") == 0) {
            opts.gdbPath = nextArg();
        } else {
            std::cerr << "Error: unknown option '" << argv[i] << "'\n";
            std::exit(1);
        }
    }

    return opts;
}