#include "diag/diag.hpp"
#include "diag/context.hpp"

#include "utils/io.hpp"

#include <iostream>

namespace trust {

// output() — ядро вывода: формат file:line:col + исходная строка + caret (^~~).
// Для non-point диапазонов на той же строке рисует подчёркивание.
// Также выполняет фильтрацию: Fatal не фильтруется, опции и minSeverity применяются
// к остальным severity.

void DiagnosticEngine::setMinSeverity(Severity sev) {
    m_minSeverity = sev;
}

Severity DiagnosticEngine::minSeverity() const {
    return m_minSeverity;
}

int DiagnosticEngine::errorCount() const {
    return m_errorCount;
}

int DiagnosticEngine::warningCount() const {
    return m_warningCount;
}

void DiagnosticEngine::clear() {
    m_errorCount = 0;
    m_warningCount = 0;
    m_diagnostics.clear();
}

// severityToString — маппинг enum → строка. Должен соответствовать порядку Severity.
static const char* severityToString(Severity sev) {
    switch (sev) {
    case Severity::Remark:
        return "remark";
    case Severity::Note:
        return "note";
    case Severity::Warning:
        return "warning";
    case Severity::Error:
        return "error";
    case Severity::Fatal:
        return "fatal";
    }
    return "unknown";
}

DiagnosticEntry* DiagnosticEngine::output(Severity sev, MapperRange range, OptKind opt, std::string_view msg) {
    // Fatal — не фильтруется ни опциями, ни minSeverity: всегда выводится и прерывает выполнение.
    if (sev != Severity::Fatal) {
        // Если опция задана и есть Options — проверяем severity через Options.
        if (m_opts && opt != OptKind::All) {
            auto opt_sev = m_opts->severity(opt);
            if (!opt_sev.has_value()) {
                // opt is "ignore" — не выводим диагностику
                return nullptr;
            }
            sev = *opt_sev;
        }

        if (sev < m_minSeverity)
            return nullptr;
    }

    // Счётчики увеличиваются до фильтрации — это корректно, т.к. фильтрация на уровне do_report().
    if (sev == Severity::Error)
        m_errorCount++;
    if (sev == Severity::Warning)
        m_warningCount++;

    // Сохраняем диагностику для последующего извлечения
    m_diagnostics.push_back({range, sev, opt, std::string(msg), {}});
    DiagnosticEntry& entry = m_diagnostics.back();

    std::ostream& out = errs();

    // Путь с локацией: печатаем file:line:col, строку кода и caret-подчёркивание.
    if (!range.begin.isInvalid() && m_ctx && range.begin.offset() > 0) {
        auto src_idx = range.begin.fileIdx();
        auto origin = m_ctx->source().get_file(src_idx).getSource();
        auto fname = m_ctx->source().get_file(src_idx).getFilename();

        auto begin_lc = m_ctx->source().line_column(range.begin);
        out << fname << ":" << begin_lc.line << ":" << begin_lc.column << ": " << severityToString(sev) << ": " << msg << "\n";

        const auto data = origin.data();
        const auto size = origin.size();
        const auto offset = range.begin.offset();

        if (offset < static_cast<uint32_t>(size) && offset > 0) {
            const char* line_start = data + offset - 1;
            while (line_start > data && line_start[-1] != '\n')
                --line_start;
            const char* line_end = line_start;
            while (*line_end && *line_end != '\n')
                ++line_end;

            std::string_view line_text(line_start, line_end - line_start);
            out << "  " << line_text << "\n";

            // Рендер caret: для однострочных range — подчёркивание ~ от begin до end.
            // Для многострочных — только ^ на begin.
            if (!range.is_point()) {
                auto end_lc = m_ctx->source().line_column(range.end);
                if (end_lc.line == begin_lc.line) {
                    int start_spaces = begin_lc.column - 1;
                    int underline_len = std::max<int>(1, static_cast<int>(end_lc.column - begin_lc.column));
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

    // Fatal — прерываем выполнение после вывода диагностики.
    if (sev == Severity::Fatal) {
        throw FatalError(std::string(msg));
    }

    return &entry;
}

void DiagnosticEngine::fixit(DiagnosticEntry* entry, MapperRange range, std::string_view replacement) {
    if (!entry)
        return;

    // Консольный вывод (как было)
    auto fname = m_ctx->source().get_file(range.begin.fileIdx()).getFilename();
    auto lc = m_ctx->source().line_column(range.begin);
    auto& out = errs();
    out << fname << ":" << lc.line << ":" << lc.column << ": ";
    out << "note: fix-it: replace with '" << replacement << "'\n";

    // Прикрепляем fixit к переданной диагностике
    entry->fixits.push_back(FixitSuggestion{range, std::string(replacement)});
}

} // namespace trust