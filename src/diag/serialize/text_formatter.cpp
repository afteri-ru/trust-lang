// Текстовый рендеринг диагностики (формат по умолчанию): file:line:col + исходная строка +
// caret (^~~). Для non-point диапазонов на той же строке рисует подчёркивание. Рендеринг
// читает ТУ ЖЕ запись DiagnosticEntry, что json/sarif (проекция, а не отдельная модель).

#include "diag/serialize/text_formatter.hpp"

#include "diag/diag.hpp"
#include "diag/diagnostic_location.hpp"
#include "diag/severity.hpp"
#include "utils/error.hpp"
#include "utils/utils.hpp" // errs() — поток по умолчанию (резолвится в момент записи)

#include <algorithm>
#include <ostream>
#include <string>
#include <string_view>

namespace trust {

void TextFormatter::write(const DiagnosticEntry& entry, const SourceTextProvider* source) {
    std::ostream& out = m_out ? *m_out : errs();
    // Локация резолвится ЕДИНЫМ декодером (diagnosticLocation); каретка дополнительно
    // требует сам файловый текст (fileContents).
    const DiagnosticLocation loc = diagnosticLocation(entry, source);

    // Путь с локацией: печатаем file:line:col, строку кода и caret-подчёркивание.
    if (loc.valid()) {
        const std::string_view origin = source->fileContents(entry.range.begin);

        out << loc.file << ":" << loc.begin.line << ":" << loc.begin.column << ": " << severityName(entry.severity) << ": " << entry.message << "\n";

        const auto data = origin.data();
        const auto size = origin.size();
        const auto offset = entry.range.begin.offset();

        if (offset < static_cast<uint32_t>(size) && offset > 0) {
            const char* line_start = data + offset - 1;
            while (line_start > data && line_start[-1] != '\n') {
                --line_start;
            }
            const char* line_end = line_start;
            while (*line_end && *line_end != '\n') {
                ++line_end;
            }

            std::string_view line_text(line_start, line_end - line_start);
            out << "  " << line_text << "\n";

            // Рендер caret: для однострочных range - подчёркивание ~ от begin до end.
            // Для многострочных - только ^ на begin.
            if (!entry.range.is_point()) {
                if (loc.end.line == loc.begin.line) {
                    int start_spaces = loc.begin.column - 1;
                    int underline_len = std::max<int>(1, static_cast<int>(loc.end.column - loc.begin.column));
                    out << "  " << std::string(start_spaces, ' ') << "^";
                    if (underline_len > 1) {
                        out << std::string(underline_len - 1, '~');
                    }
                    out << "\n";
                } else {
                    out << "  " << std::string(loc.begin.column - 1, ' ') << "^\n";
                }
            } else {
                out << "  " << std::string(loc.begin.column - 1, ' ') << "^\n";
            }
        } else {
            out << "  ^\n";
        }

        // Без валидной локации - только severity: message.
    } else {
        out << severityName(entry.severity) << ": " << entry.message << "\n";
    }
}

void TextFormatter::writeFixitNote(const FixitSuggestion& fixit, const SourceTextProvider* source) {
    EXPECT(source != nullptr && "fixit requires a source text provider");
    std::ostream& out = m_out ? *m_out : errs();
    const DiagnosticLocation loc = decodeRange(fixit.range, source);
    // Инвариант вывода: координаты печатаются только при валидной локации (никаких ":0:0").
    if (loc.valid()) {
        out << loc.file << ":" << loc.begin.line << ":" << loc.begin.column << ": ";
    }
    out << "note: fix-it: replace with '" << fixit.replacement << "'\n";
}

} // namespace trust
