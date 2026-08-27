#include "debug/dap_transport.h"
#include "pipeline/cli.hpp"
#include "utils/io.hpp"

#include <vector>

// -- DAP Protocol helpers --

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
        trust::errs() << "JSON parse error: " << e.what() << "\n"
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
    // Всегда добавляем body, даже пустой - DAP spec этого не запрещает,
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

// -- TCP server helpers (делегированы в trust::transport) --

// -- CLI parsing -- (единый арity-aware парсер драйвера, см. pipeline/cli.hpp)

namespace {

enum class DapOptId { Help, ProjectDir, Gdb };

std::vector<trust::DriverOption> dapTable() {
    using namespace trust;
    return {
        {int(DapOptId::Help), "help", "h", CliOpt::Flag, "", "Show this help message", CliCategory::General},
        {int(DapOptId::ProjectDir), "project-dir", "", CliOpt::Value, "dir", "Project working directory", CliCategory::InputOutput},
        {int(DapOptId::Gdb), "gdb", "", CliOpt::Value, "path", "Path to gdb binary (default: gdb)", CliCategory::Toolchain},
    };
}

} // namespace

void printUsage(const char* prog) {
    trust::errs() << "Usage: " << prog << " [options] [server[=<port>]]\n\n"
                  << "DAP server for debugging Trust language programs.\n"
                  << "By default runs in interactive mode (stdin/stdout).\n\n";
    trust::errs() << trust::driverHelp(dapTable());
    trust::errs() << "  server[=<port>]   TCP server mode on given port (default: " << DAP_DEFAULT_PORT << ")\n";
}

DapOptions parseDapOptions(int argc, const char* argv[]) {
    DapOptions opts;
    std::vector<std::string> args;
    args.reserve(static_cast<std::size_t>(argc));
    for (int i = 0; i < argc; ++i) {
        args.emplace_back(argv[i]);
    }

    // server[=<port>] - подкоманда (не опция), выносим из аргументов (общий helper cli.hpp).
    int server_port = 0;
    if (trust::extractServerCommand(args, server_port, DAP_DEFAULT_PORT)) {
        opts.port = server_port;
    }

    const auto table = dapTable();
    std::vector<std::string> diag; // у dap нет -W-диагностик
    std::string input_file;        // у dap нет позиционного входа
    bool diag_help = false;        // у dap нет `-Whelp`
    auto apply = [&](int id, const std::string& v) -> bool {
        switch (static_cast<DapOptId>(id)) {
        case DapOptId::Help:
            opts.help = true;
            break;
        case DapOptId::ProjectDir:
            opts.projectDir = v;
            break;
        case DapOptId::Gdb:
            opts.gdbPath = v;
            break;
        }
        return true;
    };

    const std::string err = trust::parseDriverArgs(args, table, apply, diag, input_file, diag_help);
    if (!err.empty()) {
        trust::errs() << "error: " << err << "\n";
        std::exit(1);
    }
    return opts;
}
