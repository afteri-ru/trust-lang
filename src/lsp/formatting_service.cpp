#include "lsp/formatting_service.hpp"

#include "lsp/analysis_service.hpp"
#include "lsp/lsp_options.hpp"
#include "lsp/lsp_protocol.h"
#include "lsp/lsp_utils.hpp"

#include "diag/context.hpp"
#include "diag/mapper.hpp"
#include "diag/protocol.hpp"
#include "formatter/format.hpp"
#include "pipeline/pipeline.hpp"
#include "syntax/parser.h"
#include "utils/file_io.hpp"
#include "utils/uri.hpp"

#include <filesystem>
#include <memory>

using json = nlohmann::json;
using trust::utils::uriToFilePath;

namespace trust {
namespace lsp {
namespace formatting {

void handleFormatting(trust::transport::Transport& transport, DocumentManager& documents, LspOptions& opts, const json& req) {
    json id = req.value("id", json());
    json params = req.value("params", json());
    std::string uri = params.value("textDocument", json()).value("uri", "");
    std::string filePath = uriToFilePath(uri);

    // Форматируем только Trust-файлы.
    if (!trust::SourceMapReader::isTrustFileExt(filePath)) {
        sendLspResponse(transport, id, json::array());
        return;
    }

    // Текст берём из буфера открытого документа (отражает несохранённые правки),
    // иначе — с диска.
    std::string text;
    auto it = documents.openDocuments().find(filePath);
    if (it != documents.openDocuments().end()) {
        text = it->second;
    } else {
        auto data = trust::utils::FileIO::read<std::vector<char>>(filePath);
        if (!data) {
            sendLspResponse(transport, id, json::array());
            return;
        }
        text.assign(data->data(), data->size());
    }

    trust::formatter::FormatOptions fopts;
    // Резолвим .trust-format для документа (как clang-format ищет .clang-format).
    const std::string cfgPath = trust::formatter::findConfig(std::filesystem::path(filePath).parent_path().string());
    if (!cfgPath.empty()) {
        auto cfg = trust::formatter::loadConfig(cfgPath);
        if (cfg.ok) {
            fopts = cfg.opts;
        } else {
            // Битый .trust-format - не тихий fallback: сообщаем в stderr (видно в логе LSP),
            // но форматируем с дефолтом (внешний файл, а не инвариант - EXPECT прервал бы
            // форматирование из-за некорректной пользовательской настройки).
            trust::errs() << "trust-lsp: warning: invalid .trust-format '" << cfgPath << "', using defaults\n";
        }
    }

    // Эффективные keywords (дефолт dsl + .trust-format + CLI) — набор «ключевых слов» форматтера.
    // Загружаем dsl через Pipeline (уважает --dsl/--no-dsl из LSP-опций/pipelineArgs) и
    // регистрируем макросы из самого документа (для классификации контрактов/no-paren).
    // Context держим до format(), чтобы Macro не повис.
    std::shared_ptr<trust::Context> macroCtx;
    try {
        macroCtx = std::make_shared<trust::Context>(opts.projectDir.empty() ? "." : opts.projectDir);
        // Применяем опции анализа ПО ИСТОЧНИКУ (шебанг vs окружение) по opts.shebangMode:
        // -W... и поведенческие флаги (--keywords, --solver-mode, -fsolver-loop-unroll).
        // Форматирование не публикует диагностики, поэтому ошибки опций только логируем.
        // Эффективные keywords для форматирования считываются из macroCtx->opts() ниже.
        {
            const std::vector<std::string> shebang = trust::lsp::extractShebangOptions(text);
            trust::lsp::applyAnalysisArgsBySource(
                macroCtx->opts(), opts.pipelineArgs, shebang, opts.shebangMode, [&opts](const std::string& msg, bool fromShebang) {
                    lspLog(opts, std::string("invalid ") + (fromShebang ? "shebang" : "environment") + " analysis options: " + msg);
                });
        }
        auto pipelineOpts = trust::lsp::analysis::lspOptsToPipelineOpts(opts);
        pipelineOpts.input_file = filePath;
        trust::Pipeline pipeline(*macroCtx, pipelineOpts);
        fopts.keywords = pipeline.effectiveKeywords(); // загружает DSL в macroCtx->macro()
    } catch (...) {
        // Если dsl не загрузился — остаёмся с keywords из .trust-format (не форматируем-по-keywords).
    }

    trust::formatter::FormatResult fres;
    if (macroCtx) {
        // Форматтер подписывается на Macro::on_macro_kind, прогоняет парсинг (в т.ч. модули).
        macroCtx->diag().setMinSeverity(trust::Severity::Fatal);
        trust::Parser parser(*macroCtx);
        fres = trust::formatter::format(text, filePath, fopts, parser);
    } else {
        // Нет контекста/DSL — форматируем с пустым парсером (только raw-@{...@}).
        trust::Context bareCtx;
        trust::Parser bareParser(bareCtx);
        fres = trust::formatter::format(text, filePath, fopts, bareParser);
    }
    if (!fres.ok) {
        // При лексических ошибках не форматируем (пустой результат).
        sendLspResponse(transport, id, json::array());
        return;
    }
    if (fres.text == text) {
        // Уже отформатировано — пустой список правок.
        sendLspResponse(transport, id, json::array());
        return;
    }

    // Диапазон всего документа: от (0,0) до конца.
    size_t line = 0;
    size_t lineStart = 0;
    for (size_t i = 0; i < text.size(); ++i) {
        if (text[i] == '\n') {
            ++line;
            lineStart = i + 1;
        }
    }
    json range = {{"start", {{"line", 0}, {"character", 0}}}, {"end", {{"line", line}, {"character", static_cast<int>(text.size() - lineStart)}}}};
    json edit = {{"range", range}, {"newText", fres.text}};
    sendLspResponse(transport, id, json::array({edit}));
}

} // namespace formatting
} // namespace lsp
} // namespace trust
