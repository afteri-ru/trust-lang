#include "lsp/hover_service.hpp"

#include "lsp/lsp_utils.hpp"
#include "lsp/lsp_protocol.h"
#include "lsp/builtin_catalog.h"
#include "lsp/completion.h"
#include "utils/uri.hpp"

#include <filesystem>

using json = nlohmann::json;
using trust::utils::uriToFilePath;

namespace trust {
namespace lsp {
namespace hover {

nlohmann::json buildHoverContents(const trust::SourceMapReader& reader, bool isCppRequest, const trust::SourceMapReader::Location& cursorLoc,
                                  const std::string& hoverText, const std::string& hoverLang, const std::string& trustFilePath, const std::string& cppFilePath,
                                  LspOptions& opts) {
    json hoverContents = json::array();

    // Базовый блок с кодом противоположной стороны
    hoverContents.push_back("```" + hoverLang + "\n" + hoverText + "\n```");

    // Путь к сохранённому dsl.src выводится из каталога .cppt (dsl.src сохраняется
    // в <каталог cppt>/trust/dsl.src). Если файла на диске нет (tempDir не задан) - ссылка «Macro:» не выводится.
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
        auto maybeName = reader.getCppName(cursorLoc, word);
        if (maybeName.has_value() && !maybeName->macroDefRange.has_value()) {
            const auto& targetRange = maybeName->rangeMap.to;
            std::string targetUri = makeFragmentUri(reader, cppFilePath, targetRange);
            hoverContents.push_back("[→ C++: " + maybeName->fromName + "](" + targetUri + ")");
        } else {
            if (maybeName.has_value() && maybeName->macroDefRange.has_value()) {
                const auto& defRange = *maybeName->macroDefRange;
                if (!dslFilePath.empty() && !defRange.isInvalid() && !defRange.begin.fileIdx().isOutput()) {
                    std::string targetUri = makeFragmentUri(reader, dslFilePath, defRange);
                    hoverContents.push_back("[Macro: " + maybeName->fromName + "](" + targetUri + ")");
                    lspLog(opts, "    macro hover def-link: " + formatRange(reader, defRange, dslFilePath));
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

void handleHover(trust::transport::Transport& transport, DocumentManager& documents, LspOptions& opts, const json& req) {
    json id = req.value("id", json());

    json params = req.value("params", json());
    std::string uri = params.value("textDocument", json()).value("uri", "");
    json position = params.value("position", json());
    int line = position.value("line", 0);
    int character = position.value("character", 0);

    std::string filePath = uriToFilePath(uri);
    lspLog(opts, "handleHover: " + filePath + " line=" + std::to_string(line) + " col=" + std::to_string(character));

    std::string err;
    auto cr = documents.getCachedReader(filePath, err);
    if (!cr.reader) {
        if (!err.empty()) {
            lspLog(opts, "  " + err);
            // Не отправляем LSP ошибку - показываем информационный ховер
            json hoverContents = json::array();
            hoverContents.push_back("```txt\n" + err + "\n```");
            sendLspResponse(transport, id, {{"contents", hoverContents}});
        } else {
            // C++ файл не из кеша - не наш файл, просто пустой ответ
            sendLspResponse(transport, id, {{"contents", json::array()}});
        }
        return;
    }

    const auto& reader = *cr.reader;
    trust::ReaderFile queryIdx = cr.isCppRequest ? cr.cppReaderIdx : cr.trustReaderIdx;
    trust::SourceMapReader::Location loc = reader.lspToLocation(queryIdx, line, character);

    auto maybeMap = reader.findRangeMap(loc);
    if (!maybeMap.has_value()) {
        // Каталоговый макрос (напр. `@func`) или предопределённый @__...__ не имеет source map,
        // но его док есть в едином хранилище BuiltinCatalog::macroDoc() (ключ с '@' и без).
        if (!cr.isCppRequest) {
            auto word = reader.getWordAt(loc);
            if (word.has_value()) {
                const std::string key = std::string(trust::lsp::completion::stripSigil(*word));
                const std::string* doc = trust::BuiltinCatalog::instance().macroDoc(key);
                if (doc && !doc->empty()) {
                    json hoverContents = json::array();
                    hoverContents.push_back("**" + *word + "**\n\n```trust\n" + *doc + "\n```");
                    sendLspResponse(transport, id, json{{"contents", hoverContents}});
                    return;
                }
            }
        }
        lspLog(opts, "  no mapping found for hover");
        sendLspResponse(transport, id, json{{"contents", json::array()}});
        return;
    }

    const auto& rangeMap = *maybeMap;
    // Показываем противоположную сторону (куда маппится)
    const auto& targetRange = rangeMap.to;
    std::string_view text = reader.getText(targetRange);
    lspLog(opts, "  hover: from " + formatRange(reader, rangeMap.from, filePath) + "  ->  " +
                     formatRange(reader, rangeMap.to, cr.isCppRequest ? documents.cppToTrustCache().at(filePath) : filePath));

    // Определяем язык для подсветки
    bool showCpp = !targetRange.begin.fileIdx().isOutput();
    std::string lang = showCpp ? "trust" : "cpp";

    // trustFilePath и cppFilePath для buildHoverContents
    std::string trustFilePath = cr.isCppRequest ? documents.cppToTrustCache().at(filePath) : filePath;
    std::string cppFilePath = documents.sourceCache().at(trustFilePath).cppFilePath;

    json hoverContents = buildHoverContents(reader, cr.isCppRequest, loc, std::string(text), lang, trustFilePath, cppFilePath, opts);

    // Добавляем fixit-предложения из диагностик, если курсор внутри их диапазона
    {
        auto cacheIt = documents.sourceCache().find(trustFilePath);
        if (cacheIt != documents.sourceCache().end()) {
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
        auto cacheIt = documents.sourceCache().find(trustFilePath);
        if (cacheIt != documents.sourceCache().end()) {
            auto word = reader.getWordAt(loc);
            if (word.has_value()) {
                const std::string key = std::string(trust::lsp::completion::stripSigil(*word));
                bool docFound = false;
                for (const auto& si : cacheIt->second.symbols) {
                    if (si.documentation.empty() || trust::lsp::completion::stripSigil(si.name) != key) {
                        continue;
                    }
                    // Имя для отображения: DSL-макросы хранятся в индексе с голым именем ('func'),
                    // а в коде записываются с сиглом ('@func'). Если пользователь написал имя с сиглом
                    // (@/%/$), а имя символа хранится без него - восстанавливаем сигл для отображения.
                    const std::string_view wv = *word;
                    const char sigil = (!wv.empty() && (wv[0] == '@' || wv[0] == '%' || wv[0] == '$')) ? wv[0] : '\0';
                    std::string disp = si.name;
                    if (sigil && !disp.empty() && disp[0] != '@' && disp[0] != '%' && disp[0] != '$') {
                        disp = std::string(1, sigil) + disp;
                    }
                    hoverContents.insert(hoverContents.begin(), "**" + disp + "**\n\n```trust\n" + si.documentation + "\n```");
                    docFound = true;
                    break;
                }
                // Каталоговые макросы (напр. @func, @__...__) - док в едином хранилище macroDoc().
                if (!docFound) {
                    const std::string* doc = trust::BuiltinCatalog::instance().macroDoc(key);
                    if (doc && !doc->empty()) {
                        hoverContents.insert(hoverContents.begin(), "**" + *word + "**\n\n```trust\n" + *doc + "\n```");
                    }
                }
            }
        }
    }

    json result = {{"contents", hoverContents}};
    sendLspResponse(transport, id, result);
    lspLog(opts, "  hover built with " + std::to_string(hoverContents.size()) + " element(s)");
}

} // namespace hover
} // namespace lsp
} // namespace trust
