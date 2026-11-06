// SARIF 2.1.0: документ с одним run; каждая диагностика — result. Уровни SARIF:
// error/fatal -> "error", warning -> "warning", note/remark -> "note". Документ открывается
// конструктором и закрывается деструктором (см. контракт diag_formatter.hpp).

#include "diag/serialize/sarif_formatter.hpp"

#include "diag/diag.hpp"
#include "diag/diagnostic_location.hpp"
#include "diag/serialize/json_write.hpp"
#include "utils/utils.hpp"

#include <ostream>
#include <string_view>

namespace trust {

namespace {

std::string_view sarifLevel(Severity sev) {
    switch (sev) {
    case Severity::Error:
    case Severity::Fatal:
        return "error";
    case Severity::Warning:
        return "warning";
    case Severity::Note:
    case Severity::Remark:
        return "note";
    case Severity::Ignore:
        break; // движок не передаёт Ignore форматтеру (фильтруется в output)
    }
    return "note";
}

} // namespace

SarifFormatter::SarifFormatter(ToolInfo tool, std::ostream& out)
: m_out(out)
, m_tool(tool) {
    m_out << "{\n";
    m_out << "  \"$schema\": \"https://json.schemastore.org/sarif-2.1.0.json\",\n";
    m_out << "  \"version\": \"2.1.0\",\n";
    m_out << "  \"runs\": [\n";
    m_out << "    {\n";
    m_out << "      \"tool\": {\"driver\": {";
    m_out << "\"name\": ";
    diag::json::writeString(m_out, m_tool.name.empty() ? std::string_view("TrustLang") : m_tool.name);
    if (!m_tool.version.empty()) {
        m_out << ", \"version\": ";
        diag::json::writeString(m_out, m_tool.version);
    }
    m_out << "}},\n";
    m_out << "      \"results\": [";
}

SarifFormatter::~SarifFormatter() {
    m_out << "\n      ]\n    }\n  ]\n}\n";
}

void SarifFormatter::write(const DiagnosticEntry& entry, const SourceTextProvider* source) {
    // Локация резолвится ЕДИНЫМ декодером (diagnosticLocation): file/line/col в 1-based.
    const DiagnosticLocation loc = diagnosticLocation(entry, source);

    m_out << (m_first ? "\n" : ",\n");
    m_first = false;
    m_out << "        {\"ruleId\": ";
    diag::json::writeStringOrNull(m_out, entry.optName);
    m_out << ", \"level\": ";
    diag::json::writeString(m_out, sarifLevel(entry.severity));
    m_out << ", \"message\": {\"text\": ";
    diag::json::writeString(m_out, entry.message);
    m_out << "}";
    if (loc.valid()) {
        m_out << ", \"locations\": [{\"physicalLocation\": {\"artifactLocation\": {\"uri\": ";
        diag::json::writeString(m_out, loc.file);
        m_out << "}, \"region\": {\"startLine\": " << loc.begin.line << ", \"startColumn\": " << loc.begin.column << ", \"endLine\": " << loc.end.line
              << ", \"endColumn\": " << loc.end.column << "}}}]";
    }
    if (!entry.fixits.empty()) {
        // SARIF `fix` требует artifactChanges с deletedRegion: локация fixit обязана резолвиться
        // (все подсказки создаются из диапазонов узлов).
        m_out << ", \"fixes\": [";
        bool first = true;
        for (const FixitSuggestion& f : entry.fixits) {
            const DiagnosticLocation fl = decodeRange(f.range, source);
            EXPECT(fl.valid() && "SARIF fix requires a resolvable location");
            m_out << (first ? "\n          " : ",\n          ");
            first = false;
            m_out << "{\"artifactChanges\": [{\"artifactLocation\": {\"uri\": ";
            diag::json::writeString(m_out, fl.file);
            m_out << "}, \"replacements\": [{\"deletedRegion\": {\"startLine\": " << fl.begin.line << ", \"startColumn\": " << fl.begin.column
                  << ", \"endLine\": " << fl.end.line << ", \"endColumn\": " << fl.end.column << "}, \"insertedContent\": {\"text\": ";
            diag::json::writeString(m_out, f.replacement);
            m_out << "}}]}]}";
        }
        m_out << "\n        ]";
    }
    m_out << "}";
}

} // namespace trust
