#include "diag/diag.hpp"
// Текст исходной строки приходит через SourceTextProvider (внедряет `session`): зависимости
// diag->session нет (см. component_deps_guard).

#include "utils/error.hpp"
#include "utils/utils.hpp"

#include <format>
#include <iostream>

namespace trust {

// output() - ядро вывода: формат file:line:col + исходная строка + caret (^~~).
// Для non-point диапазонов на той же строке рисует подчёркивание.
// Также выполняет фильтрацию: Fatal не фильтруется, опции и minSeverity применяются
// к остальным severity.

void DiagnosticEngine::setMinSeverity(Severity sev) {
    m_minSeverity = sev;
}

Severity DiagnosticEngine::minSeverity() const {
    return m_minSeverity;
}

void DiagnosticEngine::setErrorLimit(std::size_t limit) {
    m_errorLimit = limit;
    // Смена политики: итоговое сообщение для нового лимита выдаётся заново.
    m_errorLimitReported = false;
}

std::size_t DiagnosticEngine::errorLimit() const {
    return m_errorLimit;
}

bool DiagnosticEngine::errorLimitReached() const {
    return m_errorLimit != 0 && static_cast<std::size_t>(m_errorCount) >= m_errorLimit;
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
    m_errorLimitReported = false;
    m_diagnostics.clear();
}

// severityToString - маппинг enum → строка, генерируется из x-macro SEVERITIES (severityName);
// отдельный switch не нужен (источник имён один - severity.hpp).
static const char* severityToString(Severity sev) {
    const std::string_view n = severityName(sev);
    return n.empty() ? "unknown" : n.data();
}

DiagnosticEntry* DiagnosticEngine::output(Severity sev, MapperRange range, std::string_view opt_name, std::string_view msg, bool internal) {
    // Ignore - не диагностика: не выводится и не сохраняется (страховка; уровень игнора
    // обычно отсекается раньше - в Options::get / report с привязкой к опции).
    if (sev == Severity::Ignore) {
        return nullptr;
    }
    // После достижения лимита ошибок дальнейшие не-Fatal диагностики подавляются.
    // Итоговое сообщение (internal) выводится в обход этого ограничения.
    if (!internal && sev != Severity::Fatal && errorLimitReached()) {
        if (!m_errorLimitReported) {
            m_errorLimitReported = true;
            output(Severity::Note, range, {}, std::format("too many errors emitted ({}); stopping now", m_errorLimit), /*internal=*/true);
        }
        return nullptr;
    }
    // Fatal - не фильтруется ни опциями, ни minSeverity: всегда выводится и прерывает выполнение.
    if (!internal && sev != Severity::Fatal) {
        // Если опция задана (opt_name не пусто) и есть Options - проверяем severity через Options.
        if (m_opts && !opt_name.empty()) {
            const Severity opt_sev = m_opts->getByName(opt_name);
            if (opt_sev == Severity::Ignore) {
                // opt = "ignore" - не выводим диагностику
                return nullptr;
            }
            sev = opt_sev;
        }

        if (sev < m_minSeverity) {
            return nullptr;
        }
    }

    // Счётчики увеличиваются до фильтрации - это корректно, т.к. фильтрация на уровне do_report().
    if (sev == Severity::Error) {
        m_errorCount++;
    }
    if (sev == Severity::Warning) {
        m_warningCount++;
    }

    // Сохраняем диагностику для последующего извлечения
    m_diagnostics.push_back({range, sev, opt_name, std::string(msg), {}});
    DiagnosticEntry& entry = m_diagnostics.back();

    std::ostream& out = errs();

    // Путь с локацией: печатаем file:line:col, строку кода и caret-подчёркивание.
    if (!range.begin.isInvalid() && m_source != nullptr && range.begin.offset() > 0) {
        const std::string_view origin = m_source->fileContents(range.begin);
        const std::string_view fname = m_source->fileName(range.begin);

        const SourceTextProvider::LineColumn begin_lc = m_source->lineColumn(range.begin);
        out << fname << ":" << begin_lc.line << ":" << begin_lc.column << ": " << severityToString(sev) << ": " << msg << "\n";

        const auto data = origin.data();
        const auto size = origin.size();
        const auto offset = range.begin.offset();

        if (offset < static_cast<uint32_t>(size) && offset > 0) {
            const char* line_start = data + offset - 1;
            while (line_start > data && line_start[-1] != '\n') {
                --line_start;
            }
            const char* line_end = line_start;
            while (*line_end && *line_end != '\n') {
                ++line_end;
            }

            std::string_view line_text(line_start, line_end - line_start);
            out << "  " << line_text << "\n";

            // Рендер caret: для однострочных range - подчёркивание ~ от begin до end.
            // Для многострочных - только ^ на begin.
            if (!range.is_point()) {
                const SourceTextProvider::LineColumn end_lc = m_source->lineColumn(range.end);
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

        // Без валидной локации - только severity: message.
    } else {
        out << severityToString(sev) << ": " << msg << "\n";
    }

    // Достижение лимита ошибок: ровно одно итоговое сообщение (в обход фильтров), после
    // чего output() подавляет все не-Fatal диагностики (см. проверку в начале).
    if (!internal && sev == Severity::Error && m_errorLimit != 0 && static_cast<std::size_t>(m_errorCount) == m_errorLimit && !m_errorLimitReported) {
        m_errorLimitReported = true;
        output(Severity::Note, range, {}, std::format("too many errors emitted ({}); stopping now", m_errorLimit), /*internal=*/true);
    }

    // Fatal - прерываем выполнение после вывода диагностики.
    if (sev == Severity::Fatal) {
        throw FatalError(std::string(msg));
    }

    return &entry;
}

void DiagnosticEngine::fixit(DiagnosticEntry* entry, MapperRange range, std::string_view replacement) {
    if (!entry) {
        return;
    }

    // Консольный вывод fixit-диагностики
    EXPECT(m_source != nullptr && "fixit requires a source text provider");
    const std::string_view fname = m_source->fileName(range.begin);
    const SourceTextProvider::LineColumn lc = m_source->lineColumn(range.begin);
    auto& out = errs();
    out << fname << ":" << lc.line << ":" << lc.column << ": ";
    out << "note: fix-it: replace with '" << replacement << "'\n";

    // Прикрепляем fixit к переданной диагностике
    entry->fixits.push_back(FixitSuggestion{range, std::string(replacement)});
}

} // namespace trust