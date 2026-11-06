#pragma once

// Декодирование локации диагностики: MapperRange (offset) → file + 1-based line/col.
// Единый резолв для text/json/sarif-форматтеров и LSP publishDiagnostics: offset→локацию
// декодирует инжектируемый SourceTextProvider (реализует session::Context), поэтому diag
// остаётся листом. DiagnosticEntry — единственная запись; DiagnosticLocation — производная
// (декодированная) проекция локации, НЕ хранилище.

#include "diag/diag.hpp"

namespace trust {

/// Декодированная локация (file + 1-based begin/end).
/// Сентинел: begin.line==0 ⇔ локации нет (невалидный begin или отсутствующий провайдер).
struct DiagnosticLocation {
    std::string_view file;                      ///< provider->fileName(range.begin); пусто, если локации нет
    SourceTextProvider::LineColumn begin{0, 0}; ///< 1-based начало
    SourceTextProvider::LineColumn end{0, 0};   ///< 1-based конец; == begin при точке/ином файле

    [[nodiscard]] bool valid() const { return begin.line != 0; }
};

/// Декодирует произвольный MapperRange в локацию (диагностика и её fixit-диапазоны).
DiagnosticLocation decodeRange(const MapperRange& range, const SourceTextProvider* source);

/// Локация диагностики (частный случай decodeRange: по range записи).
inline DiagnosticLocation diagnosticLocation(const DiagnosticEntry& entry, const SourceTextProvider* source) {
    return decodeRange(entry.range, source);
}

} // namespace trust
