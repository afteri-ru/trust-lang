#pragma once

// Общие вспомогательные функции LSP, используемые сервисами hover/navigation
// и TrustLsp. Не имеют состояния.

#include "lsp/catalog/lsp_options.hpp"

#include "symbols/reference_index.hpp"
#include "sourcemap/mapper.hpp"
#include "utils/error.hpp"
#include "utils/utils.hpp"

#include <nlohmann/json.hpp>

#include <format>
#include <optional>
#include <string>
#include <string_view>

namespace trust {
namespace lsp {

// Ссылка под курсором (по проекции семантики): диапазон объявления-цели в writer-домене.
// Сравниваются индексы входных файлов и offsets: у входных файлов writer/reader offset совпадают
// (сдвиг prepend существует только у выходных файлов).
inline std::optional<trust::MapperRange> findReferenceTarget(const trust::ReferenceIndex& references, const trust::SourceMapReader::Location& loc) {
    for (const trust::SymbolReference& reference : references) {
        if (reference.ref.begin.fileIdx().isOutput() || reference.ref.begin.fileIdx().as_index() != loc.fileIdx().as_index()) {
            continue;
        }
        if (loc.offset() >= reference.ref.begin.offset() && loc.offset() <= reference.ref.end.offset()) {
            return reference.decl;
        }
    }
    return std::nullopt;
}

// Путь входного файла source map в абсолютной форме: имена нормализованы относительно
// baseDirectory компиляции (в LSP это каталог проекта), поэтому относительное имя
// разрешается от projectDir, а не от CWD процесса.
inline std::string inputFilePath(const trust::SourceMapReader& reader, trust::ReaderFile file, const std::string& projectDir) {
    const std::string_view name = reader.filename(file);
    EXPECT(!name.empty() && "source map input file must have a name");
    return trust::utils::resolvePath(std::string(name), projectDir);
}

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
