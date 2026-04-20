#pragma once

#include <format>
#include <initializer_list>
#include <optional>
#include <ostream>
#include <string>
#include <string_view>

#include "diag/location.hpp"

namespace trust {

enum class Severity : int {
    Remark,
    Note,
    Warning,
    Error,
    Fatal,
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

    // Вывод диагностики. Три перегрузки: строка, точка (SourceLoc), диапазон (SourceRange).
    void report(SourceRange range, Severity sev, std::string_view msg);

    template <typename... Args>
    void report(SourceLoc loc, Severity sev,
                std::format_string<Args...> fmt, Args&&... args) {
        report(SourceRange{loc, loc}, sev, std::format(fmt, std::forward<Args>(args)...));
    }

    template <typename... Args>
    void report(SourceRange range, Severity sev,
                std::format_string<Args...> fmt, Args&&... args) {
        report(range, sev, std::format(fmt, std::forward<Args>(args)...));
    }

    int errorCount() const;
    int warningCount() const;

    void setOutput(std::ostream* os);
    void clear();

private:
    void output(SourceRange range, Severity sev, std::string_view msg);

    Severity m_minSeverity = Severity::Remark;
    int m_errorCount = 0;
    int m_warningCount = 0;
    std::ostream* m_output = nullptr;
    const Context* m_ctx = nullptr;
};

} // namespace trust