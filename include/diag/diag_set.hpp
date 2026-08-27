#pragma once

// include/diag/diag_set.hpp
// ЕДИНСТВЕННОЕ определение переиспользуемого механизма «набора опций».
// Каждая компонента объявляет СВОИ диагностики/флаги в своём заголовке через
// TRUST_DIAG_SET / TRUST_FLAG_SET (пер-компонентный единственный источник данных).
// Макрос генерирует enum и ADL-доступы (diagName/diagHelp/diagDefaultSeverity/
// diagWarnGroups/diagCategory и flagName/flagHelp/flagCategory) в указанном namespace NS.
// Сам макрос определён здесь ОДИН раз и переиспользуется всеми компонентами — ничего не
// повторяется вручную. Options::add<T> / report<T> в diag используют эти доступы через ADL,
// поэтому diag остаётся листом (не включает заголовки компонентов).

#include "diag/options_decl.hpp"

#include <string_view>

namespace trust {

/// Дескриптор severity-диагностики (генерируется TRUST_DIAG_SET из данных компоненты).
struct DiagDesc {
    std::string_view name;
    std::string_view help;
    Severity default_severity;
    WarnGroup warn_groups;
    DiagGroup category;
};

/// Дескриптор feature-флага (генерируется TRUST_FLAG_SET из данных компоненты).
struct FlagDesc {
    std::string_view name;
    std::string_view help;
    DiagGroup category;
};

} // namespace trust

// Внутренние helper-макросы (определяются один раз, переиспользуются любым набором).
// Формат строки TRUST_DIAG_SET: M(EnumName, "cli", SevName, "help", WarnGroupMask).
#define TRUST_DG_ENUM(name, cli, sev, help, grp) name,
#define TRUST_DG_DESC(name, cli, sev, help, grp) {cli, help, Severity::sev, grp, DiagGroup::Diagnostics},

// Формат строки TRUST_FLAG_SET: M(EnumName, "cli", "help", DiagGroup).
#define TRUST_FL_ENUM(name, cli, help, grp) name,
#define TRUST_FL_DESC(name, cli, help, grp) {cli, help, grp},

// TRUST_DIAG_SET(NS, ENUM, LIST): в namespace NS генерирует enum ENUM и ADL-доступы
// diagName/diagHelp/diagDefaultSeverity/diagWarnGroups/diagCategory из пер-компонентного LIST.
#define TRUST_DIAG_SET(NS, ENUM, LIST)                                  \
    namespace NS {                                                      \
    enum class ENUM : int { LIST(TRUST_DG_ENUM) };                      \
    inline constexpr DiagDesc k##ENUM##Descs[] = {LIST(TRUST_DG_DESC)}; \
    constexpr std::string_view diagName(ENUM k) {                       \
        return k##ENUM##Descs[static_cast<int>(k)].name;                \
    }                                                                   \
    constexpr std::string_view diagHelp(ENUM k) {                       \
        return k##ENUM##Descs[static_cast<int>(k)].help;                \
    }                                                                   \
    constexpr Severity diagDefaultSeverity(ENUM k) {                    \
        return k##ENUM##Descs[static_cast<int>(k)].default_severity;    \
    }                                                                   \
    constexpr WarnGroup diagWarnGroups(ENUM k) {                        \
        return k##ENUM##Descs[static_cast<int>(k)].warn_groups;         \
    }                                                                   \
    constexpr DiagGroup diagCategory(ENUM) {                            \
        return DiagGroup::Diagnostics;                                  \
    }                                                                   \
    }

// TRUST_FLAG_SET(NS, ENUM, LIST): в namespace NS генерирует enum ENUM и ADL-доступы
// flagName/flagHelp/flagCategory из пер-компонентного LIST.
#define TRUST_FLAG_SET(NS, ENUM, LIST)                                  \
    namespace NS {                                                      \
    enum class ENUM : int { LIST(TRUST_FL_ENUM) };                      \
    inline constexpr FlagDesc k##ENUM##Descs[] = {LIST(TRUST_FL_DESC)}; \
    constexpr std::string_view flagName(ENUM k) {                       \
        return k##ENUM##Descs[static_cast<int>(k)].name;                \
    }                                                                   \
    constexpr std::string_view flagHelp(ENUM k) {                       \
        return k##ENUM##Descs[static_cast<int>(k)].help;                \
    }                                                                   \
    constexpr DiagGroup flagCategory(ENUM k) {                          \
        return k##ENUM##Descs[static_cast<int>(k)].category;            \
    }                                                                   \
    }
