#pragma once

// include/diag/protocol.hpp
// Общие конверсии диагностик/диапазонов в протокольные координаты (LSP/DAP).
// Вынесены в diag, т.к. используются и LSP-сервером (publishDiagnostics/codeAction),
// и могут понадобиться DAP. Header-only (inline), без новых зависимостей diag_lib.

#include "diag/mapper.hpp"
#include "diag/severity.hpp"

namespace trust {

/// Позиция в протокольных координатах (0-based).
struct ProtocolPosition {
    int line = 0;
    int character = 0;
};

/// Диапазон в протокольных координатах (0-based).
struct ProtocolRange {
    ProtocolPosition start;
    ProtocolPosition end;
};

/// LSP DiagnosticSeverity (1=Error, 2=Warning, 3=Info, 4=Hint).
/// kSeverityToLsp/kSeverityCount генерируются из SEVERITIES (severity.hpp).
inline int severityToLsp(Severity sev) {
    const int i = static_cast<int>(sev);
    return (i >= 0 && i < kSeverityCount) ? kSeverityToLsp[i] : 3;
}

/// Конвертирует MapperRange в протокольный диапазон (0-based) через SourceMapWriter.
/// Невалидный диапазон → позиция (0,0). Диапазон из другого файла → точка (begin).
inline ProtocolRange mapperRangeToProtocol(const SourceMapWriter& src, MapperRange range) {
    ProtocolRange out;
    if (range.begin.isInvalid()) {
        return out; // (0,0)-(0,0)
    }
    const auto b = src.line_column(range.begin);
    out.start = {static_cast<int>(b.line) - 1, static_cast<int>(b.column) - 1};
    if (!range.end.isInvalid() && range.begin.fileIdx() == range.end.fileIdx()) {
        const auto e = src.line_column(range.end);
        out.end = {static_cast<int>(e.line) - 1, static_cast<int>(e.column) - 1};
    } else {
        out.end = out.start;
    }
    return out;
}

} // namespace trust
