#pragma once

// SARIF 2.1.0 (--diagnostics-format=sarif): документ с одним run; каждая диагностика — result.
// Документ открывается конструктором и закрывается деструктором.

#include "diag/diag_formatter.hpp"

#include "utils/utils.hpp" // errs() — поток по умолчанию в default-аргументе (root header, не utils/io)

#include <ostream>

namespace trust {

class SarifFormatter : public DiagnosticFormatter {
  public:
    SarifFormatter(ToolInfo tool, std::ostream& out = trust::errs());
    ~SarifFormatter() override;

    void write(const DiagnosticEntry& entry, const SourceTextProvider* source) override;

  private:
    std::ostream& m_out;
    ToolInfo m_tool;
    bool m_first = true;
};

} // namespace trust
