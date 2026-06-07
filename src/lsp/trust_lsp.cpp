#include "lsp/trust_lsp.h"
#include "trust/version.h"
#include "utils/file_io.hpp"
#include "utils/uri.hpp"
#include "utils/utils.hpp"
#include "utils/io.hpp"

#include "pipeline/pipeline.hpp"
#include "semantic/analyzer.hpp"
#include "transpiler/transpiler.hpp"
#include "utils/io.hpp"

#include <algorithm>
#include <filesystem>
#include <iostream>
#include <memory>
#include <sstream>
#include <string>
#include <utility>
#include "utils/io.hpp"

using json = nlohmann::json;
using trust::utils::resolvePath;
using trust::utils::uriToFilePath;

// ── Конструктор ──
TrustLsp::TrustLsp(trust::transport::Transport& transport, const LspOptions& opts)
: transport_(transport)
, opts_(opts) {
}

// ── Локальный stderr-лог ──
// trace — только если включён флаг --trace
void TrustLsp::log(const std::string& msg) const {
    if (opts_.trace) {
        trust::errs() << "trust-lsp: " << msg << "\n";
    }
}

// ── Проверка jsonrpc: "2.0" ──
static bool validateJsonRpc(const json& msg) {
    if (!msg.contains("jsonrpc")) {
        return false;
    }
    const auto& v = msg["jsonrpc"];
    return v.is_string() && v.get<std::string>() == "2.0";
}

// ── URL-encoding для пути (только спецсимволы) ──
static std::string uriEncodePath(const std::string& path) {
    std::string result;
    result.reserve(path.size());
    for (unsigned char c : path) {
        if (c <= 32 || c >= 127 || c == '#' || c == '%' || c == '?' || c == '[' || c == ']' || c == ' ' || c == '"' || c == '<' || c == '>' || c == '{' ||
            c == '}' || c == '|' || c == '\\' || c == '^' || c == '`') {
            // Кодируем проблемные символы в %XX
            static const char hex[] = "0123456789ABCDEF";
            result += '%';
            result += hex[c >> 4];
            result += hex[c & 0xF];
        } else {
            result += c;
        }
    }
    return result;
}

// ── Вспомогательная: filePath → "file://" URI ──
static std::string filePathToUri(const std::string& path) {
    if (path.rfind("file://", 0) == 0)
        return path;
    return "file://" + uriEncodePath(std::filesystem::absolute(path).string());
}

// ── Вспомогательная: Location → LSP Position (0-based) ──
static json locationToLspPosition(const trust::SourceMapReader& reader, trust::SourceMapReader::Location loc) {
    auto lc = reader.line_column(loc);
    return {{"line", static_cast<int>(lc.line) - 1}, {"character", static_cast<int>(lc.column) - 1}};
}

// ── Вспомогательная: Range → LSP Range ──
static json rangeToLspRange(const trust::SourceMapReader& reader, trust::SourceMapReader::Range range) {
    return {{"start", locationToLspPosition(reader, range.begin)}, {"end", locationToLspPosition(reader, range.end)}};
}

// ── Построение file:// URI с фрагментом из SourceMapReader::rangeToFragmentString ──
static std::string makeFragmentUri(const trust::SourceMapReader& reader, const std::string& basePath, trust::SourceMapReader::Range range) {
    std::string uri = filePathToUri(basePath);
    return uri + "#" + reader.rangeToFragmentString(range);
}

// ── Вспомогательная: форматирование Range для трассировки ──
// Формат: "path:line:col–line:col [текст]". Текст берётся из source-map (getText).
// При невалидном range или невозможности получить текст — безопасный fallback.
static std::string formatRange(const trust::SourceMapReader& reader, trust::SourceMapReader::Range range, const std::string& filePath) {
    if (range.isInvalid())
        return filePath + ":<invalid>";
    auto b = reader.line_column(range.begin);
    auto e = reader.line_column(range.end);
    std::string text;
    try {
        text = std::string(reader.getText(range));
    } catch (...) {
        text = "<no text>";
    }
    return std::format("{}:{}:{}-{}:{} [{}]", filePath, b.line, b.column, e.line, e.column, text);
}

// ── Вспомогательная: проверка, является ли файл C++ (не trust-исходником) ──
// Для C++ файла, не найденного в reverse-кеше, не нужно ничего делать.
// Перемещено в SourceMapReader::isCppFileExt

// ── Вспомогательная функция для получения Reader и FileIdx из кеша ──
// trustFilePath берётся из sourceCache_ (по ключу). Для cpp-запросов
// trustFilePath находится через reverse-кеш cppToTrustCache_.
TrustLsp::CachedReader TrustLsp::getCachedReader(const std::string& filePath, std::string& outError) {
    // Определяем, является ли запрос для C++ файла (через reverse-кеш)
    bool isCpp = false;
    std::string trustFilePath;

    auto cppIt = cppToTrustCache_.find(filePath);
    if (cppIt != cppToTrustCache_.end()) {
        trustFilePath = cppIt->second;
        isCpp = true;
        log("  getCachedReader: reverse-cache HIT  cppPath=" + filePath + " -> trustPath=" + trustFilePath);
    } else if (trust::SourceMapReader::isCppFileExt(filePath)) {
        // C++ файл не в reverse-кеше — это не транспилированный файл, игнорируем
        log("  getCachedReader: cpp file NOT in reverse-cache (miss): " + filePath);
        outError = "";
        return {nullptr, trust::ReaderFile{0}, trust::ReaderFile{0}, false};
    } else {
        trustFilePath = filePath;
        log("  getCachedReader: trust file request: " + filePath);
    }

    // Фиктивный (in-memory) источник: файла на диске нет, искать/открывать его
    // не нужно — сообщаем об этом явно вместо «file not found».
    if (trust::SourceMapReader::isInMemoryName(trustFilePath)) {
        outError = "in-memory source (no file on disk): " + trustFilePath;
        return {nullptr, trust::ReaderFile{0}, trust::ReaderFile{0}, false};
    }

    // Если документ «грязный» (debounce ещё не истёк) — синхронно пере-транспилируем его,
    // чтобы hover/documentLink/definition всегда отражали текущий буфер.
    if (pendingTranspile_.find(trustFilePath) != pendingTranspile_.end()) {
        flushDocument(trustFilePath);
    }

    auto it = sourceCache_.find(trustFilePath);
    if (it == sourceCache_.end()) {
        // Auto-recovery: если кэша нет, пытаемся транспилировать на лету.
        // При наличии буфера документа (didOpen/didChange) — транспилируем его,
        // иначе читаем файл с диска.
        if (!isCpp && !trust::SourceMapReader::isCppFileExt(trustFilePath) && trustFilePath.find(".src") != std::string::npos) {
            std::string transpileErr;
            auto docIt = openDocuments_.find(trustFilePath);
            if (docIt != openDocuments_.end()) {
                transpileErr = transpileSource(trustFilePath, docIt->second);
            } else {
                transpileErr = transpileSourceFile(trustFilePath);
            }
            if (!transpileErr.empty()) {
                log("  auto-transpile on cache miss failed: " + transpileErr);
            }
            // Повторная попытка найти в кэше
            it = sourceCache_.find(trustFilePath);
        }
    }
    if (it == sourceCache_.end()) {
        outError = "File not cached: " + filePath;
        return {nullptr, trust::ReaderFile{0}, trust::ReaderFile{0}, false};
    }

    const trust::SourceMapReader* reader = it->second.sourceMap->source().toReader();
    if (!reader) {
        outError = "Failed to get SourceMapReader";
        return {nullptr, trust::ReaderFile{0}, trust::ReaderFile{0}, false};
    }

    CachedReader result;
    result.reader = reader;
    result.trustReaderIdx = it->second.trustReaderIdx;
    result.cppReaderIdx = it->second.cppReaderIdx;
    result.isCppRequest = isCpp;
    log("  getCachedReader: OK trustReaderIdx=" + std::to_string(it->second.trustReaderIdx.as_index()) +
        " cppReaderIdx=" + std::to_string(it->second.cppReaderIdx.as_index()) + " isCpp=" + (isCpp ? "1" : "0"));
    return result;
}

// ── LSP Request handlers ──

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
            handleDefinition(req);
        } else if (method == "textDocument/hover") {
            handleHover(req);
        } else if (method == "textDocument/documentLink") {
            handleDocumentLink(req);
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
            handleDidOpen(req);
        } else if (method == "textDocument/didClose") {
            handleDidClose(req);
        } else if (method == "textDocument/didChange") {
            handleDidChange(req);
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
                         {"documentLinkProvider", {{"resolveProvider", false}}}};

    json result = {{"capabilities", capabilities}, {"serverInfo", {{"name", "trust-lsp"}, {"version", TRUST_VERSION}}}};
    sendLspResponse(transport_, id, result);
}

void TrustLsp::handleShutdown(const json& req) {
    json id = req.value("id", json());
    sendLspResponse(transport_, id, json());

    // Очищаем кеш для корректного re-initialize
    sourceCache_.clear();
    cppToTrustCache_.clear();
    openDocuments_.clear();
    pendingTranspile_.clear();
}

void TrustLsp::handleDidOpen(const json& req) {
    json params = req.value("params", json());
    std::string uri = params.value("textDocument", json()).value("uri", "");

    // Преобразуем URI в файловый путь (с URL-decoding)
    std::string filePath = uriToFilePath(uri);

    log("didOpen: " + filePath);

    // Если это не Trust-файл — игнорируем
    if (!trust::SourceMapReader::isTrustFileExt(filePath)) {
        log("  skipped (not a trust source file)");
        return;
    }

    // Если это C++ файл из reverse-кеша — не транспилируем, просто логируем
    if (cppToTrustCache_.find(filePath) != cppToTrustCache_.end()) {
        log("  cpp file already cached via reverse cache");
        return;
    }

    // Новое открытие — сбрасываем отложенную пере-транспиляцию
    pendingTranspile_.erase(filePath);

    // ── Транспилируем содержимое буфера из didOpen, а не файла на диске ──
    // Это гарантирует, что hover/documentLink/definition сразу учитывают правки
    // в редакторе, даже если файл ещё не сохранён.
    std::string text = params.value("textDocument", json()).value("text", "");
    if (text.empty()) {
        // Нет текста буфера (fallback / переоткрытие) — читаем с диска
        log("  no buffer text in didOpen, reading from disk");
        std::string transpileErr = transpileSourceFile(filePath);
        if (!transpileErr.empty()) {
            publishDiagnostics(uri);
        }
    } else {
        openDocuments_[filePath] = text;
        std::string transpileErr = transpileSource(filePath, text);
        if (!transpileErr.empty()) {
            publishDiagnostics(uri);
        }
    }

    log("didOpen completed for " + filePath);
}

void TrustLsp::handleDidClose(const json& req) {
    json params = req.value("params", json());
    std::string uri = params.value("textDocument", json()).value("uri", "");

    std::string filePath = uriToFilePath(uri);

    // Не удаляем sourceCache_ или cppToTrustCache_ при didClose — это ломало
    // hover/definition/documentLink при переключении вкладок и для C++ файлов.
    // Кэш чистится только при didOpen (если хеш изменился) или при shutdown.
    // C++ файл физически существует на диске, пока сервер не перезапущен.
    log("didClose ignored (cache preserved)");
}

void TrustLsp::handleDidChange(const json& req) {
    json params = req.value("params", json());
    std::string uri = params.value("textDocument", json()).value("uri", "");

    // Преобразуем URI в файловый путь (с URL-decoding)
    std::string filePath = uriToFilePath(uri);

    // Если это не Trust-файл — игнорируем
    if (!trust::SourceMapReader::isTrustFileExt(filePath)) {
        log("  skipped (not a trust source file)");
        return;
    }

    // Обновляем буфер из contentChanges (инкрементальная синхронизация,
    // textDocumentSync=2). Каждый элемент — либо {text: полный текст},
    // либо {range, text} — инкрементальная правка.
    std::string newText = applyContentChanges(filePath, params.value("contentChanges", json::array()));
    openDocuments_[filePath] = newText;

    // Debounce: не транспилируем на каждую правку, а помечаем документ «грязным».
    // Фактическая пере-транспиляция произойдёт через kDebounceMs (flushPendingTranspile)
    // или синхронно при запросе hover/documentLink/definition (getCachedReader).
    pendingTranspile_[filePath] = std::chrono::steady_clock::now();
    log("didChange: buffer updated for " + filePath + " (transpile deferred)");
}

// ── Применение contentChanges к буферу (Incremental/Full) ──
// LSP position → offset в строке (строки разделены '\n').
static size_t positionToOffset(const std::string& text, int line, int character) {
    size_t pos = 0;
    for (int i = 0; i < line; ++i) {
        size_t nl = text.find('\n', pos);
        if (nl == std::string::npos)
            break;
        pos = nl + 1;
    }
    if (pos + static_cast<size_t>(character) > text.size())
        return text.size();
    return pos + static_cast<size_t>(character);
}

std::string TrustLsp::applyContentChanges(const std::string& filePath, const json& contentChanges) {
    auto it = openDocuments_.find(filePath);
    std::string text;
    if (it != openDocuments_.end()) {
        text = it->second;
    } else {
        // Буфер ещё не открыт (didOpen не было) — инициализируем содержимым с диска
        auto code = trust::utils::FileIO::read<std::vector<char>>(filePath);
        if (code)
            text.assign(code->data(), code->size());
    }

    if (!contentChanges.is_array())
        return text;

    for (const auto& ch : contentChanges) {
        if (ch.contains("range")) {
            const auto& range = ch["range"];
            size_t s = positionToOffset(text, range.value("start", json()).value("line", 0), range.value("start", json()).value("character", 0));
            size_t e = positionToOffset(text, range.value("end", json()).value("line", 0), range.value("end", json()).value("character", 0));
            if (e < s)
                e = s;
            text.replace(s, e - s, ch.value("text", ""));
        } else {
            // Без range — полная замена текста (Full-вариант)
            text = ch.value("text", "");
        }
    }
    return text;
}

// ── Пере-транспиляция одного «грязного» документа (синхронно) ──
void TrustLsp::flushDocument(const std::string& filePath) {
    pendingTranspile_.erase(filePath);
    auto it = openDocuments_.find(filePath);
    if (it == openDocuments_.end())
        return;

    std::string transpileErr = transpileSource(filePath, it->second);
    if (!transpileErr.empty()) {
        log("transpilation failed for " + filePath + ": " + transpileErr + ", using previous cache");
    }
    publishDiagnostics(filePathToUri(filePath));
}

// ── Debounce-флаш: транспилируем документы, чей период тишины истёк ──
void TrustLsp::flushPendingTranspile() {
    if (pendingTranspile_.empty())
        return;
    const auto now = std::chrono::steady_clock::now();
    std::vector<std::string> due;
    for (const auto& [path, t] : pendingTranspile_) {
        if (now - t >= std::chrono::milliseconds(kDebounceMs))
            due.push_back(path);
    }
    for (const auto& path : due)
        flushDocument(path);
}

// ── handleDefinition: Go to Definition ──
void TrustLsp::handleDefinition(const json& req) {
    json id = req.value("id", json());

    json params = req.value("params", json());
    std::string uri = params.value("textDocument", json()).value("uri", "");
    json position = params.value("position", json());
    int line = position.value("line", 0);
    int character = position.value("character", 0);

    std::string filePath = uriToFilePath(uri);
    log("handleDefinition: " + filePath + " line=" + std::to_string(line) + " col=" + std::to_string(character));

    std::string err;
    auto cr = getCachedReader(filePath, err);
    if (!cr.reader) {
        if (!err.empty()) {
            log("  " + err);
        }
        sendLspResponse(transport_, id, json());
        return;
    }

    const auto& reader = *cr.reader;
    trust::ReaderFile queryIdx = cr.isCppRequest ? cr.cppReaderIdx : cr.trustReaderIdx;
    trust::SourceMapReader::Location loc = reader.lspToLocation(queryIdx, line, character);

    auto maybeMap = reader.findRangeMap(loc);
    if (!maybeMap.has_value()) {
        log("  no mapping found for definition");
        sendLspResponse(transport_, id, json());
        return;
    }

    const auto& rangeMap = *maybeMap;
    // Для definition нужна целевая сторона (куда перейти)
    const auto& targetRange = rangeMap.to;

    // Определяем целевой файл
    trust::ReaderFile targetFile = targetRange.begin.fileIdx();
    std::string targetPath;
    if (targetFile.isOutput()) {
        // C++ файл — берём cppFilePath из кеша
        targetPath = sourceCache_.at(cr.isCppRequest ? cppToTrustCache_.at(filePath) : filePath).cppFilePath;
    } else {
        // Trust файл — берём исходный путь
        targetPath = cr.isCppRequest ? cppToTrustCache_.at(filePath) : filePath;
    }

    std::string targetUri = makeFragmentUri(reader, targetPath, targetRange);

    json lspLocation = {{"uri", targetUri}, {"range", rangeToLspRange(reader, targetRange)}};
    sendLspResponse(transport_, id, lspLocation);
    log("  definition: from " + formatRange(reader, rangeMap.from, filePath) + "  ->  " + formatRange(reader, rangeMap.to, targetPath));
}

// ── buildHoverContents: универсальный построитель содержимого ховера ──
// Строит массив Markdown-элементов:
//   [0] = "```<lang>\n<text>\n```" (базовый код с противоположной стороны)
//   [1+] = Markdown-ссылки на определения (если курсор на идентификаторе / макросе)
nlohmann::json TrustLsp::buildHoverContents(const trust::SourceMapReader& reader, bool isCppRequest, const trust::SourceMapReader::Location& cursorLoc,
                                            const std::string& hoverText, const std::string& hoverLang, const std::string& trustFilePath,
                                            const std::string& cppFilePath) {
    json hoverContents = json::array();

    // Базовый блок с кодом противоположной стороны
    hoverContents.push_back("```" + hoverLang + "\n" + hoverText + "\n```");

    // Пытаемся выделить слово под курсором
    auto maybeWord = reader.getWordAt(cursorLoc);
    if (!maybeWord.has_value()) {
        return hoverContents; // курсор не на идентификаторе — базовый ховер достаточен
    }
    const std::string& word = *maybeWord;

    if (!isCppRequest) {
        // ── Запрос из src (trust) файла ──
        // Сначала пробуем получить маппинг имени переменной (NameMap)
        auto maybeName = reader.getCppName(cursorLoc, word);
        if (maybeName.has_value() && !maybeName->macroDefRange.has_value()) {
            const auto& targetRange = maybeName->rangeMap.to;
            std::string targetUri = makeFragmentUri(reader, cppFilePath, targetRange);
            hoverContents.push_back("[→ C++: " + maybeName->fromName + "](" + targetUri + ")");
        }

        // Поиск макроса: вызов макроса → определение макроса
        auto maybeMacroDef = reader.getMacroDefRange(cursorLoc);
        if (maybeMacroDef.has_value()) {
            std::string_view defText = reader.getText(*maybeMacroDef);
            std::string macroTargetUri = makeFragmentUri(reader, trustFilePath, *maybeMacroDef);
            hoverContents.push_back("[Macro: " + std::string(defText) + "](" + macroTargetUri + ")");
        }
    } else {
        // ── Запрос из C++ файла ──
        auto maybeName = reader.getTrustName(cursorLoc, word);
        if (maybeName.has_value()) {
            const auto& targetRange = maybeName->rangeMap.from;
            std::string targetUri = makeFragmentUri(reader, trustFilePath, targetRange);
            hoverContents.push_back("[← Trust: " + maybeName->toName + "](" + targetUri + ")");
        } else {
            // NameMap нет (expression-операторы, embed и т.п.) — даём обратную ссылку
            // через statement-маппинг (backward cpp→trust), чтобы из cppt можно было
            // перейти обратно в src к соответствующему фрагменту.
            auto maybeStmt = reader.findRangeMap(cursorLoc);
            if (maybeStmt.has_value() && !maybeStmt->to.isInvalid() && !maybeStmt->to.begin.fileIdx().isOutput()) {
                std::string_view srcText = reader.getText(maybeStmt->to);
                std::string targetUri = makeFragmentUri(reader, trustFilePath, maybeStmt->to);
                hoverContents.push_back("[← Trust: " + std::string(srcText) + "](" + targetUri + ")");
            }
        }
    }

    return hoverContents;
}

// ── handleHover: возвращает код для фрагмента под курсором ──
void TrustLsp::handleHover(const json& req) {
    json id = req.value("id", json());

    json params = req.value("params", json());
    std::string uri = params.value("textDocument", json()).value("uri", "");
    json position = params.value("position", json());
    int line = position.value("line", 0);
    int character = position.value("character", 0);

    std::string filePath = uriToFilePath(uri);
    log("handleHover: " + filePath + " line=" + std::to_string(line) + " col=" + std::to_string(character));

    std::string err;
    auto cr = getCachedReader(filePath, err);
    if (!cr.reader) {
        if (!err.empty()) {
            log("  " + err);
            // Не отправляем LSP ошибку — показываем информационный ховер
            json hoverContents = json::array();
            hoverContents.push_back("```txt\n" + err + "\n```");
            sendLspResponse(transport_, id, {{"contents", hoverContents}});
        } else {
            // C++ файл не из кеша — не наш файл, просто пустой ответ
            sendLspResponse(transport_, id, {{"contents", json::array()}});
        }
        return;
    }

    const auto& reader = *cr.reader;
    trust::ReaderFile queryIdx = cr.isCppRequest ? cr.cppReaderIdx : cr.trustReaderIdx;
    trust::SourceMapReader::Location loc = reader.lspToLocation(queryIdx, line, character);

    auto maybeMap = reader.findRangeMap(loc);
    if (!maybeMap.has_value()) {
        log("  no mapping found for hover");
        sendLspResponse(transport_, id, json{{"contents", json::array()}});
        return;
    }

    const auto& rangeMap = *maybeMap;
    // Показываем противоположную сторону (куда маппится)
    const auto& targetRange = rangeMap.to;
    std::string_view text = reader.getText(targetRange);
    log("  hover: from " + formatRange(reader, rangeMap.from, filePath) + "  ->  " +
        formatRange(reader, rangeMap.to, cr.isCppRequest ? cppToTrustCache_.at(filePath) : filePath));

    // Определяем язык для подсветки
    bool showCpp = !targetRange.begin.fileIdx().isOutput();
    std::string lang = showCpp ? "trust" : "cpp";

    // Определяем trustFilePath и cppFilePath для buildHoverContents
    std::string trustFilePath = cr.isCppRequest ? cppToTrustCache_.at(filePath) : filePath;
    std::string cppFilePath = sourceCache_.at(trustFilePath).cppFilePath;

    json hoverContents = buildHoverContents(reader, cr.isCppRequest, loc, std::string(text), lang, trustFilePath, cppFilePath);

    // Добавляем fixit-предложения из диагностик, если курсор внутри их диапазона
    {
        auto cacheIt = sourceCache_.find(trustFilePath);
        if (cacheIt != sourceCache_.end()) {
            const auto* ctx = cacheIt->second.sourceMap.get(); // Context для line_column конверсии
            const auto& diagnostics = ctx->diag().diagnostics();
            for (const auto& entry : diagnostics) {
                if (entry.fixits.empty() || entry.range.begin.isInvalid())
                    continue;
                if (entry.range.begin.fileIdx().isOutput())
                    continue;
                auto beginLC = ctx->source().line_column(entry.range.begin);
                auto endLC = !entry.range.end.isInvalid() && entry.range.begin.fileIdx() == entry.range.end.fileIdx()
                                 ? ctx->source().line_column(entry.range.end)
                                 : beginLC;
                int diagStartLine = static_cast<int>(beginLC.line) - 1;
                int diagStartChar = static_cast<int>(beginLC.column) - 1;
                int diagEndLine = static_cast<int>(endLC.line) - 1;
                int diagEndChar = static_cast<int>(endLC.column) - 1;

                // Проверяем попадание курсора (line, character) в диапазон диагностики
                if ((line > diagStartLine || (line == diagStartLine && character >= diagStartChar)) &&
                    (line < diagEndLine || (line == diagEndLine && character <= diagEndChar))) {
                    for (const auto& fixit : entry.fixits) {
                        hoverContents.push_back("_Fix: `" + fixit.replacement + "`_");
                    }
                }
            }
        }
    }

    json result = {{"contents", hoverContents}};
    sendLspResponse(transport_, id, result);
    log("  hover built with " + std::to_string(hoverContents.size()) + " element(s)");
}

// ── handleDocumentLink: возвращает ссылки на определения макросов ──
void TrustLsp::handleDocumentLink(const json& req) {
    json id = req.value("id", json());

    json params = req.value("params", json());
    std::string uri = params.value("textDocument", json()).value("uri", "");

    std::string filePath = uriToFilePath(uri);
    log("handleDocumentLink: " + filePath);

    std::string err;
    auto cr = getCachedReader(filePath, err);
    if (!cr.reader) {
        if (!err.empty()) {
            log("  " + err);
        }
        sendLspResponse(transport_, id, json::array());
        return;
    }

    const auto& reader = *cr.reader;

    // Определяем trust-путь для доступа к кешу
    std::string trustFilePath = cr.isCppRequest ? cppToTrustCache_.at(filePath) : filePath;
    const CachedSource& cs = sourceCache_.at(trustFilePath);

    json links = json::array();

    if (cr.isCppRequest) {
        // ── C++ → Trust: link на trust-файл по cpp-диапазону ──
        // Используем m_backward: for (cppKey, RangeMap { from=cppRange, to=trustRange })
        auto backward = reader.getBackwardMappings();
        log("  [cpp→trust] backward-mappings total=" + std::to_string(backward.size()) + " cppReaderIdx=" + std::to_string(cr.cppReaderIdx.as_index()));
        for (const auto& [key, entry] : backward) {
            (void)key;
            const auto& cppRange = entry.from;
            if (cppRange.begin.fileIdx() != cr.cppReaderIdx)
                continue;
            const auto& trustRange = entry.to;
            std::string targetUri = makeFragmentUri(reader, trustFilePath, trustRange);
            json link = {{"range", rangeToLspRange(reader, cppRange)}, {"target", targetUri}};
            links.push_back(std::move(link));
            log("    cpp link: " + formatRange(reader, cppRange, filePath) + "  ->  " + formatRange(reader, trustRange, trustFilePath));
        }

        // Добавляем NameMap-ссылки для C++ → Trust (по всем nameMappings)
        for (const auto& nameMap : reader.getNameMappings()) {
            const auto& toRange = nameMap.rangeMap.to;
            if (toRange.begin.fileIdx() != cr.cppReaderIdx)
                continue;
            std::string targetUri = makeFragmentUri(reader, trustFilePath, nameMap.rangeMap.from);
            json link = {{"range", rangeToLspRange(reader, toRange)}, {"target", targetUri}};
            links.push_back(std::move(link));
            log("    cpp name-link: " + formatRange(reader, toRange, filePath) + "  ->  " + formatRange(reader, nameMap.rangeMap.from, trustFilePath));
        }
    } else {
        // ── Trust → C++: link на C++ файл ──
        auto mappings = reader.getTrustFileMappings(cr.trustReaderIdx);
        log("  [trust→cpp] forward-mappings total=" + std::to_string(mappings.size()) + " trustReaderIdx=" + std::to_string(cr.trustReaderIdx.as_index()));
        for (const auto& mapping : mappings) {
            const auto& trustRange = mapping.from;
            const auto& cppRange = mapping.to;

            // Проверяем, является ли это макросом
            auto maybeMacroDef = reader.getMacroDefRange(trustRange.begin);
            if (maybeMacroDef.has_value()) {
                // Ссылка на определение макроса (в том же trust-файле)
                std::string targetUri = makeFragmentUri(reader, filePath, *maybeMacroDef);
                json link = {{"range", rangeToLspRange(reader, trustRange)}, {"target", targetUri}};
                links.push_back(std::move(link));
                log("    trust link(macro): " + formatRange(reader, trustRange, filePath) + "  ->  " + formatRange(reader, *maybeMacroDef, filePath));
            } else {
                // Обычный маппинг — link на C++ файл
                std::string targetUri = makeFragmentUri(reader, cs.cppFilePath, cppRange);
                json link = {{"range", rangeToLspRange(reader, trustRange)}, {"target", targetUri}};
                links.push_back(std::move(link));
                log("    trust link: " + formatRange(reader, trustRange, filePath) + "  ->  " + formatRange(reader, cppRange, cs.cppFilePath));
            }
        }

        // Добавляем NameMap-ссылки для Trust → C++ (по всем nameMappings)
        for (const auto& nameMap : reader.getNameMappings()) {
            const auto& fromRange = nameMap.rangeMap.from;
            if (fromRange.begin.fileIdx() != cr.trustReaderIdx)
                continue;
            std::string targetUri = makeFragmentUri(reader, cs.cppFilePath, nameMap.rangeMap.to);
            json link = {{"range", rangeToLspRange(reader, fromRange)}, {"target", targetUri}};
            links.push_back(std::move(link));
            log("    trust name-link: " + formatRange(reader, fromRange, filePath) + "  ->  " + formatRange(reader, nameMap.rangeMap.to, cs.cppFilePath));
        }
    }

    json result = links;
    sendLspResponse(transport_, id, result);
    log("  generated " + std::to_string(links.size()) + " document link(s)");
}

// ── Обработка workspace/executeCommand (заглушка — не используется) ──
void TrustLsp::handleExecuteCommand(const json& req) {
    json id = req.value("id", json());
    json params = req.value("params", json());
    std::string command = params.value("command", "");
    log("handleExecuteCommand: " + command + " (stub - not implemented)");
    sendLspError(transport_, id, -32601, "Command not implemented: " + command);
}

// ── Обработка workspace/didChangeConfiguration ──
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
}

// ── In-process транспиляция .src → C++ + source map ──

// Конвертирует LspOptions → PipelineOpts для использования Pipeline
static trust::PipelineOpts lspOptsToPipelineOpts(const LspOptions& lspOpts) {
    trust::PipelineOpts opts{};
    opts.verbose = lspOpts.trace;
    opts.quiet = false;
    opts.temp_dir = lspOpts.tempDir;
    return opts;
}

std::string TrustLsp::transpileSourceFile(const std::string& trustFilePath) {
    log("transpiling (in-process): " + trustFilePath);

    // Читаем содержимое файла
    auto trustCodeOpt = trust::utils::FileIO::read<std::vector<char>>(trustFilePath);
    if (!trustCodeOpt) {
        std::string err = "cannot open file: " + trustFilePath;
        log(err);
        return err;
    }
    std::string trustCodeStr(trustCodeOpt->data(), trustCodeOpt->size());
    return transpileSource(trustFilePath, trustCodeStr);
}

// Транспиляция текста буфера. trustFilePath — ключ кеша/идентификатор исходника,
// содержимое берётся из переданного текста, а не с диска.
std::string TrustLsp::transpileSource(const std::string& trustFilePath, const std::string& trustCodeStr) {
    log("transpiling (in-process): " + trustFilePath);
    std::string_view trustCode = trustCodeStr;

    // Создаём Context (projectDir или "." по умолчанию)
    auto ctx = std::make_unique<trust::Context>(opts_.projectDir.empty() ? "." : opts_.projectDir);

    // Конвертируем LspOptions → PipelineOpts и вычисляем пути через Pipeline
    auto pipelineOpts = lspOptsToPipelineOpts(opts_);
    pipelineOpts.input_file = trustFilePath;

    std::filesystem::path cpptPath = trust::computeCpptPath(pipelineOpts);
    bool saveToDisk = !opts_.tempDir.empty();

    // Если tempDir задан — создаём директорию
    if (saveToDisk) {
        std::error_code ec;
        std::filesystem::create_directories(cpptPath.parent_path(), ec);
        if (ec) {
            log("failed to create temp dir " + cpptPath.parent_path().string() + ": " + ec.message());
        }
    }

    // ── Загружаем исходный код и создаём выходной файл ──
    trust::MapperFile trustIdx = ctx->source().add_source(trustFilePath, std::string(trustCode));
    trust::MapperFile cppIdx = ctx->source().add_output(cpptPath.filename().string());

    // ── Запускаем пайплайн через Pipeline ──
    {
        // Pipeline сам настраивает diag, но trust-lsp предпочитает свой вывод
        trust::Pipeline pipeline(*ctx, pipelineOpts);
        pipeline.runPipeline(trust::PipelineSteps::ParseAST | trust::PipelineSteps::Semantic | trust::PipelineSteps::Transpile, trustIdx, cppIdx);
        // AST нас не интересует — нам нужен только source map и C++ код
        // Возвращаемый PipelineResult можно не использовать
    }

    // ── Конвертируем MapperFile → ReaderFile для хранения в кеше ──
    trust::ReaderFile trustReaderIdx = trust::ReaderFile::from(trustIdx);
    trust::ReaderFile cppReaderIdx = trust::ReaderFile::from(cppIdx);

    // Сохраняем .cppt + .src_map рядом + #embed — единый код
    if (saveToDisk) {
        if (trust::saveCppAndEmbedSourceMap(*ctx, cppIdx, cpptPath, opts_.trace)) {
            log("  saved cpp to: " + cpptPath.string());
        } else {
            log("warning: could not write to " + cpptPath.string());
        }
    }

    // Определяем абсолютный путь для cppFilePath (для корректного file:/// URI)
    std::string cppFilePath;
    if (saveToDisk) {
        cppFilePath = std::filesystem::absolute(cpptPath).string();
    } else {
        cppFilePath = std::filesystem::absolute(resolvePath(cpptPath.filename().string(), opts_.projectDir)).string();
    }

    // Проверяем наличие ошибок до перемещения в кеш
    bool hasErrors = ctx->diag().errorCount() > 0;
    std::string errMsg;
    if (hasErrors) {
        errMsg = "transpilation completed with " + std::to_string(ctx->diag().errorCount()) + " error(s)";
        log(errMsg);
    }

    // ── Трассировка: дамп всех маппингов из source map (до перемещения ctx в кеш) ──
    if (opts_.trace) {
        try {
            const auto* reader = ctx->source().toReader();
            if (reader) {
                log("  === mapping dump for " + trustFilePath + " ===");
                log("  trustReaderIdx=" + std::to_string(trustReaderIdx.as_index()) + " cppReaderIdx=" + std::to_string(cppReaderIdx.as_index()) +
                    " cppFilePath=" + cppFilePath);
                for (const auto& [key, entry] : reader->getForwardMappings()) {
                    (void)key;
                    log("    forward: " + formatRange(*reader, entry.from, trustFilePath) + " [in=" + std::to_string(entry.from.begin.fileIdx().as_index()) +
                        "@" + std::to_string(entry.from.begin.offset()) + "-" + std::to_string(entry.from.end.offset()) + "]  ->  " +
                        formatRange(*reader, entry.to, cppFilePath) + " [out=" + std::to_string(entry.to.begin.fileIdx().as_index()) + "@" +
                        std::to_string(entry.to.begin.offset()) + "-" + std::to_string(entry.to.end.offset()) + "]");
                }
                for (const auto& [key, entry] : reader->getBackwardMappings()) {
                    (void)key;
                    log("    backward: " + formatRange(*reader, entry.from, cppFilePath) + "  ->  " + formatRange(*reader, entry.to, trustFilePath));
                }
                for (const auto& nm : reader->getNameMappings()) {
                    log("    name(" + nm.fromName + " -> " + nm.toName + "): " + formatRange(*reader, nm.rangeMap.from, trustFilePath) +
                        " [in=" + std::to_string(nm.rangeMap.from.begin.fileIdx().as_index()) + "@" + std::to_string(nm.rangeMap.from.begin.offset()) + "-" +
                        std::to_string(nm.rangeMap.from.end.offset()) + "]  ->  " + formatRange(*reader, nm.rangeMap.to, cppFilePath) +
                        " [out=" + std::to_string(nm.rangeMap.to.begin.fileIdx().as_index()) + "@" + std::to_string(nm.rangeMap.to.begin.offset()) + "-" +
                        std::to_string(nm.rangeMap.to.end.offset()) + "]");
                }
                log("  === end mapping dump ===");
            }
        } catch (const std::exception& e) {
            log("  mapping dump failed: " + std::string(e.what()));
        }
    }

    // Кешируем результат (даже при ошибках диагностики — для частичного source map)
    CachedSource cs;
    cs.sourceMap = std::move(ctx);
    cs.cppFilePath = cppFilePath;
    cs.trustReaderIdx = trustReaderIdx;
    cs.cppReaderIdx = cppReaderIdx;
    sourceCache_[trustFilePath] = std::move(cs);

    if (!hasErrors) {
        cppToTrustCache_[cppFilePath] = trustFilePath;
        log("transpilation completed successfully");
    }

    return errMsg;
}

// ── Отправка диагностики из кеша ──
// Извлекает диагностики из CachedSource (если есть) по trustFilePath.
// trustFilePath используется как ключ в sourceCache_ для доступа к Context.
void TrustLsp::publishDiagnostics(const std::string& uri) {

    // Получаем filePath из URI
    std::string filePath = uriToFilePath(uri);
    json diagnostics = json::array();

    // Пытаемся найти кеш для этого файла
    auto it = sourceCache_.find(filePath);
    if (it != sourceCache_.end()) {
        const auto& diag = it->second.sourceMap->diag();
        const auto* ctx = it->second.sourceMap.get(); // Context владеет line_column для MapperLocation
        const auto& entries = diag.diagnostics();
        for (const auto& entry : entries) {
            if (entry.severity < trust::Severity::Error) {
                continue; // отправляем только ошибки и выше
            }
            // Конвертируем MapperRange → LSP Range
            json range = json::object();
            if (ctx && !entry.range.begin.isInvalid()) {
                auto beginLC = ctx->source().line_column(entry.range.begin);
                auto endLC = !entry.range.end.isInvalid() && entry.range.begin.fileIdx() == entry.range.end.fileIdx()
                                 ? ctx->source().line_column(entry.range.end)
                                 : beginLC;
                range = {{"start", {{"line", static_cast<int>(beginLC.line) - 1}, {"character", static_cast<int>(beginLC.column) - 1}}},
                         {"end", {{"line", static_cast<int>(endLC.line) - 1}, {"character", static_cast<int>(endLC.column) - 1}}}};
            } else {
                // Без позиции — ставим в начало
                range = {{"start", {{"line", 0}, {"character", 0}}}, {"end", {{"line", 0}, {"character", 1}}}};
            }

            log("  diagnostic: " + filePath + " range=" + range.dump() + " sev=" + std::to_string(static_cast<int>(entry.severity)) + " msg=" + entry.message);

            // Маппим Severity → LSP DiagnosticSeverity (1=Error, 2=Warning, 3=Info, 4=Hint)
            int lspSeverity = 3; // Info по умолчанию
            switch (entry.severity) {
            case trust::Severity::Fatal:
            case trust::Severity::Error:
                lspSeverity = 1;
                break;
            case trust::Severity::Warning:
                lspSeverity = 2;
                break;
            case trust::Severity::Note:
                lspSeverity = 3;
                break;
            case trust::Severity::Remark:
                lspSeverity = 4;
                break;
            }

            json diagObj = {{"range", range}, {"severity", lspSeverity}, {"source", "trust-lsp"}, {"message", entry.message}};

            // Прикрепляем fixit-подсказки, если есть
            if (!entry.fixits.empty() && ctx) {
                json fixits = json::array();
                for (const auto& f : entry.fixits) {
                    auto fbLC = ctx->source().line_column(f.range.begin);
                    auto feLC = ctx->source().line_column(f.range.end);
                    fixits.push_back({{"range",
                                       {{"start", {{"line", static_cast<int>(fbLC.line) - 1}, {"character", static_cast<int>(fbLC.column) - 1}}},
                                        {"end", {{"line", static_cast<int>(feLC.line) - 1}, {"character", static_cast<int>(feLC.column) - 1}}}}},
                                      {"replacement", f.replacement}});
                }
                diagObj["fixits"] = std::move(fixits);
            }

            diagnostics.push_back(std::move(diagObj));
        }
    }

    json params = {{"uri", uri}, {"diagnostics", diagnostics}};
    sendLspNotification(transport_, "textDocument/publishDiagnostics", params);
    log("published " + std::to_string(diagnostics.size()) + " diagnostic(s) for " + uri);
}