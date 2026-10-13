#pragma once

// include/semantic/verify/format_check.hpp
// Компиляйт-тайм проверка типов аргументов нативных функций на соответствие форматной
// строке printf (атрибут @[format("printf", string_index, first_to_check)] - GCC-аналог
// `__attribute__((format(...)))`). Содержит парсер printf-спецификаторов и сверку
// ожидаемой категории аргумента с фактическим типом.
//
// Использование (см. NameResolutionPass::typeExpr, case CallExpr):
//   vector<format_check::Conversion> convs;
//   if (format_check::parse_printf_format(fmt, convs)) {
//       for (i-я конверсия) if (!format_check::arg_matches_expect(types, argType, c.expect)) → diag
//   }

#include "types/type_read.hpp"
#include "types/type_id.hpp"
#include "types/typekind.hpp"

#include <cstdint>
#include <string_view>
#include <vector>

namespace trust {

class TypeSystem;

namespace format_check {

// Ожидаемая категория printf-аргумента (по спецификатору + length-модификатору).
/// Единый источник: X-макрос генерирует и enum, и читаемые имена (format_expect_name ниже).
#define TRUST_FORMAT_EXPECT_NAMES(O)            \
    /* %d %i (и %c) - целочисленный аргумент */ \
    O(Integer, "integer")                       \
    /* %u %o %x %X - беззнаковое целое */       \
    O(Unsigned, "unsigned integer")             \
    /* %f %e %g %a - плавающий */               \
    O(Float, "floating point")                  \
    /* %s - узкая строка (C-строка) */          \
    O(StrChar, "string")                        \
    /* %p (%n) - указатель */                   \
    O(Pointer, "pointer")

enum class Expect : uint8_t {
#define TRUST_FORMAT_EXPECT_ENUM(name, text) name,
    TRUST_FORMAT_EXPECT_NAMES(TRUST_FORMAT_EXPECT_ENUM)
#undef TRUST_FORMAT_EXPECT_ENUM
};

// Один printf-конверсионный спецификатор, потребляющий аргумент.
struct Conversion {
    Expect expect; ///< ожидаемая категория аргумента
    char conv;     ///< конверсионный символ (для диагностики)
};

/// Парсит printf-формат-строку и заполняет список конверсий в порядке потребления аргументов
/// (`%%` - литерал, аргумент не потребляет и в список не попадает). Возвращает true при
/// успехе; false - строка не является валидным printf-форматом (незакрытый '%' либо
/// неизвестный конверсионный символ).
[[nodiscard]] bool parse_printf_format(std::string_view fmt, std::vector<Conversion>& out);

/// Соответствует ли фактический тип аргумента ожидаемой категории printf-конверсии.
/// INVALID-тип (выражение без выведенного типа) не проверяется → true.
[[nodiscard]] bool arg_matches_expect(const TypeReadModel& reg, TypeId argType, Expect expect);

} // namespace format_check

// Человекочитаемое имя ожидаемой категории printf-аргумента (текст диагностики).
// Недопустимое значение enum — внутренняя ошибка (FAULT).
inline const char* format_expect_name(format_check::Expect expect) {
    using format_check::Expect;
    switch (expect) {
#define TRUST_FORMAT_EXPECT_CASE(name, text) \
    case Expect::name:                       \
        return text;
        TRUST_FORMAT_EXPECT_NAMES(TRUST_FORMAT_EXPECT_CASE)
#undef TRUST_FORMAT_EXPECT_CASE
    }
    FAULT("unknown Expect value {}", static_cast<int>(expect));
}

} // namespace trust
