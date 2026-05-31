#include "lsp/trust_lsp.h"
#include "trust/version.h"
#include "utils/file_io.hpp"
#include "utils/uri.hpp"
#include "utils/utils.hpp"

#include <algorithm>
#include <filesystem>
#include <iostream>
#include <string>
#include <utility>

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
        std::cerr << "trust-lsp: " << msg << "\n";
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
    } else if (trust::SourceMapReader::isCppFileExt(filePath)) {
        // C++ файл не в reverse-кеше — это не транспилированный файл, игнорируем
        outError = "";
        return {nullptr, trust::ReaderFile{0}, trust::ReaderFile{0}, false};
    } else {
        trustFilePath = filePath;
    }

    auto it = sourceCache_.find(trustFilePath);
    if (it == sourceCache_.end()) {
        // Auto-recovery: если кэша нет, пытаемся транспилировать на лету
        if (!isCpp && !trust::SourceMapReader::isCppFileExt(trustFilePath) && trustFilePath.find(".src") != std::string::npos) {
            std::string transpileErr = transpileSourceFile(trustFilePath);
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

    const trust::SourceMapReader* reader = it->second.sourceMap->toReader();
    if (!reader) {
        outError = "Failed to get SourceMapReader";
        return {nullptr, trust::ReaderFile{0}, trust::ReaderFile{0}, false};
    }

    CachedReader result;
    result.reader = reader;
    result.trustReaderIdx = it->second.trustReaderIdx;
    result.cppReaderIdx = it->second.cppReaderIdx;
    result.isCppRequest = isCpp;
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
        } else {
            sendLspError(transport_, id, -32601, "Method not found: " + method);
        }
    } catch (const std::exception& e) {
        std::string msg = std::string("UNEXPECTED ERROR in request '") + method + "': " + e.what();
        std::cerr << "trust-lsp: " << msg << "\n";
        json logParams = {{"type", 1}, {"message", msg}}; // type=1 = Error
        sendLspNotification(transport_, "window/logMessage", logParams);
        sendLspError(transport_, id, -32803, "Internal error: " + std::string(e.what()));
    } catch (...) {
        std::string msg = std::string("UNEXPECTED ERROR in request '") + method + "': unknown exception";
        std::cerr << "trust-lsp: " << msg << "\n";
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
        std::cerr << "trust-lsp: " << msg << "\n";
        json logParams = {{"type", 1}, {"message", msg}};
        sendLspNotification(transport_, "window/logMessage", logParams);
    } catch (...) {
        std::string msg = std::string("UNEXPECTED ERROR in notification '") + method + "': unknown exception";
        std::cerr << "trust-lsp: " << msg << "\n";
        json logParams = {{"type", 1}, {"message", msg}};
        sendLspNotification(transport_, "window/logMessage", logParams);
    }
}

void TrustLsp::handleInitialize(const json& req) {
    json id = req.value("id", json());
    json capabilities = {{"textDocumentSync", 1}, // TextDocumentSyncKind::Full
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
}

// Проверка, является ли файл Trust-исходником (расширение .src).
// .trust — бинарный скомпилированный модуль, не является исходным файлом.
// Функция перемещена в SourceMapReader::isTrustFileExt

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

    // ── Проверяем хеш файла: если кэш есть и хеш совпадает — пропускаем транспиляцию ──
    auto it = sourceCache_.find(filePath);
    if (it != sourceCache_.end()) {
        const trust::SourceMapReader* reader = it->second.sourceMap->toReader();
        if (reader) {
            uint64_t cachedHash = reader->getFileHash(it->second.trustReaderIdx);
            // Читаем файл и вычисляем хеш текущего содержимого
            auto currentCode = trust::utils::FileIO::read<std::vector<char>>(filePath);
            if (currentCode) {
                trust::FileEntry temp(filePath, std::string(currentCode->data(), currentCode->size()));
                uint64_t currentHash = temp.getHash();
                if (cachedHash == currentHash) {
                    log("  hash matches, skipping transpilation");
                    return;
                }
                log("  hash changed, re-transpiling");
            }
        }
    }

    // In-process транспиляция Trust → C++ + source map
    std::string transpileErr = transpileSourceFile(filePath);

    if (!transpileErr.empty()) {
        // Диагностика при ошибке транспиляции
        publishDiagnostics(uri);
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

    // Перетранспилируем при изменении файла
    std::string transpileErr = transpileSourceFile(filePath);

    if (!transpileErr.empty()) {
        // Не удаляем существующий кеш при ошибке, чтобы не сломать hover/definition
        log("re-transpilation failed for " + filePath + ": " + transpileErr + ", using previous cache");
        publishDiagnostics(uri);
    }
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
    log("  definition -> " + targetPath);
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
        // Используем findRangeMap для поиска маппинга всей строки (как делает documentLink)
        auto map = reader.findRangeMap(cursorLoc);
        if (map.has_value()) {
            const auto& targetRange = map->to;
            std::string targetUri = makeFragmentUri(reader, cppFilePath, targetRange);

            auto maybeName = reader.getCppName(cursorLoc, word);
            if (maybeName.has_value() && !maybeName->macroDefRange.has_value()) {
                hoverContents.push_back("[→ C++: " + maybeName->fromName + "](" + targetUri + ")");
            }

            // Поиск макроса: вызов макроса → определение макроса
            auto maybeMacroDef = reader.getMacroDefRange(cursorLoc);
            if (maybeMacroDef.has_value()) {
                std::string_view defText = reader.getText(*maybeMacroDef);
                std::string macroTargetUri = makeFragmentUri(reader, trustFilePath, *maybeMacroDef);
                hoverContents.push_back("[Macro: " + std::string(defText) + "](" + macroTargetUri + ")");
            }
        }
    } else {
        // ── Запрос из C++ файла ──
        auto map = reader.findRangeMap(cursorLoc);
        if (map.has_value()) {
            const auto& targetRange = map->to;
            std::string targetUri = makeFragmentUri(reader, trustFilePath, targetRange);

            auto maybeName = reader.getTrustName(cursorLoc, word);
            if (maybeName.has_value()) {
                hoverContents.push_back("[← Trust: " + maybeName->toName + "](" + targetUri + ")");
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

    // Определяем язык для подсветки
    bool showCpp = !targetRange.begin.fileIdx().isOutput();
    std::string lang = showCpp ? "trust" : "cpp";

    // Определяем trustFilePath и cppFilePath для buildHoverContents
    std::string trustFilePath = cr.isCppRequest ? cppToTrustCache_.at(filePath) : filePath;
    std::string cppFilePath = sourceCache_.at(trustFilePath).cppFilePath;

    json hoverContents = buildHoverContents(reader, cr.isCppRequest, loc, std::string(text), lang, trustFilePath, cppFilePath);

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
        for (const auto& [key, entry] : reader.getBackwardMappings()) {
            (void)key;
            const auto& cppRange = entry.from;
            if (cppRange.begin.fileIdx() != cr.cppReaderIdx)
                continue;
            const auto& trustRange = entry.to;
            std::string targetUri = makeFragmentUri(reader, trustFilePath, trustRange);
            json link = {{"range", rangeToLspRange(reader, cppRange)}, {"target", targetUri}};
            links.push_back(std::move(link));
        }

        // Добавляем NameMap-ссылки для C++ → Trust (по всем nameMappings)
        for (const auto& nameMap : reader.getNameMappings()) {
            const auto& toRange = nameMap.rangeMap.to;
            if (toRange.begin.fileIdx() != cr.cppReaderIdx)
                continue;
            std::string targetUri = makeFragmentUri(reader, trustFilePath, nameMap.rangeMap.from);
            json link = {{"range", rangeToLspRange(reader, toRange)}, {"target", targetUri}};
            links.push_back(std::move(link));
        }
    } else {
        // ── Trust → C++: link на C++ файл ──
        auto mappings = reader.getTrustFileMappings(cr.trustReaderIdx);
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
            } else {
                // Обычный маппинг — link на C++ файл
                std::string targetUri = makeFragmentUri(reader, cs.cppFilePath, cppRange);
                json link = {{"range", rangeToLspRange(reader, trustRange)}, {"target", targetUri}};
                links.push_back(std::move(link));
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
        }
    }

    json result = links;
    sendLspResponse(transport_, id, result);
    log("  generated " + std::to_string(links.size()) + " document link(s)");
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
    std::string_view trustCode = trustCodeStr;

    // Создаём Context (projectDir или "." по умолчанию)
    auto ctx = std::make_unique<trust::Context>(opts_.projectDir.empty() ? "." : opts_.projectDir);

    // Вычисляем cppFileName:
    // Если tempDir задан — сохраняем туда (реальный файл на диске),
    // иначе — только имя без пути (in-memory, source map будет ссылаться на несуществующий файл)
    auto trustPath = std::filesystem::path(trustFilePath);
    std::string cppFileName;
    bool saveToDisk = false;
    if (!opts_.tempDir.empty()) {
        auto dir = std::filesystem::path(opts_.tempDir);
        std::error_code ec;
        std::filesystem::create_directories(dir, ec);
        if (ec) {
            log("failed to create temp dir " + opts_.tempDir + ": " + ec.message());
        }
        cppFileName = (dir / trustPath.stem()).string() + ".cppt";
        saveToDisk = true;
    } else {
        cppFileName = trustPath.filename().string() + ".cppt";
    }

    // Транспилируем напрямую в созданный Context
    auto [trustIdx, cppIdx] = trust::lsp::transpile(trustCode, trustFilePath, cppFileName, *ctx);

    // Конвертируем MapperFile → ReaderFile для хранения в кеше
    trust::ReaderFile trustReaderIdx = trust::ReaderFile::from(trustIdx);
    trust::ReaderFile cppReaderIdx = trust::ReaderFile::from(cppIdx);

    // Сохраняем C++ код на диск (расширение .cppt), если tempDir задан
    if (saveToDisk) {
        std::string_view cppBody = ctx->output_body(cppIdx);
        if (!trust::utils::FileIO::write(cppFileName, cppBody)) {
            log("warning: could not write to " + cppFileName);
        } else {
            log("  saved cpp to: " + cppFileName);
        }
    }

    // Определяем абсолютный путь для cppFilePath (для корректного file:/// URI)
    std::string cppFilePath;
    if (saveToDisk) {
        cppFilePath = std::filesystem::absolute(cppFileName).string();
    } else {
        cppFilePath = std::filesystem::absolute(resolvePath(cppFileName, opts_.projectDir)).string();
    }

    // Проверяем наличие ошибок после транспиляции
    const auto& diag = ctx->diag();
    if (diag.errorCount() > 0) {
        std::string errMsg = "transpilation completed with " + std::to_string(diag.errorCount()) + " error(s)";
        log(errMsg);
        // Кешируем даже при ошибках для частичного результата
        CachedSource cs;
        cs.sourceMap = std::move(ctx);
        cs.cppFilePath = cppFilePath;
        cs.trustReaderIdx = trustReaderIdx;
        cs.cppReaderIdx = cppReaderIdx;
        sourceCache_[trustFilePath] = std::move(cs);
        return errMsg;
    }

    CachedSource cs;
    cs.sourceMap = std::move(ctx);
    cs.cppFilePath = cppFilePath;
    cs.trustReaderIdx = trustReaderIdx;
    cs.cppReaderIdx = cppReaderIdx;
    sourceCache_[trustFilePath] = std::move(cs);
    cppToTrustCache_[cppFilePath] = trustFilePath;

    log("transpilation completed successfully");
    return {};
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
            if (ctx && entry.range.begin.isValid()) {
                auto beginLC = ctx->line_column(entry.range.begin);
                auto endLC =
                    entry.range.end.isValid() && entry.range.begin.fileIdx() == entry.range.end.fileIdx() ? ctx->line_column(entry.range.end) : beginLC;
                range = {{"start", {{"line", static_cast<int>(beginLC.line) - 1}, {"character", static_cast<int>(beginLC.column) - 1}}},
                         {"end", {{"line", static_cast<int>(endLC.line) - 1}, {"character", static_cast<int>(endLC.column) - 1}}}};
            } else {
                // Без позиции — ставим в начало
                range = {{"start", {{"line", 0}, {"character", 0}}}, {"end", {{"line", 0}, {"character", 1}}}};
            }

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
            diagnostics.push_back(std::move(diagObj));
        }
    }

    json params = {{"uri", uri}, {"diagnostics", diagnostics}};
    sendLspNotification(transport_, "textDocument/publishDiagnostics", params);
    log("published " + std::to_string(diagnostics.size()) + " diagnostic(s) for " + uri);
}
