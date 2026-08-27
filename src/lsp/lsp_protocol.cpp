#include "lsp/lsp_protocol.h"
#include "pipeline/cli.hpp"
#include <iostream>
#include <vector>
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

// -- CLI parsing -- (единый арity-aware парсер драйвера, см. pipeline/cli.hpp)

namespace {

enum class LspOptId { Help, Trace, Json, Html, HtmlFull, MonacoUrl, ServerUrl, ExamplesDir, ProjectDir, TempDir, EmitBuildDir, ShebangMode };

std::vector<trust::DriverOption> lspTable() {
    using namespace trust;
    std::vector<DriverOption> out = {
        {int(LspOptId::Help), "help", "h", CliOpt::Flag, "", "Show this help message", CliCategory::General},
        {int(LspOptId::Trace), "trace", "", CliOpt::Flag, "", "Trace LSP protocol traffic", CliCategory::General},
        {int(LspOptId::Json), "json", "", CliOpt::Flag, "", "Transpile Trust to C++ + source map as JSON to stdout (input file optional)",
         CliCategory::CompileModel},
        {int(LspOptId::Html), "html", "", CliOpt::Flag, "", "Emit godbolt-style HTML fragment (input file optional)", CliCategory::CompileModel},
        {int(LspOptId::HtmlFull), "html-full", "", CliOpt::Flag, "", "Wrap --html output in a complete HTML page", CliCategory::CompileModel},
        {int(LspOptId::MonacoUrl), "monaco-url", "", CliOpt::Value, "url", "Monaco AMD 'vs' base URL for --html", CliCategory::InputOutput},
        {int(LspOptId::ServerUrl), "server-url", "", CliOpt::Value, "url", "Live-run endpoint for --html", CliCategory::InputOutput},
        {int(LspOptId::ExamplesDir), "examples-dir", "", CliOpt::Value, "dir", "Directory with *.src examples for --html", CliCategory::InputOutput},
        {int(LspOptId::ProjectDir), "project-dir", "", CliOpt::Value, "dir", "Project working directory", CliCategory::InputOutput},
        {int(LspOptId::TempDir), "temp-dir", "", CliOpt::Value, "dir", "Temporary directory for transpiled .cpp files", CliCategory::InputOutput},
        {int(LspOptId::EmitBuildDir), "emit-build-dir", "", CliOpt::Value, "dir", "Also build a tar.gz of the build dir (without compiling)",
         CliCategory::ProjectSpecific},
        {int(LspOptId::ShebangMode), "shebang-mode", "", CliOpt::Value, "mode",
         "How to apply analysis options from the file shebang (#!...) relative to environment options: "
         "ignore | shebang-only | env-after-shebang | env-before-shebang (default: env-after-shebang)",
         CliCategory::ProjectSpecific},
    };
    // ОБЩИЕ опции анализа (--solver-mode, --keywords, -fsolver-loop-unroll, -W...) НЕ объявляются
    // в lspTable: они разбираются ОБЩИМ методом (applyAnalysisArgs, таблица trust). Сюда их
    // принимаем через analysis_passthrough в parseDriverArgs (см. parseLspOptions ниже) и
    // отдаём в applyAnalysisArgs как есть - определения не дублируются, набор может быть любым.
    return out;
}

} // namespace

void printLspUsage(const char* prog) {
    trust::errs() << "Usage: " << prog << " [options] [input] [server[=<port>]]\n\n"
                  << "LSP server for Trust language source mapping.\n"
                  << "By default runs in interactive mode (stdin/stdout).\n\n";
    trust::errs() << trust::driverHelp(lspTable());
    trust::errs() << "  server[=<port>]    TCP server mode on given port (default: " << LSP_DEFAULT_PORT << ")\n";
}

LspOptions parseLspOptions(int argc, const char* argv[]) {
    LspOptions opts;
    std::vector<std::string> args;
    args.reserve(static_cast<std::size_t>(argc));
    for (int i = 0; i < argc; ++i) {
        args.emplace_back(argv[i]);
    }

    // server[=<port>] - подкоманда (не опция), выносим из аргументов (общий helper cli.hpp).
    int server_port = 0;
    if (trust::extractServerCommand(args, server_port, LSP_DEFAULT_PORT)) {
        opts.mode = LspMode::Server;
        opts.port = server_port;
    }

    const auto table = lspTable();
    std::string input_file;
    bool diag_help = false; // у trust-lsp нет `-Whelp` (playground/-W применяются позже через applyDiagnostics)
    auto apply = [&](int id, const std::string& v) -> bool {
        switch (static_cast<LspOptId>(id)) {
        case LspOptId::Help:
            opts.help = true;
            break;
        case LspOptId::Trace:
            opts.trace = true;
            break;
        case LspOptId::Json:
            opts.mode = LspMode::Json;
            break;
        case LspOptId::Html:
            opts.mode = LspMode::Html;
            break;
        case LspOptId::HtmlFull:
            opts.htmlFull = true;
            break;
        case LspOptId::MonacoUrl:
            opts.monacoUrl = v;
            break;
        case LspOptId::ServerUrl:
            opts.serverUrl = v;
            break;
        case LspOptId::ExamplesDir:
            opts.examplesDir = v;
            break;
        case LspOptId::ProjectDir:
            opts.projectDir = v;
            break;
        case LspOptId::TempDir:
            opts.tempDir = v;
            break;
        case LspOptId::EmitBuildDir:
            opts.emitBuildDir = v;
            break;
        case LspOptId::ShebangMode:
            if (auto m = ::shebangModeFromName(v)) {
                opts.shebangMode = *m;
            } else {
                // Невалидное значение -> ошибка (no silent fallback).
                return false;
            }
            break;
        }
        return true;
    };

    // analysis_passthrough = &opts.pipelineArgs: ОБЩИЕ опции анализа (--solver-mode=,
    // -fsolver-loop-unroll, произвольные --name=value/-f...) не объявляются в lspTable, а
    // собираются в pipelineArgs и разбираются центрально applyAnalysisArgs (таблица trust).
    const std::string err = trust::parseDriverArgs(args, table, apply, opts.pipelineArgs, input_file, diag_help, &opts.pipelineArgs);
    if (!err.empty()) {
        trust::errs() << "error: " << err << "\n";
        std::exit(1);
    }
    if (!input_file.empty()) {
        opts.inputFile = input_file;
    }
    return opts;
}
