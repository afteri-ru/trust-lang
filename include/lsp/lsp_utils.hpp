#pragma once

// include/lsp/lsp_utils.hpp
// Общие вспомогательные функции LSP (ранее - file-static в src/lsp/trust_lsp.cpp),
// используемые сервисами hover/navigation и TrustLsp. Не имеют состояния.

#include "lsp/lsp_options.hpp"

#include "diag/mapper.hpp"
#include "utils/io.hpp"
#include "utils/uri.hpp"

#include <nlohmann/json.hpp>

#include <format>
#include <string>

namespace trust {
namespace lsp {

// Location → LSP Position (0-based).
inline nlohmann::json locationToLspPosition(const trust::SourceMapReader& reader, trust::SourceMapReader::Location loc) {
    auto lc = reader.line_column(loc);
    return {{"line", static_cast<int>(lc.line) - 1}, {"character", static_cast<int>(lc.column) - 1}};
}

// Range → LSP Range.
inline nlohmann::json rangeToLspRange(const trust::SourceMapReader& reader, trust::SourceMapReader::Range range) {
    return {{"start", locationToLspPosition(reader, range.begin)}, {"end", locationToLspPosition(reader, range.end)}};
}

// Построение file:// URI с фрагментом из SourceMapReader::rangeToFragmentString.
inline std::string makeFragmentUri(const trust::SourceMapReader& reader, const std::string& basePath, trust::SourceMapReader::Range range) {
    std::string uri = trust::utils::filePathToUri(basePath);
    return uri + "#" + reader.rangeToFragmentString(range);
}

// Форматирование Range для трассировки: "path:line:col–line:col [текст]".
inline std::string formatRange(const trust::SourceMapReader& reader, trust::SourceMapReader::Range range, const std::string& filePath) {
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

// stderr-лог (только при opts_.trace), как в TrustLsp::log.
inline void lspLog(const LspOptions& opts, const std::string& msg) {
    if (opts.trace) {
        trust::errs() << "trust-lsp: " << msg << "\n";
    }
}

// Безусловная запись в stderr (видна в канале LSP), независимо от --trace.
// Для ВНУТРЕННИХ ошибок обработчиков (completion/codeAction): обработчик возвращает
// корректный (пустой) результат, но причина падения всегда фиксируется в выводе сервера.
inline void lspReportHandlerError(const std::string& what) {
    trust::errs() << "trust-lsp: " << what << "\n";
}

} // namespace lsp
} // namespace trust
