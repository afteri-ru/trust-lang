#pragma once

// include/ast/trust_prop.hpp
// Типы trust-контрактов (единая форма `@{ [kind:] expr @}`). Единый источник: один список
// порождает enum PropertyKind, имя (propertyKindName) и обратный разбор (parsePropertyKind) -
// по образцу SOLVER_OPERATOR_LIST (solver/smt_op.hpp) и SOLVER_MODE_LIST (semantic/solver.hpp).
// kind может задаваться явно (в маркере `kind:`) либо выводиться из места привязки
// (цикл->Invariant, тип->Type, переменная/автономный->Assert; функция без явного kind -> ошибка).

#include <optional>
#include <string_view>

// -- Единый список типов trust-контрактов: имя (enum) + строковый вид (в маркере) --
#define TRUST_PROP_KIND(M)    \
    M(Pre, "pre")             \
    M(Post, "post")           \
    M(Assert, "check")        \
    M(Invariant, "invariant") \
    M(Type, "type")

namespace trust {

#define TRUST_PROP_ENUM(name, str) name,
enum class PropertyKind { TRUST_PROP_KIND(TRUST_PROP_ENUM) kUnknown };
#undef TRUST_PROP_ENUM

#define TRUST_PROP_NAME(name, str) str,
inline constexpr std::string_view kPropertyKindNames[] = {TRUST_PROP_KIND(TRUST_PROP_NAME)};
#undef TRUST_PROP_NAME

inline constexpr std::size_t kPropertyKindCount = sizeof(kPropertyKindNames) / sizeof(kPropertyKindNames[0]);

static_assert(static_cast<std::size_t>(PropertyKind::kUnknown) == kPropertyKindCount, "TRUST_PROP_KIND and kPropertyKindNames must stay in sync");

/// Имя типа контракта (для диагностик/печати). Известное значение обязательно.
[[nodiscard]] inline constexpr std::string_view propertyKindName(PropertyKind k) noexcept {
    const int idx = static_cast<int>(k);
    return (idx >= 0 && idx < static_cast<int>(kPropertyKindCount)) ? kPropertyKindNames[idx] : "unknown";
}

/// Разбор строкового имени типа контракта. Неизвестное имя (в т.ч. обычный идентификатор) - nullopt.
[[nodiscard]] inline constexpr std::optional<PropertyKind> parsePropertyKind(std::string_view v) noexcept {
    for (std::size_t i = 0; i < kPropertyKindCount; ++i) {
        if (kPropertyKindNames[i] == v) {
            return static_cast<PropertyKind>(i);
        }
    }
    return std::nullopt;
}

} // namespace trust

#undef TRUST_PROP_KIND