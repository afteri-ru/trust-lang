#include "lsp/trust_lsp.h"
#include "trust/version.h"
#include "utils/utils.hpp"
#include <algorithm>
#include <fstream>
#include <iostream>
#include <sstream>
#include <string>
#include <utility>

using json = nlohmann::json;

// ── Локальный stderr-лог (если trace включён) ──
void TrustLsp::log(const std::string &msg) const {
    if (opts_.trace) {
        std::cerr << "trust-lsp: " << msg << "\n";
    }
}

// ── Преобразование file:// URI в путь с URL-decoding ──
static std::string uriToFilePath(const std::string &uri) {
    if (uri.rfind("file://", 0) != 0) {
        return uri;
    }
    std::string path = uri.substr(7); // срезаем "file://"

    // URL-decoding: %XX → символ
    std::string decoded;
    for (size_t i = 0; i < path.size(); ++i) {
        if (path[i] == '%' && i + 2 < path.size()) {
            char hex[3] = {path[i + 1], path[i + 2], '\0'};
            char *end = nullptr;
            long val = std::strtol(hex, &end, 16);
            if (end == hex + 2) {
                decoded += static_cast<char>(val);
                i += 2;
                continue;
            }
        }
        decoded += path[i];
    }
    return decoded;
}

// ── Нормализация пути для поиска в source-map ──
static std::string normalizeForSourceMap(const std::string &filePath, const std::string &projectDir) {
    namespace fs = std::filesystem;
    fs::path p(filePath);

    if (p.is_relative() || projectDir.empty()) {
        return p.lexically_normal().string();
    }

    auto absPath = fs::absolute(p).lexically_normal().string();
    auto prefix = fs::path(projectDir).lexically_normal().string() + "/";

    if (absPath.find(prefix) == 0) {
        return absPath.substr(prefix.size());
    }

    return absPath;
}

// ── Проверка jsonrpc: "2.0" ──
static bool validateJsonRpc(const json &msg) {
    if (!msg.contains("jsonrpc")) {
        return false;
    }
    const auto &v = msg["jsonrpc"];
    return v.is_string() && v.get<std::string>() == "2.0";
}

// ── Конструктор ──

TrustLsp::TrustLsp(LspTransport &transport, const LspOptions &opts)
    : transport_(transport), opts_(opts) {}

// ── Загрузка source-map (из CachedSource) ──

bool TrustLsp::loadSourceMap(const std::string &trustFilePath) {
    auto it = sourceCache_.find(trustFilePath);
    if (it == sourceCache_.end()) {
        log("no cached source for: " + trustFilePath);
        return false;
    }

    currentTrustFile_ = trustFilePath;
    source_ = it->second.sourceMap.get();
    log("loaded cached source map for: " + trustFilePath);
    return true;
}

// ── LSP Request handlers ──

void TrustLsp::handleRequest(const json &req) {
    if (!validateJsonRpc(req)) {
        json id = req.value("id", json());
        sendLspError(transport_, id, -32600, "Invalid Request: missing or invalid jsonrpc field");
        return;
    }

    std::string method = req.value("method", "");
    json id = req.value("id", json());

    if (method == "initialize") {
        handleInitialize(req);
    } else if (method == "shutdown") {
        handleShutdown(req);
    } else if (method == "textDocument/definition") {
        handleDefinition(req);
    } else if (method == "textDocument/hover") {
        handleHover(req);
    } else if (method == "textDocument/documentLink") {
        handleDocumentLink(req);
    } else {
        sendLspError(transport_, id, -32601, "Method not found: " + method);
    }
}

void TrustLsp::handleNotification(const json &req) {
    std::string method = req.value("method", "");

    if (method == "initialized") {
        // Ничего не делаем
    } else if (method == "textDocument/didOpen") {
        handleDidOpen(req);
    } else if (method == "textDocument/didClose") {
        handleDidClose(req);
    } else if (method == "textDocument/didChange") {
        handleDidChange(req);
    } else if (method == "exit") {
        running_ = false;
    }
    // Прочие нотификации игнорируем
}

void TrustLsp::handleInitialize(const json &req) {
    json id = req.value("id", json());
    json capabilities = {
        {"textDocumentSync", 1},  // TextDocumentSyncKind::Full
        {"definitionProvider", true},
        {"hoverProvider", true},
        {"documentLinkProvider", {{"resolveProvider", false}}}
    };

    json result = {
        {"capabilities", capabilities},
        {"serverInfo", {
            {"name", "trust-lsp"},
            {"version", TRUST_VERSION}
        }}
    };
    sendLspResponse(transport_, id, result);
}

void TrustLsp::handleShutdown(const json &req) {
    json id = req.value("id", json());
    sendLspResponse(transport_, id, json());

    // Очищаем кеш для корректного re-initialize
    sourceCache_.clear();
    currentTrustFile_.clear();
    source_ = nullptr;
}

void TrustLsp::handleDidOpen(const json &req) {
    json params = req.value("params", json());
    std::string uri = params.value("textDocument", json()).value("uri", "");

    // Преобразуем URI в файловый путь (с URL-decoding)
    std::string filePath = uriToFilePath(uri);

    log("didOpen: " + filePath);

    // In-process транспиляция Trust → C++ + source map
    std::string transpileErr = transpileSourceFile(filePath);

    if (transpileErr.empty()) {
        loadSourceMap(filePath);
    } else {
        // Диагностика при ошибке транспиляции
        publishDiagnostics(uri, {{transpileErr, ""}});
    }

    log("didOpen completed for " + filePath);
}

void TrustLsp::handleDidClose(const json &req) {
    json params = req.value("params", json());
    std::string uri = params.value("textDocument", json()).value("uri", "");

    std::string filePath = uriToFilePath(uri);

    sourceCache_.erase(filePath);
    if (currentTrustFile_ == filePath) {
        currentTrustFile_.clear();
        source_ = nullptr;
    }
}

void TrustLsp::handleDidChange(const json &req) {
    json params = req.value("params", json());
    std::string uri = params.value("textDocument", json()).value("uri", "");

    // Преобразуем URI в файловый путь (с URL-decoding)
    std::string filePath = uriToFilePath(uri);

    // Перетранспилируем при изменении файла
    std::string transpileErr = transpileSourceFile(filePath);

    if (transpileErr.empty()) {
        loadSourceMap(filePath);
    } else {
        // Не удаляем существующий кеш при ошибке, чтобы не сломать hover/definition
        log("re-transpilation failed for " + filePath + ": " + transpileErr + ", using previous cache");
        publishDiagnostics(uri, {{transpileErr, ""}});
    }
}

void TrustLsp::handleDefinition(const json &req) {
    json id = req.value("id", json());
    json params = req.value("params", json());

    std::string uri = params.value("textDocument", json()).value("uri", "");
    std::string filePath = uriToFilePath(uri);

    trust::LineNumber trustLine = params.value("position", json()).value("line", 0) + 1; // 1-based

    // Ищем source в кеше
    const trust::TrustSource *ts = nullptr;
    auto it = sourceCache_.find(filePath);
    if (it != sourceCache_.end()) {
        ts = it->second.sourceMap.get();
    }

    if (!ts) {
        sendLspError(transport_, id, -32000, "No source map loaded for file: " + filePath);
        return;
    }

    auto result = ts->nearestTrustToCpp(normalizeForSourceMap(filePath, opts_.projectDir), trustLine);
    if (!result) {
        sendLspResponse(transport_, id, json());
        return;
    }

    const std::string &cppFile = result->first;
    trust::LineNumber cppLine = result->second;

    // Формируем Location (всегда массив для соответствия LSP spec)
    json location = {
        {"uri", "file://" + cppFile},
        {"range", {
            {"start", {{"line", static_cast<int>(cppLine) - 1}, {"character", 0}}},
            {"end", {{"line", static_cast<int>(cppLine) - 1}, {"character", 0}}}
        }}
    };

    sendLspResponse(transport_, id, json::array({location}));
}

void TrustLsp::handleHover(const json &req) {
    json id = req.value("id", json());
    json params = req.value("params", json());

    std::string uri = params.value("textDocument", json()).value("uri", "");
    std::string filePath = uriToFilePath(uri);

    trust::LineNumber trustLine = params.value("position", json()).value("line", 0) + 1;

    const trust::TrustSource *ts = nullptr;
    const std::vector<std::string> *cppLines = nullptr;
    auto it = sourceCache_.find(filePath);
    if (it != sourceCache_.end()) {
        ts = it->second.sourceMap.get();
        cppLines = &it->second.cppLines;
    }

    if (!ts || !cppLines) {
        sendLspError(transport_, id, -32000, "No source map loaded for file: " + filePath);
        return;
    }

    auto result = ts->nearestTrustToCpp(normalizeForSourceMap(filePath, opts_.projectDir), trustLine);
    if (!result) {
        sendLspResponse(transport_, id, json());
        return;
    }

    trust::LineNumber cppLine = result->second;

    // Читаем C++ строку из in-memory кеша
    std::string cppLineContent;
    if (cppLine > 0 && static_cast<size_t>(cppLine) <= cppLines->size()) {
        cppLineContent = (*cppLines)[static_cast<size_t>(cppLine) - 1];
    }
    if (cppLineContent.empty()) {
        cppLineContent = "(empty line)";
    }

    std::string hoverText = "**→ C++:** `line " + std::to_string(cppLine) + "`\n```cpp\n" + cppLineContent + "\n```";

    json hoverResult = {
        {"contents", {
            {"kind", "markdown"},
            {"value", hoverText}
        }},
        {"range", {
            {"start", {{"line", static_cast<int>(trustLine) - 1}, {"character", 0}}},
            {"end", {{"line", static_cast<int>(trustLine) - 1}, {"character", 0}}}
        }}
    };

    sendLspResponse(transport_, id, hoverResult);
}

void TrustLsp::handleDocumentLink(const json &req) {
    json id = req.value("id", json());
    json params = req.value("params", json());

    std::string uri = params.value("textDocument", json()).value("uri", "");
    std::string filePath = uriToFilePath(uri);

    const trust::TrustSource *ts = nullptr;
    auto it = sourceCache_.find(filePath);
    if (it != sourceCache_.end()) {
        ts = it->second.sourceMap.get();
    }

    if (!ts) {
        sendLspResponse(transport_, id, json::array());
        return;
    }

    std::string searchPath = normalizeForSourceMap(filePath, opts_.projectDir);
    json links = json::array();
    for (const auto &entry : ts->entries()) {
        if (entry.files.first != searchPath) continue;

        for (const auto &[trustLine, cppLine] : entry.trustToCppIndex) {
            json link = {
                {"range", {
                    {"start", {{"line", static_cast<int>(trustLine) - 1}, {"character", 0}}},
                    {"end", {{"line", static_cast<int>(trustLine) - 1}, {"character", 0}}}
                }},
                {"target", "file://" + entry.files.second + "#" + std::to_string(cppLine)}
            };
            links.push_back(std::move(link));
        }
    }

    sendLspResponse(transport_, id, links);
}

// ── In-process транспиляция .trust → C++ + source map ──

std::string TrustLsp::transpileSourceFile(const std::string &trustFilePath) {
    log("transpiling (in-process): " + trustFilePath);

    // Читаем содержимое файла
    std::ifstream file(trustFilePath);
    if (!file.is_open()) {
        std::string err = "cannot open file: " + trustFilePath;
        log(err);
        return err;
    }
    std::stringstream buffer;
    buffer << file.rdbuf();
    std::string trustCode = buffer.str();

    // Транспилируем
    auto tr = trust::lsp::transpileTrustSource(trustCode, trustFilePath, opts_.projectDir);

    // Сохраняем в кеш (даже если есть ошибки — частичный результат может быть полезен)
    CachedSource cs;
    cs.sourceMap = std::move(tr.sourceMap);
    cs.cppLines = std::move(tr.cppLines);
    sourceCache_[trustFilePath] = std::move(cs);

    if (!tr.errors.empty()) {
        std::string errMsg = "transpilation completed with " + std::to_string(tr.errors.size()) + " error(s): ";
        for (size_t i = 0; i < tr.errors.size(); ++i) {
            if (i > 0) errMsg += "; ";
            errMsg += tr.errors[i];
        }
        log(errMsg);
        return errMsg;
    }

    log("transpilation completed successfully");
    return {};
}

// ── Отправка диагностики ──
void TrustLsp::publishDiagnostics(const std::string &uri, const std::vector<std::pair<std::string, std::string>> &errors) {
    json diagnostics = json::array();
    for (const auto &[message, fileHint] : errors) {
        json diag = {
            {"range", {
                {"start", {{"line", 0}, {"character", 0}}},
                {"end", {{"line", 0}, {"character", 1}}}
            }},
            {"severity", 1}, // Error
            {"source", "trust-lsp"},
            {"message", message + (fileHint.empty() ? "" : " (" + fileHint + ")")}
        };
        diagnostics.push_back(std::move(diag));
    }

    json params = {
        {"uri", uri},
        {"diagnostics", diagnostics}
    };
    sendLspNotification(transport_, "textDocument/publishDiagnostics", params);
    log("published " + std::to_string(errors.size()) + " diagnostic(s) for " + uri);
}