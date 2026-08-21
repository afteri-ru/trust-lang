#pragma once

#include <format>
#include <initializer_list>
#include <optional>
#include <stdexcept>
#include <string>
#include <string_view>
#include <vector>

#include "location/location.hpp"
#include "diag/severity.hpp"
#include "diag/options.hpp"

namespace trust {

// -- Исключение для фатальных диагностик: бросается из report() при Severity::Fatal --
class FatalError : public std::runtime_error {
  public:
    explicit FatalError(const std::string& msg)
    : std::runtime_error(msg) {}
};

// -- Fixit-подсказка: автоматическое исправление --
struct FixitSuggestion {
    MapperRange range;       // что заменяем
    std::string replacement; // на что заменяем
};

// -- Структура для хранения одной диагностики --
struct DiagnosticEntry {
    MapperRange range;
    Severity severity;
    OptKind optKind{OptKind::All};
    std::string message;
    std::vector<FixitSuggestion> fixits; // привязанные fixit-подсказки
};

class Context;
class Options;

class DiagnosticEngine {
  public:
    DiagnosticEngine() = default;
    virtual ~DiagnosticEngine() = default;
    DiagnosticEngine(const DiagnosticEngine&) = delete;
    DiagnosticEngine& operator=(const DiagnosticEngine&) = delete;

    void setSourceManager(const Context* ctx) { m_ctx = ctx; }

    void setMinSeverity(Severity sev);
    Severity minSeverity() const;

    void setOptions(Options* opts) { m_opts = opts; }

    // -- Шаблоны с форматной строкой --
    /// С явным OptKind перед fmt. Возвращает nullptr, если диагностика подавлена.
    template <typename... Args>
    DiagnosticEntry* report(Severity sev, MapperRange range, OptKind opt, std::format_string<Args...> fmt, Args&&... args) {
        return output(sev, range, opt, std::format(fmt, std::forward<Args>(args)...));
    }

    /// Без OptKind - перенаправляет с OptKind::All
    template <typename... Args>
    DiagnosticEntry* report(Severity sev, MapperRange range, std::format_string<Args...> fmt, Args&&... args) {
        return output(sev, range, OptKind::All, std::format(fmt, std::forward<Args>(args)...));
    }

    /// С MapperLocation
    template <typename... Args>
    DiagnosticEntry* report(Severity sev, MapperLocation loc, std::format_string<Args...> fmt, Args&&... args) {
        return output(sev, MapperRange{loc, loc}, OptKind::All, std::format(fmt, std::forward<Args>(args)...));
    }

    // -- Fixit-подсказки --
    /// Зафиксировать fixit для указанной диагностики. entry может быть nullptr (тогда ничего не делается).
    void fixit(DiagnosticEntry* entry, MapperRange range, std::string_view replacement);

    int errorCount() const;
    int warningCount() const;

    // Доступ к накопленным диагностикам
    const std::vector<DiagnosticEntry>& diagnostics() const { return m_diagnostics; }

    void clear();

  private:
    DiagnosticEntry* output(Severity sev, MapperRange range, OptKind opt, std::string_view msg);

    Severity m_minSeverity = Severity::Remark;
    Options* m_opts = nullptr;
    int m_errorCount = 0;
    int m_warningCount = 0;
    const Context* m_ctx = nullptr;
    std::vector<DiagnosticEntry> m_diagnostics; // накопленные диагностики
};

} // namespace trust