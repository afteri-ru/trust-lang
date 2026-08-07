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
    M(Embed, "embed", Warning)                  \
    M(NoSigil, "sigil", Warning)                \
    M(Format, "format", Error)                  \
    M(WidenAny, "widen-any", Warning)           \
    M(All, "all", Warning)
#endif

// X-macro для булевых feature-флагов (НЕ severity-диагностик): формат M(EnumName, "cli-name").
// Используется для опций кодогенерации (например подавление комментариев в C++-выводе) и
// для опциональных уровней анализа (переключаются через pass-менеджер семантики).
// Флаг может нести необязательное строковое значение: -W<flag>=<value> (как в clang).
// Переопределите OPTIONS_FLAGS до включения заголовка, чтобы добавить свои флаги.
#ifndef OPTIONS_FLAGS
#define OPTIONS_FLAGS(M)    \
    M(Comments, "comments") \
    M(Lint, "lint")         \
    M(Effect, "effect")     \
    M(Trust, "trust")       \
    M(Extended, "extended") \
    M(Symbols, "symbols")   \
    M(Assert, "assert")     \
    M(Backtrace, "backtrace")

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

/// Булев feature-флаг (не severity-диагностика), генерируется из OPTIONS_FLAGS.
enum class FlagKind : int {
#define FLAG_ENUM(name, cli) name,
    OPTIONS_FLAGS(FLAG_ENUM)
#undef FLAG_ENUM
};

constexpr std::string_view FlagName(FlagKind k) {
    switch (k) {
#define FLAG_CASE(name, cli) \
    case FlagKind::name:     \
        return cli;
        OPTIONS_FLAGS(FLAG_CASE)
#undef FLAG_CASE
    }
    return {};
}

struct OptionInitInfo {
    OptKind kind;
    Severity severity;
};

class Options {
  public:
    explicit Options(DiagnosticEngine& diag);
    Options();

    void add_option(OptKind kind, std::optional<Severity> default_severity = std::nullopt);

    // ── Булевые feature-флаги (см. OPTIONS_FLAGS) ──
    /// Регистрирует feature-флаг (по умолчанию выключен).
    void register_flag(FlagKind kind);
    /// Проверяет, является ли cli-имя флагом (а не severity-опцией).
    [[nodiscard]] bool is_flag(std::string_view name) const;
    /// Текущее состояние флага (незарегистрированный = false).
    [[nodiscard]] bool is_enabled(FlagKind kind) const;
    /// Текущее состояние флага по cli-имени (незарегистрированный = false).
    [[nodiscard]] bool is_enabled(std::string_view name) const;
    void set_enabled(FlagKind kind, bool enabled);
    /// Включить флаг по cli-имени; false, если флаг не найден.
    bool set_enabled(std::string_view name, bool enabled);
    /// Строковое значение флага (nullopt, если не задано).
    [[nodiscard]] std::optional<std::string_view> flag_value(FlagKind kind) const;
    /// Установить значение флага по id (неявно включает флаг).
    void set_flag_value(FlagKind kind, std::string_view value);
    /// Установить значение флага по cli-имени; false, если флаг не найден.
    bool set_flag_value(std::string_view name, std::string_view value);

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

    /// Запись булевого feature-флага: вкл/выкл + необязательное строковое значение.
    struct FlagEntry {
        bool enabled = false;
        std::optional<std::string> value;
    };

    /// Дельта изменения флага для отката push/pop.
    struct FlagDelta {
        FlagKind kind;
        bool previous_enabled;
        std::optional<std::string> previous_value;
    };

    std::unordered_map<OptKind, OptionEntry> by_kind_;
    std::unordered_map<std::string, OptKind> name_to_kind_;
    std::unordered_map<FlagKind, FlagEntry> flags_;
    std::unordered_map<std::string, FlagKind> flag_name_to_kind_;
    std::stack<std::vector<OptionDelta>> history_;
    std::stack<std::vector<FlagDelta>> flag_history_;
    DiagnosticEngine* m_diag = nullptr;
};

} // namespace trust