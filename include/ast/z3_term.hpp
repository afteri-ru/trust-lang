#pragma once

// include/ast/z3_term.hpp
// Термины КОНКРЕТНОГО решателя (SMT/Z3) - операторы специального назначения внутри выражений
// trust-контрактов (`@{ [kind:] expr @}`). Это НЕ элементы языка: у другого решателя набор терминов
// может отличаться. Записываются маркером `@( term, args... @)` (пара лексем @(/@), симметрично
// @{/@}), первый аргумент - СТРОГО маркер термина из этого X-макроса. Мнемонические макросы
// (z3_old/z3_forall/...) - тонкие обёртки над `@( term, ... @)`.
// Единый источник: один список порождает enum Z3TermKind, имя (z3TermName) и обратный разбор
// (parseZ3Term) - по образцу TRUST_PROP_KIND (ast/trust_prop.hpp).

#include <cstddef>
#include <optional>
#include <string_view>

// -- Единый список терминов решателя: имя (enum) + строковый маркер + арность --
#define SMT_Z3_TERM_LIST(M) \
    M(Old, "old", 1)        \
    M(Forall, "forall", 2)  \
    M(Exists, "exists", 2)  \
    M(Fresh, "fresh", 1)    \
    M(Length, "length", 1)  \
    M(Result, "result", 0)  \
    M(Unroll, "unroll", 1)

namespace trust {

#define Z3_TERM_ENUM(name, str, arity) name,
enum class Z3TermKind { SMT_Z3_TERM_LIST(Z3_TERM_ENUM) kUnknown };
#undef Z3_TERM_ENUM

#define Z3_TERM_NAME(name, str, arity) str,
inline constexpr std::string_view kZ3TermNames[] = {SMT_Z3_TERM_LIST(Z3_TERM_NAME)};
#undef Z3_TERM_NAME

inline constexpr std::size_t kZ3TermCount = sizeof(kZ3TermNames) / sizeof(kZ3TermNames[0]);

/// Арность термина (число аргументов после маркера): Old=1, Forall=2, Result=0, ...
[[nodiscard]] inline constexpr int z3TermArity(Z3TermKind e) noexcept {
    switch (e) {
#define Z3_TERM_ARITY(name, str, arity) \
    case Z3TermKind::name:              \
        return arity;
        SMT_Z3_TERM_LIST(Z3_TERM_ARITY)
#undef Z3_TERM_ARITY
    default:
        return -1;
    }
}

static_assert(static_cast<std::size_t>(Z3TermKind::kUnknown) == kZ3TermCount, "SMT_Z3_TERM_LIST and kZ3TermNames must stay in sync");

/// Имя термина (для диагностик/печати). Известное значение обязательно.
[[nodiscard]] inline constexpr std::string_view z3TermName(Z3TermKind e) noexcept {
    const int idx = static_cast<int>(e);
    return (idx >= 0 && idx < static_cast<int>(kZ3TermCount)) ? kZ3TermNames[idx] : "unknown";
}

/// Разбор строкового маркера термина. Неизвестное имя - nullopt.
[[nodiscard]] inline constexpr std::optional<Z3TermKind> parseZ3Term(std::string_view v) noexcept {
    for (std::size_t i = 0; i < kZ3TermCount; ++i) {
        if (kZ3TermNames[i] == v) {
            return static_cast<Z3TermKind>(i);
        }
    }
    return std::nullopt;
}

} // namespace trust

#undef SMT_Z3_TERM_LIST