// JSON-массив диагностик: массив открывается конструктором, закрывается деструктором.
// Поля объекта фиксированы (порядок стабилен): severity/code/message/file/line/column/
// endLine/endColumn + fixits (если есть). Байт-стабильность — на совести порядка записи.

#include "diag/serialize/json_formatter.hpp"

#include "diag/diag.hpp"
#include "diag/diagnostic_location.hpp"
#include "diag/serialize/json_write.hpp"
#include "utils/utils.hpp"

#include <ostream>
#include <string_view>

namespace trust {

JsonFormatter::JsonFormatter(std::ostream& out)
: m_out(out) {
    m_out << "[\n";
}

JsonFormatter::~JsonFormatter() {
    m_out << "]\n";
}

void JsonFormatter::write(const DiagnosticEntry& entry, const SourceTextProvider* source) {
    // Локация резолвится ЕДИНЫМ декодером (diagnosticLocation): file/line/col в 1-based.
    // Валидная локация = корректный begin + внедрённый провайдер (DiagnosticLocation::valid()).
    const DiagnosticLocation loc = diagnosticLocation(entry, source);

    m_out << (m_first ? "  " : ", ") << "{\n";
    m_first = false;
    m_out << "    \"severity\": ";
    diag::json::writeString(m_out, severityName(entry.severity));
    m_out << ",\n    \"code\": ";
    diag::json::writeStringOrNull(m_out, entry.optName);
    m_out << ",\n    \"message\": ";
    diag::json::writeString(m_out, entry.message);
    m_out << ",\n    \"file\": ";
    diag::json::writeStringOrNull(m_out, loc.valid() ? loc.file : std::string_view{});
    m_out << ",\n    \"line\": ";
    diag::json::writeSizeOrNull(m_out, loc.begin.line, loc.valid());
    m_out << ",\n    \"column\": ";
    diag::json::writeSizeOrNull(m_out, loc.begin.column, loc.valid());
    m_out << ",\n    \"endLine\": ";
    diag::json::writeSizeOrNull(m_out, loc.end.line, loc.valid());
    m_out << ",\n    \"endColumn\": ";
    diag::json::writeSizeOrNull(m_out, loc.end.column, loc.valid());
    if (!entry.fixits.empty()) {
        m_out << ",\n    \"fixits\": [";
        bool first = true;
        for (const FixitSuggestion& f : entry.fixits) {
            m_out << (first ? "" : ", ") << "{\"replacement\": ";
            first = false;
            diag::json::writeString(m_out, f.replacement);
            m_out << "}";
        }
        m_out << "]";
    }
    m_out << "\n  }";
}

} // namespace trust
