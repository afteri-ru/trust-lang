#pragma once

#include "diag/severity.hpp"

#include <algorithm>
#include <functional>
#include <initializer_list>
#include <optional>
#include <span>
#include <stack>
#include <string>
#include <string_view>
#include <unordered_map>
#include <vector>

namespace trust {

// X-macro для compile-time определения опций.
// Формат: M(EnumName, "cli-name", DefaultSeverityName)
// DefaultSeverityName — имя Severity без префикса (например Warning, Error).
// Переопределите OPTIONS_LIST до включения заголовка, чтобы добавить свои опции.
#ifndef OPTIONS_LIST
#define OPTIONS_LIST(M)                         \
    M(UnusedVar, "unused-var", Warning)         \
    M(Deprecated, "deprecated", Warning)        \
    M(ParseError, "parse-error", Error)         \
    M(MacroRedefined, "macro-redefined", Fatal) \
    M(All, "all", Warning)
#endif

class DiagnosticEngine;

enum class OptKind : int {
#define OPT_ENUM(name, str, sev) name,
    OPTIONS_LIST(OPT_ENUM)
#undef OPT_ENUM
};

constexpr std::string_view OptName(OptKind k) {
    switch (k) {
#define OPT_CASE(name, str, sev) \
    case OptKind::name:          \
        return str;
        OPTIONS_LIST(OPT_CASE)
#undef OPT_CASE
    }
    return {};
}

constexpr Severity OptDefaultSeverity(OptKind k) {
    switch (k) {
#define OPT_CASE(name, str, sev) \
    case OptKind::name:          \
        return Severity::sev;
        OPTIONS_LIST(OPT_CASE)
#undef OPT_CASE
    }
    return Severity::Warning;
}

static constexpr int NumOptions = static_cast<int>(OptKind::All) + 1;

struct OptionInitInfo {
    OptKind kind;
    Severity severity;
};

class Options {
  public:
    explicit Options(DiagnosticEngine& diag);
    Options();

    void add_option(OptKind kind, std::optional<Severity> default_severity = std::nullopt);

    void set(OptKind kind, std::optional<Severity> severity);
    void set(std::string_view name, std::optional<Severity> severity);

    std::optional<Severity> get(OptKind kind) const;
    std::optional<Severity> get(std::string_view name) const;

    std::span<char*> parse_argv(std::span<char*> argv);

    void push();
    void pop();

    [[nodiscard]] std::optional<Severity> severity(OptKind kind) const;
    [[nodiscard]] std::optional<Severity> severity(std::string_view name) const;

    [[nodiscard]] bool is_registered(OptKind kind) const;
    [[nodiscard]] bool is_registered(std::string_view name) const;

    [[nodiscard]] std::string_view name(OptKind kind) const;

    static Options make(std::initializer_list<OptionInitInfo> opts);

    /// Парсит строковое имя severity ("fatal", "error", "warning", "remark", "note", "ignore").
    /// Для "ignore" возвращает nullopt.
    [[nodiscard]] static std::optional<Severity> parseSeverityName(std::string_view name) noexcept;

  private:
    struct OptionEntry {
        OptKind kind;
        std::optional<Severity> severity;
        std::string_view name;
    };

    struct OptionDelta {
        OptKind kind;
        std::optional<Severity> previous_severity;
    };

    std::unordered_map<OptKind, OptionEntry> by_kind_;
    std::unordered_map<std::string, OptKind> name_to_kind_;
    std::stack<std::vector<OptionDelta>> history_;
    DiagnosticEngine* m_diag = nullptr;
};

} // namespace trust