// Декодирование MapperRange → 1-based локация. Единый резолв для форматеров и LSP.

#include "diag/diagnostic_location.hpp"

namespace trust {

DiagnosticLocation decodeRange(const MapperRange& range, const SourceTextProvider* source) {
    DiagnosticLocation out;
    // offset==0 — «позиция до начала файла» (напр. модульный импорт/шапка): строку не резолвить
    // (calc_column требует offset>0). Такая локация приравнивается к отсутствующей.
    if (range.begin.isInvalid() || source == nullptr || range.begin.offset() == 0) {
        return out; // begin.line==0 => локации нет (сентинел)
    }
    out.file = source->fileName(range.begin);
    out.begin = source->lineColumn(range.begin);
    // Конец: только в том же файле и с ненулевым offset (зеркало mapperRangeToProtocol); иначе — точка.
    if (!range.end.isInvalid() && range.end.offset() != 0 && range.begin.fileIdx() == range.end.fileIdx()) {
        out.end = source->lineColumn(range.end);
    } else {
        out.end = out.begin;
    }
    return out;
}

} // namespace trust
