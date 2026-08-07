#pragma once

// include/semantic/format_check.hpp
// Компиляйт-тайм проверка типов аргументов нативных функций на соответствие форматной
// строке printf (атрибут @[format("printf", string_index, first_to_check)] — GCC-аналог
// `__attribute__((format(...)))`). Содержит парсер printf-спецификаторов и сверку
// ожидаемой категории аргумента с фактическим типом.
//
// Использование (см. NameResolutionPass::typeExpr, case CallExpr):
//   vector<format_check::Conversion> convs;
//   if (format_check::parse_printf_format(fmt, convs)) {
//       for (i-я конверсия) if (!format_check::arg_matches_expect(types, argType, c.expect)) → diag
//   }

#include "types/type_id.hpp"
#include "types/typekind.hpp"

#include <cstdint>
#include <string_view>
#include <vector>

namespace trust {

class TypeRegistry;

namespace format_check {

// Ожидаемая категория printf-аргумента (по спецификатору + length-модификатору).
enum class Expect : uint8_t {
    Integer,  // %d %i (и %c) — целочисленный аргумент
    Unsigned, // %u %o %x %X — беззнаковое целое
    Float,    // %f %e %g %a — плавающий
    StrChar,  // %s — узкая строка (C-строка)
    Pointer,  // %p (%n) — указатель
};

// Один printf-конверсионный спецификатор, потребляющий аргумент.
struct Conversion {
    Expect expect; ///< ожидаемая категория аргумента
    char conv;     ///< конверсионный символ (для диагностики)
};

/// Парсит printf-формат-строку и заполняет список конверсий в порядке потребления аргументов
/// (`%%` — литерал, аргумент не потребляет и в список не попадает). Возвращает true при
/// успехе; false — строка не является валидным printf-форматом (незакрытый '%' либо
/// неизвестный конверсионный символ).
[[nodiscard]] bool parse_printf_format(std::string_view fmt, std::vector<Conversion>& out);

/// Соответствует ли фактический тип аргумента ожидаемой категории printf-конверсии.
/// INVALID-тип (выражение без выведенного типа) не проверяется → true.
[[nodiscard]] bool arg_matches_expect(const TypeRegistry& reg, TypeId argType, Expect expect);

} // namespace format_check
} // namespace trust
