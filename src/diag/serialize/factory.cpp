// Фабрика форматтеров диагностик (единственная точка создания): движок по умолчанию создаёт
// text-форматтер; CLI/LSP запрашивают json/sarif через --diagnostics-format.

#include "diag/diag_formatter.hpp"

#include "diag/serialize/json_formatter.hpp"
#include "diag/serialize/sarif_formatter.hpp"
#include "diag/serialize/text_formatter.hpp"
#include "utils/error.hpp"

#include <memory>

namespace trust {

std::unique_ptr<DiagnosticFormatter> makeDiagnosticFormatter(DiagnosticsFormat format, ToolInfo tool) {
    switch (format) {
    case DiagnosticsFormat::Text:
        return std::make_unique<TextFormatter>();
    case DiagnosticsFormat::Json:
        return std::make_unique<JsonFormatter>();
    case DiagnosticsFormat::Sarif:
        return std::make_unique<SarifFormatter>(tool);
    }
    FAULT("unknown DiagnosticsFormat value {}", static_cast<int>(format));
}

} // namespace trust
