#pragma once

// include/types/promotion.hpp
// Продвижение арифметических типов (TypeId-aware).
// Единый источник правил продвижения числовых типов для анализатора выражений.
// Работает с TypeId: канонизирует цепочки алиасов через TypeRegistry и учитывает
// группу и разрядность (getData) типа. Операторная семантика (Compare/Logical → Bool,
// '//'/'//=' → Int64, std::any-операнды) живёт в semantic/type_inference.hpp и
// делегирует числовую часть сюда.

#include <algorithm>
#include <cstdint>

#include "types/group.hpp"
#include "types/registry.hpp"
#include "types/type_id.hpp"
#include "types/type_names.hpp"
#include "types/typekind.hpp"

namespace trust {

// -- Имя float-типа по разрядности (data) ----------------
// 16 → Float16, 32 → Float32, прочие (в т.ч. 64/неизвестная) → Float64.
inline TypeId floatTypeForData(const TypeRegistry& reg, uint8_t data) {
    switch (data) {
    case 16:
        return reg.getType(type::Float16);
    case 32:
        return reg.getType(type::Float32);
    case 64:
    default:
        return reg.getType(type::Float64);
    }
}

// -- Продвижение одиночного конкретного числового типа (для std::any-операнда) --
// Малые целые → Int32 (C++ int), 64-битные → Int64; float остаётся собой;
// беззнаковые → UInt32/UInt64. Не-числовой/неизвестный → INVALID_TYPE_ID.
inline TypeId promoteSingleNumeric(const TypeRegistry& reg, TypeId id) {
    const TypeKind k = getKindFromId(reg.getCanonicalTypeId(id));
    const Group g = getGroup(k);
    if (g == Group::kNumbers) {
        return reg.getCanonicalTypeId(id);
    }
    if (g == Group::kIntegers) {
        return getData(k) >= 64 ? reg.getType(type::Int64) : reg.getType(type::Int32);
    }
    if (g == Group::kUnsigned) {
        return getData(k) >= 64 ? reg.getType(type::UInt64) : reg.getType(type::UInt32);
    }
    return INVALID_TYPE_ID;
}

// -- Общий арифметический тип двух операндов (usual arithmetic conversions C++) --
// Присутствие float-операнда → более широкая float-группа; иначе целые:
// при 64-битном операнде → Int64, иначе малые продвигаются к int → Int32.
// Операнды должны быть числами (Integers/Unsigned/Numbers); иначе → INVALID_TYPE_ID.
inline TypeId commonArithmeticType(const TypeRegistry& reg, TypeId lhs, TypeId rhs) {
    const TypeId lc = reg.getCanonicalTypeId(lhs);
    const TypeId rc = reg.getCanonicalTypeId(rhs);
    const Group lg = getGroup(getKindFromId(lc));
    const Group rg = getGroup(getKindFromId(rc));

    const bool lFloat = (lg == Group::kNumbers);
    const bool rFloat = (rg == Group::kNumbers);
    if (lFloat || rFloat) {
        const uint8_t data = std::max(lFloat ? getData(getKindFromId(lc)) : 0, rFloat ? getData(getKindFromId(rc)) : 0);
        return floatTypeForData(reg, data);
    }

    const uint8_t lw = getData(getKindFromId(lc));
    const uint8_t rw = getData(getKindFromId(rc));
    if (lw >= 64 || rw >= 64) {
        return reg.getType(type::Int64);
    }
    return reg.getType(type::Int32);
}

// -- Тип результата арифметики для группы произвольной точности (BigInteger/Rational) --
// BigInteger/Rational - кастомные runtime-типы и НЕ участвуют в C++ usual arithmetic
// conversions (isArithmeticGroup), поэтому отдельная ветка. Правила:
//   * BigInteger op BigInteger → BigInteger; Rational op Rational → Rational;
//   * BigInteger op Rational → Rational (Rational "шире" - Rational построен на BigInteger);
//   * X op машинное целое (Integers/Unsigned) → X (машинное целое точно входит в X);
//   * X op float (Numbers) → НЕДОПУСТИМО без явного каста → INVALID_TYPE_ID (диагностика
//     у вызывающего: «cannot mix BigInteger/Rational with a floating-point number»);
//   * не-числовые/несводимые операнды → INVALID_TYPE_ID.
inline TypeId arbitraryPrecisionArithmeticType(const TypeRegistry& reg, TypeId lhs, TypeId rhs) {
    const TypeId lc = reg.getCanonicalTypeId(lhs);
    const TypeId rc = reg.getCanonicalTypeId(rhs);
    const TypeId rat = reg.getCanonicalTypeId(reg.getType(type::Rational));
    // «Уровень точности»: Rational(2) > BigInteger(1) > машинное целое(0); float/не-число = -1.
    auto level = [&](const TypeId id) -> int {
        const Group g = getGroup(getKindFromId(id));
        if (g == Group::kArbitraryPrecision) {
            return reg.getCanonicalTypeId(id) == rat ? 2 : 1;
        }
        if (g == Group::kIntegers || g == Group::kUnsigned) {
            return 0;
        }
        return -1; // float (Numbers) или не-числовой → смешивание запрещено без явного каста
    };
    const int ll = level(lc);
    const int rl = level(rc);
    if (ll < 0 || rl < 0) {
        return INVALID_TYPE_ID;
    }
    return (ll >= rl) ? lc : rc;
}

} // namespace trust
