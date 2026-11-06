#include "diag/diag.hpp"

#include "utils/error.hpp" // EXPECT — инвариант стека контекста (диагностика обязана дать диагностику)

// Текст исходной строки приходит через SourceTextProvider (внедряет `session`): зависимости
// diag->session нет (см. component_deps_guard). Рендеринг вынесен в сменный форматтер
// (diag/serialize): движок фильтрует/считает/накапливает, печатает форматтер.

#include <format>

namespace trust {

// Единственный владелец форматтера: по умолчанию — text (проекция той же DiagnosticEntry).
DiagnosticEngine::DiagnosticEngine()
: m_formatter(makeDiagnosticFormatter(DiagnosticsFormat::Text)) {
}

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
    m_contextStack.clear();
}

void DiagnosticEngine::pushContext(MapperRange range, std::string label) {
    m_contextStack.push_back(ContextFrame{range, std::move(label)});
}

void DiagnosticEngine::popContext() {
    // Стек снимается только парным ContextScope; пустой стек — нарушение инварианта владельца (R30).
    EXPECT(!m_contextStack.empty() && "DiagnosticEngine::popContext: no matching ContextScope");
    m_contextStack.pop_back();
}

// Маппинг severity → строка — единый источник severityName (диагностики/severity.hpp); используется
// форматтерами напрямую (в т.ч. TextFormatter), отдельного switch нет.

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

    // Сохраняем диагностику для последующего извлечения. Указатель стабилен: `m_diagnostics` —
    // deque, а рекурсивный output() добавляет Note-кадры в конец.
    m_diagnostics.push_back({range, sev, opt_name, std::string(msg), {}});
    DiagnosticEntry* entry = &m_diagnostics.back();

    // Рендеринг: форматтер (text по умолчанию / json / sarif) — проекция той же записи
    // DiagnosticEntry; второй модели данных (отдельного текстового пути) нет.
    m_formatter->write(*entry, m_source);

    // Цепочка контекста (R30/R50): сначала динамические кадры (inner→outer), затем макро-кадры из пары
    // `use → definition` владельца карты (порт, R34). Note-записи той же модели; m_emittingChain — рекурсия.
    if (!internal && !m_emittingChain && sev != Severity::Note) {
        m_emittingChain = true;
        for (auto it = m_contextStack.rbegin(); it != m_contextStack.rend(); ++it) {
            output(Severity::Note, it->range, {}, it->label, /*internal=*/true);
        }
        if (m_source != nullptr && !range.begin.isInvalid()) {
            const std::vector<ExpansionFrame> frames = m_source->expansionChain(range);
            for (const ExpansionFrame& frame : frames) {
                const std::string label = frame.name.empty() ? std::string("in expansion of macro") : std::format("in expansion of macro '{}'", frame.name);
                output(Severity::Note, frame.origin, {}, label, /*internal=*/true);
            }
        }
        m_emittingChain = false;
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

    return entry;
}

void DiagnosticEngine::fixit(DiagnosticEntry* entry, MapperRange range, std::string_view replacement) {
    if (!entry) {
        return;
    }

    // Прикрепляем fixit к переданной диагностике (структурированные форматы сериализуют его).
    entry->fixits.push_back(FixitSuggestion{range, std::string(replacement)});
    // Текстовая пометка консоли — только у text-форматтера (для json/sarif — no-op).
    m_formatter->writeFixitNote(entry->fixits.back(), m_source);
}

} // namespace trust