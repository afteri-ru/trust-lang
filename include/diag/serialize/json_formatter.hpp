#pragma once

// JSON-массив диагностик (--diagnostics-format=json): по одному объекту на запись.
// Массив открывается конструктором и закрывается деструктором (у движка нет «конца прогона»).

#include "diag/diag_formatter.hpp"

#include "utils/utils.hpp" // errs() — поток по умолчанию в default-аргументе (root header, не utils/io)

#include <ostream>

namespace trust {

class JsonFormatter : public DiagnosticFormatter {
  public:
    explicit JsonFormatter(std::ostream& out = trust::errs());
    ~JsonFormatter() override;

    void write(const DiagnosticEntry& entry, const SourceTextProvider* source) override;

  private:
    std::ostream& m_out;
    bool m_first = true;
};

} // namespace trust
