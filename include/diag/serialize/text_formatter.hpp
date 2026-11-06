#pragma once

// Текстовый формат диагностик (формат по умолчанию): file:line:col + строка + caret (^~~),
// fix-it-пометка. Это ПРОЕКЦИЯ `DiagnosticEntry` (как json/sarif): отдельной текстовой модели
// данных нет — рендеринг читает ту же запись.

#include "diag/diag_formatter.hpp"

#include <ostream>

namespace trust {

class TextFormatter : public DiagnosticFormatter {
  public:
    /// Поток по умолчанию — `trust::errs()`, резолвится НА МОМЕНТ ЗАПИСИ (позднее связывание):
    /// движок создаётся раньше, чем тест/приложение перенаправляет `errs()` (setErrs).
    TextFormatter() = default;
    /// Инъекция конкретного потока (юнит-тесты: форматтер над ostringstream).
    explicit TextFormatter(std::ostream& out)
    : m_out(&out) {}

    void write(const DiagnosticEntry& entry, const SourceTextProvider* source) override;
    void writeFixitNote(const FixitSuggestion& fixit, const SourceTextProvider* source) override;

  private:
    std::ostream* m_out = nullptr; ///< nullptr → trust::errs() в момент записи
};

} // namespace trust
