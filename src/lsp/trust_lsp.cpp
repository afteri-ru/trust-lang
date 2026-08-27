#include "lsp/trust_lsp.h"
#include "lsp/code_action.hpp"
#include "lsp/completion.h"
#include "lsp/formatting_service.hpp"

#include "trust/version.h"
#include "utils/io.hpp"

#include <exception>
#include <string>

using json = nlohmann::json;

// Типы/классы DocumentManager (CachedSource/CachedReader) и stateless-сервисы
// (navigation/hover/completion/codeaction/formatting) - в trust::lsp.
using namespace trust::lsp;

// -- Конструктор --
TrustLsp::TrustLsp(trust::transport::Transport& transport, const LspOptions& opts)
: transport_(transport)
, opts_(opts)
, analysis_(transport_, opts_, documents_) {
    // Wire DocumentManager to transpile/publish/log (делегирует AnalysisService).
    documents_.setCallbacks([this](const std::string& path, const std::string& code) { return analysis_.transpileSource(path, code); },
                            [this](const std::string& uri) { analysis_.publishDiagnostics(uri); }, [this](const std::string& msg) { log(msg); });
}

// -- Локальный stderr-лог --
// trace - только если включён флаг --trace
void TrustLsp::log(const std::string& msg) const {
    if (opts_.trace) {
        trust::errs() << "trust-lsp: " << msg << "\n";
    }
}

// -- Проверка jsonrpc: "2.0" --
static bool validateJsonRpc(const json& msg) {
    if (!msg.contains("jsonrpc")) {
        return false;
    }
    const auto& v = msg["jsonrpc"];
    return v.is_string() && v.get<std::string>() == "2.0";
}

// -- LSP Request handlers --
void TrustLsp::handleRequest(const json& req) {
    if (!validateJsonRpc(req)) {
        json id = req.value("id", json());
        sendLspError(transport_, id, -32600, "Invalid Request: missing or invalid jsonrpc field");
        return;
    }

    std::string method = req.value("method", "");
    json id = req.value("id", json());

    try {
        if (method == "initialize") {
            handleInitialize(req);
        } else if (method == "shutdown") {
            handleShutdown(req);
        } else if (method == "textDocument/definition") {
            navigation::handleDefinition(transport_, documents_, opts_, req);
        } else if (method == "textDocument/hover") {
            hover::handleHover(transport_, documents_, opts_, req);
        } else if (method == "textDocument/documentLink") {
            navigation::handleDocumentLink(transport_, documents_, opts_, req);
        } else if (method == "textDocument/completion") {
            completion::handleCompletion(transport_, documents_, opts_, req);
        } else if (method == "textDocument/codeAction") {
            codeaction::handleCodeAction(transport_, req);
        } else if (method == "textDocument/formatting") {
            formatting::handleFormatting(transport_, documents_, opts_, req);
        } else if (method == "workspace/executeCommand") {
            handleExecuteCommand(req);
        } else {
            sendLspError(transport_, id, -32601, "Method not found: " + method);
        }
    } catch (const std::exception& e) {
        std::string msg = std::string("UNEXPECTED ERROR in request '") + method + "': " + e.what();
        trust::errs() << "trust-lsp: " << msg << "\n";
        json logParams = {{"type", 1}, {"message", msg}}; // type=1 = Error
        sendLspNotification(transport_, "window/logMessage", logParams);
        sendLspError(transport_, id, -32803, "Internal error: " + std::string(e.what()));
    } catch (...) {
        std::string msg = std::string("UNEXPECTED ERROR in request '") + method + "': unknown exception";
        trust::errs() << "trust-lsp: " << msg << "\n";
        json logParams = {{"type", 1}, {"message", msg}};
        sendLspNotification(transport_, "window/logMessage", logParams);
        sendLspError(transport_, id, -32803, "Internal error: unknown exception");
    }
}

void TrustLsp::handleNotification(const json& req) {
    std::string method = req.value("method", "");

    try {
        if (method == "initialized") {
            // Ничего не делаем
        } else if (method == "textDocument/didOpen") {
            documents_.didOpen(req);
        } else if (method == "textDocument/didClose") {
            documents_.didClose(req);
        } else if (method == "textDocument/didChange") {
            documents_.didChange(req);
        } else if (method == "workspace/didChangeConfiguration") {
            handleDidChangeConfiguration(req);
        } else if (method == "exit") {
            running_ = false;
        }
        // Прочие нотификации игнорируем
    } catch (const std::exception& e) {
        std::string msg = std::string("UNEXPECTED ERROR in notification '") + method + "': " + e.what();
        trust::errs() << "trust-lsp: " << msg << "\n";
        json logParams = {{"type", 1}, {"message", msg}};
        sendLspNotification(transport_, "window/logMessage", logParams);
    } catch (...) {
        std::string msg = std::string("UNEXPECTED ERROR in notification '") + method + "': unknown exception";
        trust::errs() << "trust-lsp: " << msg << "\n";
        json logParams = {{"type", 1}, {"message", msg}};
        sendLspNotification(transport_, "window/logMessage", logParams);
    }
}

void TrustLsp::handleInitialize(const json& req) {
    json id = req.value("id", json());
    json capabilities = {{"textDocumentSync", 2}, // TextDocumentSyncKind::Incremental
                         {"definitionProvider", true},
                         {"hoverProvider", true},
                         {"documentLinkProvider", {{"resolveProvider", false}}},
                         {"completionProvider", {{"resolveProvider", false}, {"triggerCharacters", {"%", "$", ":", "@", "_", "."}}}},
                         {"codeActionProvider", {{"codeActionKinds", json::array({"quickfix"})}}},
                         {"documentFormattingProvider", true}};

    json result = {{"capabilities", capabilities}, {"serverInfo", {{"name", "trust-lsp"}, {"version", TRUST_VERSION}}}};
    sendLspResponse(transport_, id, result);
}

void TrustLsp::handleShutdown(const json& req) {
    json id = req.value("id", json());
    sendLspResponse(transport_, id, json());

    // Очищаем кеш для корректного re-initialize
    documents_.sourceCache().clear();
    documents_.cppToTrustCache().clear();
    documents_.openDocuments().clear();
    documents_.pendingTranspile().clear();
}

// -- Debounce-флаш: транспилируем документы, чей период тишины истёк --
void TrustLsp::flushPendingTranspile() {
    documents_.flushPendingTranspile();
}

// -- Обработка workspace/executeCommand (заглушка - не используется) --
void TrustLsp::handleExecuteCommand(const json& req) {
    json id = req.value("id", json());
    json params = req.value("params", json());
    std::string command = params.value("command", "");
    log("handleExecuteCommand: " + command + " (stub - not implemented)");
    sendLspError(transport_, id, -32601, "Command not implemented: " + command);
}

// -- Обработка workspace/didChangeConfiguration --
void TrustLsp::handleDidChangeConfiguration(const json& req) {
    json params = req.value("params", json());
    json settings = params.value("settings", json());
    log("didChangeConfiguration");

    // Извлекаем trust.tempDir из настроек
    json trustSettings = settings.value("trust", json());
    if (trustSettings.contains("tempDir") && trustSettings["tempDir"].is_string()) {
        opts_.tempDir = trustSettings["tempDir"].get<std::string>();
        log("  tempDir updated to: " + opts_.tempDir);
    }
    // trust.shebangMode - режим применения опций анализа из шебанга файла.
    if (trustSettings.contains("shebangMode") && trustSettings["shebangMode"].is_string()) {
        const std::string val = trustSettings["shebangMode"].get<std::string>();
        if (auto m = ::shebangModeFromName(val)) {
            opts_.shebangMode = *m;
            log("  shebangMode updated to: " + std::string(::shebangModeName(*m)));
        } else {
            log("  warning: invalid shebangMode value '" + val + "' (ignored)");
        }
    }
}
