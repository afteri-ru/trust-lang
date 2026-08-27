#pragma once

// include/diag/options_decl.hpp
// Декларации enum-имён, общих для опций диагностик: категории для справки (DiagGroup) и
// группы-агрегаты (WarnGroup). Вынесены из options.hpp в отдельный лёгкий заголовок, чтобы
// пер-компонентные таблицы опций (TRUST_DIAG_SET/TRUST_FLAG_SET) могли использовать эти имена
// БЕЗ включения options.hpp (и без цикла include: options.hpp включает пер-компонентные
// декларации). Здесь только типы/константы, без класса Options.

#include "diag/severity.hpp"

#include <string_view>

namespace trust {

// -- Категории для справки `-Whelp` (feature-флаги и severity-диагностики). --
enum class DiagGroup {
    Diagnostics, ///< severity-диагностики
    Analysis,    ///< флаги опциональных анализаторов (lint/effect/trust/...)
    Codegen,     ///< флаги кодогенерации (comments/assert/backtrace/...)
};

constexpr std::string_view diagGroupName(DiagGroup g) {
    switch (g) {
    case DiagGroup::Diagnostics:
        return "Diagnostics";
    case DiagGroup::Analysis:
        return "Analysis flags";
    case DiagGroup::Codegen:
        return "Codegen flags";
    }
    return {};
}

// -- Предопределённые ГРУППЫ-агрегаты (общая регистрация, стиль clang). --
// Центрально определены только агрегаты; отдельные диагностики при регистрации привязываются
// к ОДНОЙ ИЛИ НЕСКОЛЬКИМ группам (битовая маска WarnGroup). `-Wall`/`-Wextra`/`-Wpedantic`/
// `-W<group>` включают все диагностики соответствующей группы.
// X-macro: M(EnumSuffix, "cli-suffix", "DisplayName", bit). Единый источник истины для
// групп-агрегатов: из него генерируются enum WarnGroup, конвертации имени/CLI-суффикса
// и список групп.
#ifndef WARN_GROUPS
#define WARN_GROUPS(M)                             \
    M(Wall, "all", "Wall", 0)                      \
    M(Wextra, "extra", "Wextra", 1)                \
    M(Wpedantic, "pedantic", "Wpedantic", 2)       \
    M(Wunused, "unused", "Wunused", 3)             \
    M(Wdeprecated, "deprecated", "Wdeprecated", 4) \
    M(Wformat, "format", "Wformat", 5)             \
    M(Wconversion, "conversion", "Wconversion", 6)
#endif

enum WarnGroup : unsigned {
    WG_None = 0,
#define WARN_GROUP_ENUM(name, cli, disp, bit) WG_##name = 1u << bit,
    WARN_GROUPS(WARN_GROUP_ENUM)
#undef WARN_GROUP_ENUM
};

inline constexpr WarnGroup operator|(WarnGroup a, WarnGroup b) {
    return static_cast<WarnGroup>(static_cast<unsigned>(a) | static_cast<unsigned>(b));
}
inline constexpr WarnGroup operator&(WarnGroup a, WarnGroup b) {
    return static_cast<WarnGroup>(static_cast<unsigned>(a) & static_cast<unsigned>(b));
}

/// Имя группы для справки ("Wall", "Wunused", ...), генерируется из WARN_GROUPS.
constexpr std::string_view warnGroupName(WarnGroup g) {
    switch (g) {
#define WARN_GROUP_NAME(name, cli, disp, bit) \
    case WG_##name:                           \
        return disp;
        WARN_GROUPS(WARN_GROUP_NAME)
#undef WARN_GROUP_NAME
    case WG_None:
        return {};
    }
    return {};
}

/// CLI-суффикс группы (после -W): "all" -> -Wall, "unused" -> -Wunused, ...
constexpr std::string_view warnGroupCli(WarnGroup g) {
    switch (g) {
#define WARN_GROUP_CLI(name, cli, disp, bit) \
    case WG_##name:                          \
        return cli;
        WARN_GROUPS(WARN_GROUP_CLI)
#undef WARN_GROUP_CLI
    case WG_None:
        return {};
    }
    return {};
}

/// Все предопределённые группы (для итерации в warnGroupFromCli/справке).
constexpr WarnGroup kAllWarnGroups[] = {
#define WARN_GROUP_ALL(name, cli, disp, bit) WG_##name,
    WARN_GROUPS(WARN_GROUP_ALL)
#undef WARN_GROUP_ALL
};

} // namespace trust
