#include "diag/diag.hpp"
#include "diag/context.hpp"

#include <iostream>

namespace trust {

// output() — ядро вывода: формат file:line:col + исходная строка + caret (^~~).
// Для non-point диапазонов на той же строке рисует подчёркивание.

void DiagnosticEngine::setMinSeverity(Severity sev) {
    m_minSeverity = sev;
}

Severity DiagnosticEngine::minSeverity() const {
    return m_minSeverity;
}

void DiagnosticEngine::report(SourceRange range, Severity sev, std::string_view msg) {
    if (sev < m_minSeverity) return;
    output(range, sev, std::string(msg));
}

int DiagnosticEngine::errorCount() const {
    return m_errorCount;
}

int DiagnosticEngine::warningCount() const {
    return m_warningCount;
}

void DiagnosticEngine::setOutput(std::ostream* os) {
    m_output = os;
}

void DiagnosticEngine::clear() {
    m_errorCount = 0;
    m_warningCount = 0;
}

// severityToString — маппинг enum → строка. Должен соответствовать порядку Severity.
static const char* severityToString(Severity sev) {
    switch (sev) {
        case Severity::Remark:  return "remark";
        case Severity::Note:    return "note";
        case Severity::Warning: return "warning";
        case Severity::Error:   return "error";
        case Severity::Fatal:   return "fatal";
    }
    return "unknown";
}

void DiagnosticEngine::output(SourceRange range, Severity sev, std::string_view msg) {
    // Счётчики увеличиваются до фильтрации — это корректно, т.к. фильтрация на уровне report().
    if (sev == Severity::Error) m_errorCount++;
    if (sev == Severity::Warning) m_warningCount++;

    std::ostream& out = m_output ? *m_output : std::cerr;

    // Путь с локацией: печатаем file:line:col, строку кода и caret-подчёркивание.
    if (range.begin.isValid() && m_ctx) {
        auto src_idx = range.begin.source_idx();
        auto origin = m_ctx->source(src_idx);
        auto fname = m_ctx->filename(src_idx);

        auto begin_lc = m_ctx->line_column(range.begin);
        out << fname << ":" << begin_lc.line << ":" << begin_lc.column << ": "
            << severityToString(sev) << ": " << msg << "\n";

        const auto data = origin.data();
        const auto size = origin.size();
        const auto offset = range.begin.offset();

        if (offset < static_cast<int>(size) && offset > 0) {
            const char* line_start = data + offset - 1;
            while (line_start > data && line_start[-1] != '\n') --line_start;
            const char* line_end = line_start;
            while (*line_end && *line_end != '\n') ++line_end;

            std::string_view line_text(line_start, line_end - line_start);
            out << "  " << line_text << "\n";

            // Рендер caret: для однострочных range — подчёркивание ~ от begin до end.
            // Для многострочных — только ^ на begin.
            if (!range.is_point()) {
                auto end_lc = m_ctx->line_column(range.end);
                if (end_lc.line == begin_lc.line) {
                    int start_spaces = begin_lc.column - 1;
                    int underline_len = std::max(1, end_lc.column - begin_lc.column);
                    out << "  " << std::string(start_spaces, ' ') << "^";
                    if (underline_len > 1) {
                        out << std::string(underline_len - 1, '~');
                    }
                    out << "\n";
                } else {
                    out << "  " << std::string(begin_lc.column - 1, ' ') << "^\n";
                }
            } else {
                out << "  " << std::string(begin_lc.column - 1, ' ') << "^\n";
            }
        } else {
            out << "  ^\n";
        }
    // Без валидной локации — только severity: message.
    } else {
        out << severityToString(sev) << ": " << msg << "\n";
    }
}

} // namespace trust