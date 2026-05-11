#pragma once

#include <format>
#include <initializer_list>
#include <optional>
#include <ostream>
#include <string>
#include <string_view>
#include <vector>

#include "diag/location.hpp"

namespace trust {

enum class Severity : int {
    Remark,
    Note,
    Warning,
    Error,
    Fatal,
};

// ── Структура для хранения одной диагностики ──
struct DiagnosticEntry {
    MapperRange range;
    Severity severity;
    std::string message;
};

class Context;

class DiagnosticEngine {
  public:
    DiagnosticEngine() = default;
    virtual ~DiagnosticEngine() = default;
    DiagnosticEngine(const DiagnosticEngine&) = delete;
    DiagnosticEngine& operator=(const DiagnosticEngine&) = delete;

    void setSourceManager(const Context* ctx) { m_ctx = ctx; }

    void setMinSeverity(Severity sev);
    Severity minSeverity() const;

    // Вывод диагностики. Три перегрузки: строка, точка (MapperLocation), диапазон (MapperRange).
    void report(MapperRange range, Severity sev, std::string_view msg);

    template <typename... Args>
    void report(MapperLocation loc, Severity sev, std::format_string<Args...> fmt, Args&&... args) {
        report(MapperRange{loc, loc}, sev, std::format(fmt, std::forward<Args>(args)...));
    }

    template <typename... Args>
    void report(MapperRange range, Severity sev, std::format_string<Args...> fmt, Args&&... args) {
        report(range, sev, std::format(fmt, std::forward<Args>(args)...));
    }

    int errorCount() const;
    int warningCount() const;

    // Доступ к накопленным диагностикам
    const std::vector<DiagnosticEntry>& diagnostics() const { return m_diagnostics; }

    void setOutput(std::ostream* os);
    void clear();

  private:
    void output(MapperRange range, Severity sev, std::string_view msg);

    Severity m_minSeverity = Severity::Remark;
    int m_errorCount = 0;
    int m_warningCount = 0;
    std::ostream* m_output = nullptr;
    const Context* m_ctx = nullptr;
    std::vector<DiagnosticEntry> m_diagnostics; // накопленные диагностики
};

} // namespace trust
