#include "lsp/trust_lsp.h"
#include "lsp/builtin_catalog.h"
#include "lsp/completion.h"
#include "diag/protocol.hpp"
#include "trust/version.h"
#include "utils/file_io.hpp"
#include "utils/uri.hpp"
#include "utils/utils.hpp"
#include "utils/io.hpp"

#include "pipeline/pipeline.hpp"
#include "transpiler/transpiler.hpp"
#include "ast/ast_nodes.hpp"
#include "syntax/macro.h"
#include "syntax/parser.h"
#include "utils/io.hpp"

#include <algorithm>
#include <cctype>
#include <cstring>
#include <filesystem>
#include <fstream>
#include <iostream>
#include <memory>
#include <regex>
#include <set>
#include <sstream>
#include <string>
#include <utility>
#include "utils/io.hpp"

using json = nlohmann::json;
using trust::utils::filePathToUri;
using trust::utils::resolvePath;
using trust::utils::uriToFilePath;

// Хелперы автодополнения вынесены в модуль lsp/completion.h.
using namespace trust::lsp::completion;

// -- Конструктор --
TrustLsp::TrustLsp(trust::transport::Transport& transport, const LspOptions& opts)
: transport_(transport)
, opts_(opts) {
}

// -- Локальный stderr-лог --
// trace - только если включён флаг --trace
void TrustLsp::log(const std::string& msg) const {
    if (opts_.trace) {
        trust::errs() << "trust-lsp: " << msg << "\n";
    }
}

// Безусловная запись в stderr (видна в канале LSP), независимо от --trace.
// Используется для ВНУТРЕННИХ ошибок обработчиков (completion/codeAction), чтобы
// не «глотать» их молча: LSP-обработчик возвращает корректный (пустой) результат,
// но причина падения всегда фиксируется в выводе сервера.
static void reportHandlerError(const std::string& what) {
    trust::errs() << "trust-lsp: " << what << "\n";
}

// -- Проверка jsonrpc: "2.0" --
static bool validateJsonRpc(const json& msg) {
    if (!msg.contains("jsonrpc")) {
        return false;
    }
    const auto& v = msg["jsonrpc"];
    return v.is_string() && v.get<std::string>() == "2.0";
}

// -- Вспомогательная: Location → LSP Position (0-based) --
static json locationToLspPosition(const trust::SourceMapReader& reader, trust::SourceMapReader::Location loc) {
    auto lc = reader.line_column(loc);
    return {{"line", static_cast<int>(lc.line) - 1}, {"character", static_cast<int>(lc.column) - 1}};
}

// -- Вспомогательная: Range → LSP Range --
static json rangeToLspRange(const trust::SourceMapReader& reader, trust::SourceMapReader::Range range) {
    return {{"start", locationToLspPosition(reader, range.begin)}, {"end", locationToLspPosition(reader, range.end)}};
}

// -- Построение file:// URI с фрагментом из SourceMapReader::rangeToFragmentString --
static std::string makeFragmentUri(const trust::SourceMapReader& reader, const std::string& basePath, trust::SourceMapReader::Range range) {
    std::string uri = filePathToUri(basePath);
    return uri + "#" + reader.rangeToFragmentString(range);
}

// -- Вспомогательная: форматирование Range для трассировки --
// Формат: "path:line:col–line:col [текст]". Текст берётся из source-map (getText).
// При невалидном range или невозможности получить текст - безопасный fallback.
static std::string formatRange(const trust::SourceMapReader& reader, trust::SourceMapReader::Range range, const std::string& filePath) {
    if (range.isInvalid()) {
        return filePath + ":<invalid>";
    }
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

// -- Вспомогательная: проверка, является ли файл C++ (не trust-исходником) --
// Для C++ файла, не найденного в reverse-кеше, не нужно ничего делать.
// Перемещено в SourceMapReader::isCppFileExt

// -- Вспомогательная функция для получения Reader и FileIdx из кеша --
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
        // C++ файл не в reverse-кеше - это не транспилированный файл, игнорируем
        log("  getCachedReader: cpp file NOT in reverse-cache (miss): " + filePath);
        outError = "";
        return {nullptr, trust::ReaderFile{0}, trust::ReaderFile{0}, false};
    } else {
        trustFilePath = filePath;
        log("  getCachedReader: trust file request: " + filePath);
    }

    // Фиктивный (in-memory) источник: файла на диске нет, искать/открывать его
    // не нужно - сообщаем об этом явно вместо «file not found».
    if (trust::SourceMapReader::isInMemoryName(trustFilePath)) {
        outError = "in-memory source (no file on disk): " + trustFilePath;
        return {nullptr, trust::ReaderFile{0}, trust::ReaderFile{0}, false};
    }

    // Если документ «грязный» (debounce ещё не истёк) - синхронно пере-транспилируем его,
    // чтобы hover/documentLink/definition всегда отражали текущий буфер.
    if (pendingTranspile_.find(trustFilePath) != pendingTranspile_.end()) {
        flushDocument(trustFilePath);
    }

    auto it = sourceCache_.find(trustFilePath);
    if (it == sourceCache_.end()) {
        // Auto-recovery: если кэша нет, пытаемся транспилировать на лету.
        // При наличии буфера документа (didOpen/didChange) - транспилируем его,
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
            handleDefinition(req);
        } else if (method == "textDocument/hover") {
            handleHover(req);
        } else if (method == "textDocument/documentLink") {
            handleDocumentLink(req);
        } else if (method == "textDocument/completion") {
            handleCompletion(req);
        } else if (method == "textDocument/codeAction") {
            handleCodeAction(req);
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
                         {"documentLinkProvider", {{"resolveProvider", false}}},
                         {"completionProvider", {{"resolveProvider", false}, {"triggerCharacters", {"%", "$", ":", "@", "_", "."}}}},
                         {"codeActionProvider", {{"codeActionKinds", json::array({"quickfix"})}}}};

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

    // Если это не Trust-файл - игнорируем
    if (!trust::SourceMapReader::isTrustFileExt(filePath)) {
        log("  skipped (not a trust source file)");
        return;
    }

    // Если это C++ файл из reverse-кеша - не транспилируем, просто логируем
    if (cppToTrustCache_.find(filePath) != cppToTrustCache_.end()) {
        log("  cpp file already cached via reverse cache");
        return;
    }

    // Новое открытие - сбрасываем отложенную пере-транспиляцию
    pendingTranspile_.erase(filePath);

    // -- Транспилируем содержимое буфера из didOpen, а не файла на диске --
    // Это гарантирует, что hover/documentLink/definition сразу учитывают правки
    // в редакторе, даже если файл ещё не сохранён.
    std::string text = params.value("textDocument", json()).value("text", "");
    if (text.empty()) {
        // Нет текста буфера (fallback / переоткрытие) - читаем с диска
        log("  no buffer text in didOpen, reading from disk");
        transpileSourceFile(filePath);
        // Публикуем диагностики ВСЕГДА (не только при ошибках): предупреждения
        // (например, -Wsigil) должны подсвечиваться сразу при открытии, а не после
        // первой правки (didChange→flush). publishDiagnostics сам отправляет пустой
        // список, если диагностик нет.
        publishDiagnostics(uri);
    } else {
        openDocuments_[filePath] = text;
        transpileSource(filePath, text);
        publishDiagnostics(uri);
    }

    log("didOpen completed for " + filePath);
}

void TrustLsp::handleDidClose(const json& req) {
    json params = req.value("params", json());
    std::string uri = params.value("textDocument", json()).value("uri", "");

    std::string filePath = uriToFilePath(uri);

    // Не удаляем sourceCache_ или cppToTrustCache_ при didClose - это ломало
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

    // Если это не Trust-файл - игнорируем
    if (!trust::SourceMapReader::isTrustFileExt(filePath)) {
        log("  skipped (not a trust source file)");
        return;
    }

    // Обновляем буфер из contentChanges (инкрементальная синхронизация,
    // textDocumentSync=2). Каждый элемент - либо {text: полный текст},
    // либо {range, text} - инкрементальная правка.
    std::string newText = applyContentChanges(filePath, params.value("contentChanges", json::array()));
    openDocuments_[filePath] = newText;

    // Debounce: не транспилируем на каждую правку, а помечаем документ «грязным».
    // Фактическая пере-транспиляция произойдёт через kDebounceMs (flushPendingTranspile)
    // или синхронно при запросе hover/documentLink/definition (getCachedReader).
    pendingTranspile_[filePath] = std::chrono::steady_clock::now();
    log("didChange: buffer updated for " + filePath + " (transpile deferred)");
}

// -- Применение contentChanges к буферу (Incremental/Full) --
// LSP position → offset в строке (строки разделены '\n').
static size_t positionToOffset(const std::string& text, int line, int character) {
    size_t pos = 0;
    for (int i = 0; i < line; ++i) {
        size_t nl = text.find('\n', pos);
        if (nl == std::string::npos) {
            break;
        }
        pos = nl + 1;
    }
    if (pos + static_cast<size_t>(character) > text.size()) {
        return text.size();
    }
    return pos + static_cast<size_t>(character);
}

std::string TrustLsp::applyContentChanges(const std::string& filePath, const json& contentChanges) {
    auto it = openDocuments_.find(filePath);
    std::string text;
    if (it != openDocuments_.end()) {
        text = it->second;
    } else {
        // Буфер ещё не открыт (didOpen не было) - инициализируем содержимым с диска
        auto code = trust::utils::FileIO::read<std::vector<char>>(filePath);
        if (code) {
            text.assign(code->data(), code->size());
        }
    }

    if (!contentChanges.is_array()) {
        return text;
    }

    for (const auto& ch : contentChanges) {
        if (ch.contains("range")) {
            const auto& range = ch["range"];
            size_t s = positionToOffset(text, range.value("start", json()).value("line", 0), range.value("start", json()).value("character", 0));
            size_t e = positionToOffset(text, range.value("end", json()).value("line", 0), range.value("end", json()).value("character", 0));
            if (e < s) {
                e = s;
            }
            text.replace(s, e - s, ch.value("text", ""));
        } else {
            // Без range - полная замена текста (Full-вариант)
            text = ch.value("text", "");
        }
    }
    return text;
}

// -- Пере-транспиляция одного «грязного» документа (синхронно) --
void TrustLsp::flushDocument(const std::string& filePath) {
    pendingTranspile_.erase(filePath);
    auto it = openDocuments_.find(filePath);
    if (it == openDocuments_.end()) {
        return;
    }

    std::string transpileErr = transpileSource(filePath, it->second);
    if (!transpileErr.empty()) {
        log("transpilation failed for " + filePath + ": " + transpileErr + ", using previous cache");
    }
    publishDiagnostics(filePathToUri(filePath));
}

// -- Debounce-флаш: транспилируем документы, чей период тишины истёк --
void TrustLsp::flushPendingTranspile() {
    if (pendingTranspile_.empty()) {
        return;
    }
    const auto now = std::chrono::steady_clock::now();
    std::vector<std::string> due;
    for (const auto& [path, t] : pendingTranspile_) {
        if (now - t >= std::chrono::milliseconds(kDebounceMs)) {
            due.push_back(path);
        }
    }
    for (const auto& path : due) {
        flushDocument(path);
    }
}

// -- handleDefinition: Go to Definition --
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
        // C++ файл - берём cppFilePath из кеша
        targetPath = sourceCache_.at(cr.isCppRequest ? cppToTrustCache_.at(filePath) : filePath).cppFilePath;
    } else {
        // Trust файл - берём исходный путь
        targetPath = cr.isCppRequest ? cppToTrustCache_.at(filePath) : filePath;
    }

    std::string targetUri = makeFragmentUri(reader, targetPath, targetRange);

    json lspLocation = {{"uri", targetUri}, {"range", rangeToLspRange(reader, targetRange)}};
    sendLspResponse(transport_, id, lspLocation);
    log("  definition: from " + formatRange(reader, rangeMap.from, filePath) + "  ->  " + formatRange(reader, rangeMap.to, targetPath));
}

// -- buildHoverContents: универсальный построитель содержимого ховера --
// Строит массив Markdown-элементов:
//   [0] = "```<lang>\n<text>\n```" (базовый код с противоположной стороны)
//   [1+] = Markdown-ссылки на определения (если курсор на идентификаторе / макросе)
nlohmann::json TrustLsp::buildHoverContents(const trust::SourceMapReader& reader, bool isCppRequest, const trust::SourceMapReader::Location& cursorLoc,
                                            const std::string& hoverText, const std::string& hoverLang, const std::string& trustFilePath,
                                            const std::string& cppFilePath) {
    json hoverContents = json::array();

    // Базовый блок с кодом противоположной стороны
    hoverContents.push_back("```" + hoverLang + "\n" + hoverText + "\n```");

    // Путь к сохранённому dsl.src выводится из каталога .cppt (dsl.src сохраняется
    // в <каталог cppt>/trust/dsl.src). Отдельное поле не нужно - расположение
    // детерминировано относительно cppFilePath. Если файла на диске нет
    // (tempDir не задан) - ссылка «Macro:» не выводится.
    std::string dslFilePath;
    {
        std::filesystem::path dslPath = std::filesystem::path(cppFilePath).parent_path() / "trust" / "dsl.src";
        if (std::filesystem::exists(dslPath)) {
            dslFilePath = std::filesystem::absolute(dslPath).string();
        }
    }

    // Пытаемся выделить слово под курсором
    auto maybeWord = reader.getWordAt(cursorLoc);
    if (!maybeWord.has_value()) {
        return hoverContents; // курсор не на идентификаторе - базовый ховер достаточен
    }
    const std::string& word = *maybeWord;

    if (!isCppRequest) {
        // -- Запрос из src (trust) файла: ссылка «→ C++» на сгенерированный код. --
        // Если слово - объявленное имя - ссылка на его C++-имя (NameMap.to); иначе
        // (в т.ч. макрос DSL: @assert/@while/print) - на раскрытый statement в cppt
        // (findRangeMap → to). Для макроса дополнительно выдаём ссылку «Macro:» на его
        // определение (getMacroDefRange). Диапазон определения лежит в in-memory
        // источнике "@dsl"; ссылка навигируема только если dsl.src сохранён на диск
        // (путь выводится из каталога cppFilePath: <каталог cppt>/trust/dsl.src).
        auto maybeName = reader.getCppName(cursorLoc, word);
        if (maybeName.has_value() && !maybeName->macroDefRange.has_value()) {
            const auto& targetRange = maybeName->rangeMap.to;
            std::string targetUri = makeFragmentUri(reader, cppFilePath, targetRange);
            hoverContents.push_back("[→ C++: " + maybeName->fromName + "](" + targetUri + ")");
        } else {
            // Курсор на макросе (или на необъявленном имени).
            // Сначала ссылка на определение макроса («Macro:»), затем ссылка «→ C++»
            // на раскрытый код в cppt - большое тело макроса не должно скрывать
            // ссылку на определение. Переход по клику на текст идёт только в cppt.
            if (maybeName.has_value() && maybeName->macroDefRange.has_value()) {
                const auto& defRange = *maybeName->macroDefRange;
                if (!dslFilePath.empty() && !defRange.isInvalid() && !defRange.begin.fileIdx().isOutput()) {
                    std::string targetUri = makeFragmentUri(reader, dslFilePath, defRange);
                    hoverContents.push_back("[Macro: " + maybeName->fromName + "](" + targetUri + ")");
                    log("    macro hover def-link: " + formatRange(reader, defRange, dslFilePath));
                }
            }
            auto maybeStmt = reader.findRangeMap(cursorLoc);
            if (maybeStmt.has_value() && !maybeStmt->to.isInvalid() && maybeStmt->to.begin.fileIdx().isOutput()) {
                std::string_view cppText = reader.getText(maybeStmt->to);
                std::string targetUri = makeFragmentUri(reader, cppFilePath, maybeStmt->to);
                hoverContents.push_back("[→ C++: " + std::string(cppText) + "](" + targetUri + ")");
            }
        }
    } else {
        // -- Запрос из C++ файла --
        auto maybeName = reader.getTrustName(cursorLoc, word);
        if (maybeName.has_value()) {
            const auto& targetRange = maybeName->rangeMap.from;
            std::string targetUri = makeFragmentUri(reader, trustFilePath, targetRange);
            hoverContents.push_back("[← Trust: " + maybeName->toName + "](" + targetUri + ")");
        } else {
            // NameMap нет (expression-операторы, embed и т.п.) - даём обратную ссылку
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

// -- handleHover: возвращает код для фрагмента под курсором --
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
            // Не отправляем LSP ошибку - показываем информационный ховер
            json hoverContents = json::array();
            hoverContents.push_back("```txt\n" + err + "\n```");
            sendLspResponse(transport_, id, {{"contents", hoverContents}});
        } else {
            // C++ файл не из кеша - не наш файл, просто пустой ответ
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
                if (entry.fixits.empty() || entry.range.begin.isInvalid()) {
                    continue;
                }
                if (entry.range.begin.fileIdx().isOutput()) {
                    continue;
                }
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

    // Документирующий комментарий объявления/макроса - если имя под курсором есть в таблице
    // символов анализатора (SymbolIndex). Выводим в начало ховера.
    if (!cr.isCppRequest) {
        auto cacheIt = sourceCache_.find(trustFilePath);
        if (cacheIt != sourceCache_.end()) {
            auto word = reader.getWordAt(loc);
            if (word.has_value()) {
                const std::string key = std::string(stripSigil(*word));
                for (const auto& si : cacheIt->second.symbols) {
                    if (si.documentation.empty() || stripSigil(si.name) != key) {
                        continue;
                    }
                    hoverContents.insert(hoverContents.begin(), "**" + si.name + "**\n\n```trust\n" + si.documentation + "\n```");
                    break;
                }
            }
        }
    }

    json result = {{"contents", hoverContents}};
    sendLspResponse(transport_, id, result);
    log("  hover built with " + std::to_string(hoverContents.size()) + " element(s)");
}

// -- handleDocumentLink: возвращает ссылки на определения макросов --
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
        // -- C++ → Trust: link на trust-файл по cpp-диапазону --
        // Используем m_backward: for (cppKey, RangeMap { from=cppRange, to=trustRange })
        auto backward = reader.getBackwardMappings();
        log("  [cpp→trust] backward-mappings total=" + std::to_string(backward.size()) + " cppReaderIdx=" + std::to_string(cr.cppReaderIdx.as_index()));
        for (const auto& [key, entry] : backward) {
            (void)key;
            const auto& cppRange = entry.from;
            if (cppRange.begin.fileIdx() != cr.cppReaderIdx) {
                continue;
            }
            const auto& trustRange = entry.to;
            std::string targetUri = makeFragmentUri(reader, trustFilePath, trustRange);
            json link = {{"range", rangeToLspRange(reader, cppRange)}, {"target", targetUri}};
            links.push_back(std::move(link));
            log("    cpp link: " + formatRange(reader, cppRange, filePath) + "  ->  " + formatRange(reader, trustRange, trustFilePath));
        }

        // Добавляем NameMap-ссылки для C++ → Trust (по всем nameMappings)
        for (const auto& nameMap : reader.getNameMappings()) {
            const auto& toRange = nameMap.rangeMap.to;
            if (toRange.begin.fileIdx() != cr.cppReaderIdx) {
                continue;
            }
            std::string targetUri = makeFragmentUri(reader, trustFilePath, nameMap.rangeMap.from);
            json link = {{"range", rangeToLspRange(reader, toRange)}, {"target", targetUri}};
            links.push_back(std::move(link));
            log("    cpp name-link: " + formatRange(reader, toRange, filePath) + "  ->  " + formatRange(reader, nameMap.rangeMap.from, trustFilePath));
        }
    } else {
        // -- Trust → C++: link на C++ файл --
        auto mappings = reader.getTrustFileMappings(cr.trustReaderIdx);
        log("  [trust→cpp] forward-mappings total=" + std::to_string(mappings.size()) + " trustReaderIdx=" + std::to_string(cr.trustReaderIdx.as_index()));
        for (const auto& mapping : mappings) {
            const auto& trustRange = mapping.from;
            const auto& cppRange = mapping.to;

            // Для макросов и обычных операторов цель одна - раскрытый код в cppt
            // (клик по тексту ведёт только в cppt). Раньше для макроса строилась
            // ссылка на определение с basePath=filePath, но координаты определения
            // лежат в in-memory "@dsl" и применялись к src → переход уводил в конец
            // файла (баг навигации). Определение макроса навигируемо из ховера
            // (ссылка «Macro:» в buildHoverContents), а не из documentLink.
            if (cppRange.begin.fileIdx().isOutput()) {
                std::string targetUri = makeFragmentUri(reader, cs.cppFilePath, cppRange);
                json link = {{"range", rangeToLspRange(reader, trustRange)}, {"target", targetUri}};
                links.push_back(std::move(link));
                log("    trust link: " + formatRange(reader, trustRange, filePath) + "  ->  " + formatRange(reader, cppRange, cs.cppFilePath));
            }
        }

        // Добавляем NameMap-ссылки для Trust → C++ (по всем nameMappings)
        for (const auto& nameMap : reader.getNameMappings()) {
            const auto& fromRange = nameMap.rangeMap.from;
            if (fromRange.begin.fileIdx() != cr.trustReaderIdx) {
                continue;
            }
            std::string targetUri = makeFragmentUri(reader, cs.cppFilePath, nameMap.rangeMap.to);
            json link = {{"range", rangeToLspRange(reader, fromRange)}, {"target", targetUri}};
            links.push_back(std::move(link));
            log("    trust name-link: " + formatRange(reader, fromRange, filePath) + "  ->  " + formatRange(reader, nameMap.rangeMap.to, cs.cppFilePath));
        }
    }

    // Убираем ссылки, чей диапазон - строгое надмножество другого диапазона
    // (напр. маппинг всей функции перекрывает маппинги отдельных операторов и
    // при клике уводит к началу функции вместо конкретного оператора - баг #4).
    if (links.size() > 1) {
        auto posLE = [](const json& a, const json& b) -> bool {
            return a["line"].get<int>() < b["line"].get<int>() ||
                   (a["line"].get<int>() == b["line"].get<int>() && a["character"].get<int>() <= b["character"].get<int>());
        };
        auto contains = [&](const json& outer, const json& inner) -> bool {
            return posLE(outer["start"], inner["start"]) && posLE(inner["end"], outer["end"]);
        };
        json filtered = json::array();
        for (const auto& link : links) {
            bool isSuperset = false;
            for (const auto& other : links) {
                if (&link == &other) {
                    continue;
                }
                if (contains(link["range"], other["range"]) && !contains(other["range"], link["range"])) {
                    isSuperset = true;
                    break;
                }
            }
            if (!isSuperset) {
                filtered.push_back(link);
            }
        }
        links = std::move(filtered);
    }

    json result = links;
    sendLspResponse(transport_, id, result);
    log("  generated " + std::to_string(links.size()) + " document link(s)");
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
}

// -- Обработчик textDocument/completion --
void TrustLsp::handleCompletion(const json& req) {
    json id = req.value("id", json());
    json params = req.value("params", json());
    std::string uri = params.value("textDocument", json()).value("uri", "");
    std::string filePath = uriToFilePath(uri);
    json pos = params.value("position", json());
    int line = pos.value("line", 0);
    int character = pos.value("character", 0);

    log("completion: " + filePath + " " + std::to_string(line) + ":" + std::to_string(character));

    json items = json::array();
    try {
        std::string trustFilePath = filePath;
        auto cppIt = cppToTrustCache_.find(filePath);
        if (cppIt != cppToTrustCache_.end()) {
            trustFilePath = cppIt->second;
        }

        // Текст документа из буфера (актуальные правки). Не зависим от успешной
        // транспиляции - завершение работает и на недописанном коде.
        std::string docText;
        auto docIt = openDocuments_.find(trustFilePath);
        if (docIt != openDocuments_.end()) {
            docText = docIt->second;
        } else if (auto content = trust::utils::FileIO::read<std::string>(filePath)) {
            docText = *content;
        }

        // Единый каталог встроенных имён (типы/методы/функции/predef+DSL-макросы).
        const trust::BuiltinCatalog& catalog = trust::BuiltinCatalog::instance();
        // Пер-файловый реестр + таблица символов из кеша (последняя транспиляция).
        // Встроенные имена - из каталога; пользовательские - из SymbolIndex/реестра.
        const trust::TypeRegistry* reg = nullptr;
        const trust::SymbolIndex* symbols = nullptr;
        const trust::Context* ctx = nullptr;
        auto cit = sourceCache_.find(trustFilePath);
        if (cit != sourceCache_.end()) {
            reg = cit->second.types.get();
            symbols = &cit->second.symbols;
            ctx = cit->second.sourceMap.get();
        }

        std::string lineStr = lineAt(docText, line);
        // Позиция курсора в байтах UTF-8 (LSP даёт UTF-16 code units).
        const int byteChar = utf16ToByte(lineStr, character);

        // Режим: member-доступ (obj.<имя>) или обычное имя.
        std::string objName;
        std::string prefix;
        bool member = false;
        {
            int dot = -1;
            for (int i = 0; i < byteChar; ++i) {
                if (lineStr[static_cast<size_t>(i)] == '.') {
                    dot = i;
                }
            }
            if (dot >= 0) {
                bool onlyName = true;
                for (int i = dot + 1; i < byteChar; ++i) {
                    char c = lineStr[static_cast<size_t>(i)];
                    if (!(isalnum(static_cast<unsigned char>(c)) || c == '_' || c == '$')) {
                        onlyName = false;
                        break;
                    }
                }
                if (onlyName) {
                    member = true;
                    objName = memberObjectName(lineStr, dot);
                    prefix = lineStr.substr(static_cast<size_t>(dot + 1), static_cast<size_t>(byteChar - dot - 1));
                }
            }
            if (!member) {
                prefix = wordPrefix(lineStr, byteChar);
            }
        }
        // Начало набранного префикса в UTF-16 для textEdit-диапазона.
        const int utf16Start = byteToUtf16(lineStr, byteChar - static_cast<int>(prefix.size()));

        if (member) {
            collectMemberItems(reg, &catalog, symbols, objName, prefix, line, utf16Start, character, items);
        } else {
            // Имена пользовательского кода - из таблицы анализатора (SymbolIndex).
            collectSymbolItems(symbols, ctx, line, prefix, utf16Start, character, items);
            // Типы: встроенные (каталог) + пользовательские (реестр).
            collectTypeItems(reg, &catalog, prefix, line, utf16Start, character, items);
            // Макросы: predef/DSL (каталог) + записанные анализатором (SymbolIndex).
            collectMacroItems(&catalog, symbols, prefix, line, utf16Start, character, items);
        }
        items = sortCompletionItems(items);
    } catch (const std::exception& e) {
        reportHandlerError("completion error: " + std::string(e.what()));
    } catch (...) {
        reportHandlerError("completion error: unknown");
    }
    sendLspResponse(transport_, id, json{{"isIncomplete", false}, {"items", items}});
}

// -- Обработчик textDocument/codeAction (quickfix по fixits) --
// Клиент (VSCode) передаёт в context.diagnostics те же объекты, что сервер опубликовал
// в publishDiagnostics. Fixits сервер кладёт в зарезервированное LSP поле "data"
// (vscode-languageclient сохраняет только стандартные поля диагностики + data;
// кастомные поля на верхнем уровне отбрасывает - поэтому без data quickfix не доходит).
// Для обратной совместимости поддерживаем и прямое поле "fixits".
void TrustLsp::handleCodeAction(const json& req) {
    json id = req.value("id", json());
    json params = req.value("params", json());
    std::string uri = params.value("textDocument", json()).value("uri", "");
    json actions = json::array();
    try {
        const json& ctxDiags = params.value("context", json()).value("diagnostics", json::array());
        for (const auto& d : ctxDiags) {
            // Достаём fixits из data (стандарт LSP) с fallback на прямое поле "fixits".
            json fixits;
            if (d.contains("data") && d["data"].is_object() && d["data"].contains("fixits") && d["data"]["fixits"].is_array()) {
                fixits = d["data"]["fixits"];
            } else if (d.contains("fixits") && d["fixits"].is_array()) {
                fixits = d["fixits"];
            } else {
                continue;
            }
            std::string message = d.value("message", "diagnostic");
            json changes = json::array();
            for (const auto& f : fixits) {
                json te = {{"range", f["range"]}, {"newText", f.value("replacement", "")}};
                changes.push_back(std::move(te));
            }
            if (changes.empty()) {
                continue;
            }
            json action = {
                {"title", "Fix: " + message}, {"kind", "quickfix"}, {"diagnostics", json::array({d})}, {"edit", {{"changes", {{uri, std::move(changes)}}}}}};
            actions.push_back(std::move(action));
        }
    } catch (const std::exception& e) {
        reportHandlerError("codeAction error: " + std::string(e.what()));
    } catch (...) {
        reportHandlerError("codeAction error: unknown");
    }
    sendLspResponse(transport_, id, actions);
}

// -- In-process транспиляция .src → C++ + source map --

// Конвертирует LspOptions → PipelineOpts для использования Pipeline
static trust::PipelineOpts lspOptsToPipelineOpts(const LspOptions& lspOpts) {
    trust::PipelineOpts opts{};
    opts.verbose = lspOpts.trace;
    opts.quiet = false;
    opts.temp_dir = lspOpts.tempDir;
    // LSP: анализатор должен работать на частичном AST даже при ошибках лексера/парсера
    // (для сбора имён/типов и автодополнения). Транспиляция при ошибках не выполняется.
    opts.allow_semantic_on_errors = true;
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

// Транспиляция текста буфера. trustFilePath - ключ кеша/идентификатор исходника,
// содержимое берётся из переданного текста, а не с диска.

std::string TrustLsp::transpileSource(const std::string& trustFilePath, const std::string& trustCodeStr) {
    log("transpiling (in-process): " + trustFilePath);
    std::string_view trustCode = trustCodeStr;

    // Создаём Context (projectDir или "." по умолчанию)
    auto ctx = std::make_unique<trust::Context>(opts_.projectDir.empty() ? "." : opts_.projectDir);

    // Сбор имён/типов анализатором (SymbolCollectorHook) для автодополнения.
    ctx->opts().set_enabled(trust::FlagKind::Symbols, true);

    // Конвертируем LspOptions → PipelineOpts и вычисляем пути через Pipeline
    auto pipelineOpts = lspOptsToPipelineOpts(opts_);
    pipelineOpts.input_file = trustFilePath;

    std::filesystem::path cpptPath = trust::computeCpptPath(pipelineOpts);
    bool saveToDisk = !opts_.tempDir.empty();

    // Если tempDir задан - создаём директорию
    if (saveToDisk) {
        std::error_code ec;
        std::filesystem::create_directories(cpptPath.parent_path(), ec);
        if (ec) {
            log("failed to create temp dir " + cpptPath.parent_path().string() + ": " + ec.message());
        }
    }

    // -- Загружаем исходный код и создаём выходной файл --
    trust::MapperFile trustIdx = ctx->source().add_source(trustFilePath, std::string(trustCode));
    trust::MapperFile cppIdx = ctx->source().add_output(cpptPath.filename().string());

    // -- Запускаем пайплайн через Pipeline --
    trust::SymbolIndex symbols;                    // имена+типы+диапазоны из анализатора
    std::unique_ptr<trust::TypeRegistry> registry; // живой реестр (чтобы TypeId в symbols был валиден)
    {
        // Pipeline сам настраивает diag, но trust-lsp предпочитает свой вывод
        trust::Pipeline pipeline(*ctx, pipelineOpts);
        trust::PipelineResult result;
        try {
            result = pipeline.runPipeline(trust::PipelineSteps::ParseAST | trust::PipelineSteps::Semantic | trust::PipelineSteps::Transpile, trustIdx, cppIdx);
        } catch (const std::exception& e) {
            // Анализатор упал на частичном AST. НЕ глотаем без диагностики: публикуем сообщение
            // с диапазоном (начало файла), чтобы LSP показал причину падения, а не «пустоту».
            log("pipeline exception: " + std::string(e.what()));
            ctx->diag().report(trust::Severity::Error, ctx->source().makeLoc(trustIdx, 1), "internal analysis error: {}", e.what());
        }

        // Забираем владение реестром типов, чтобы SymbolInfo::type (TypeId) оставался валидным
        // после уничтожения Pipeline. Встроенные типы разделяются через общее ядро TypeRegistry.
        registry = pipeline.releaseTypes();

        // Символы из анализатора (SymbolCollectorHook): имя + тип + диапазоны. Если runPipeline
        // упал ДО семантического шага (Fatal при парсинге), result.symbols пуст - дособираем
        // макросы, записанные в Context во время парсинга (не теряются после PopScope модуля).
        if (result.symbols) {
            symbols = std::move(*result.symbols);
        } else {
            trust::appendMacroSymbols(*ctx, symbols);
        }
    }

    // -- Конвертируем MapperFile → ReaderFile для хранения в кеше --
    trust::ReaderFile trustReaderIdx = trust::ReaderFile::from(trustIdx);
    trust::ReaderFile cppReaderIdx = trust::ReaderFile::from(cppIdx);

    // -- Сохраняем dsl.src вместе с остальными заголовками в <tempDir>/trust/,
    // чтобы были навигируемы определения макросов: их диапазоны лежат в
    // in-memory источнике "@dsl" (файла на диске нет). Копируем содержимое
    // "@dsl" в <tempDir>/trust/dsl.src. Путь к файлу в buildHoverContents
    // выводится из каталога cppFilePath (<каталог cppt>/trust/dsl.src). --
    if (saveToDisk) {
        try {
            const auto* reader = ctx->source().toReader();
            if (reader) {
                trust::ReaderFile dslIdx = reader->findFile("@dsl");
                if (!dslIdx.isInvalid()) {
                    std::string_view dslSource = reader->source(dslIdx);
                    std::filesystem::path dslPath = cpptPath.parent_path() / "trust" / "dsl.src";
                    std::error_code ec;
                    std::filesystem::create_directories(dslPath.parent_path(), ec);
                    std::ofstream ofs(dslPath, std::ios::binary);
                    if (ofs) {
                        ofs.write(dslSource.data(), static_cast<std::streamsize>(dslSource.size()));
                        log("  saved dsl.src to: " + std::filesystem::absolute(dslPath).string());
                    }
                }
            }
        } catch (const std::exception& e) {
            log("  dsl.src save failed: " + std::string(e.what()));
        }
    }

    // Сохраняем .cppt + .src_map рядом + #embed - единый код
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

    // -- Трассировка: дамп всех маппингов из source map (до перемещения ctx в кеш) --
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

    // Кешируем результат (даже при ошибках диагностики - для частичного source map)
    CachedSource cs;
    cs.sourceMap = std::move(ctx);
    cs.cppFilePath = cppFilePath;
    cs.trustReaderIdx = trustReaderIdx;
    cs.cppReaderIdx = cppReaderIdx;
    cs.symbols = std::move(symbols);
    cs.types = std::move(registry);
    sourceCache_[trustFilePath] = std::move(cs);

    if (!hasErrors) {
        cppToTrustCache_[cppFilePath] = trustFilePath;
        log("transpilation completed successfully");
    }

    return errMsg;
}

// -- Отправка диагностики из кеша --
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
            // Публикуем только диагностики ТЕКУЩЕГО trust-файла: диагностики импортированных
            // модулей имеют собственные URI/координаты и не должны приписываться этому файлу.
            // Output (cpp) и невалидные диапазоны пропускаем (им нет места в проблем-панели src).
            if (entry.range.begin.isInvalid() || entry.range.begin.fileIdx().isOutput() ||
                entry.range.begin.fileIdx().as_index() != it->second.trustReaderIdx.as_index()) {
                continue;
            }
            // Публикуем все severity (Error/Fatal → 1, Warning → 2, Note → 3, Remark → 4).
            // Конвертируем MapperRange → LSP Range (0-based) через общий diag/protocol.hpp
            json range;
            if (ctx) {
                const auto pr = trust::mapperRangeToProtocol(ctx->source(), entry.range);
                range = {{"start", {{"line", pr.start.line}, {"character", pr.start.character}}},
                         {"end", {{"line", pr.end.line}, {"character", pr.end.character}}}};
            } else {
                // Без позиции - ставим в начало
                range = {{"start", {{"line", 0}, {"character", 0}}}, {"end", {{"line", 0}, {"character", 1}}}};
            }

            log("  diagnostic: " + filePath + " range=" + range.dump() + " sev=" + std::to_string(static_cast<int>(entry.severity)) + " msg=" + entry.message);

            // Severity → LSP DiagnosticSeverity (1=Error, 2=Warning, 3=Info, 4=Hint)
            const int lspSeverity = trust::severityToLsp(entry.severity);

            json diagObj = {{"range", range}, {"severity", lspSeverity}, {"source", "trust-lsp"}, {"message", entry.message}};

            // Прикрепляем fixit-подсказки, если есть. Кладём их в зарезервированное
            // LSP поле "data": vscode-languageclient сохраняет у диагностики только
            // стандартные поля + data, а кастомные (например, прямое "fixits") при
            // запросе codeAction отбрасывает - без data quickfix не построить.
            if (!entry.fixits.empty() && ctx) {
                json fixits = json::array();
                for (const auto& f : entry.fixits) {
                    const auto pr = trust::mapperRangeToProtocol(ctx->source(), f.range);
                    fixits.push_back({{"range",
                                       {{"start", {{"line", pr.start.line}, {"character", pr.start.character}}},
                                        {"end", {{"line", pr.end.line}, {"character", pr.end.character}}}}},
                                      {"replacement", f.replacement}});
                }
                diagObj["data"] = json{{"fixits", std::move(fixits)}};
            }

            diagnostics.push_back(std::move(diagObj));
        }
    }

    json params = {{"uri", uri}, {"diagnostics", diagnostics}};
    sendLspNotification(transport_, "textDocument/publishDiagnostics", params);
    log("published " + std::to_string(diagnostics.size()) + " diagnostic(s) for " + uri);
}