#pragma once
// std::format-поддержка ГЕНЕРИРУЕМЫХ trust-классов строк — РАНТАЙМ-ПРИМИТИВ.
//
// Специализация `std::formatter<...>` невыразима в TrustLang, поэтому живёт в примитиве: класс
// `trust::StrChar` генерируется из stdlib/builtin/string.src, а формат-поддержка подключается
// include'ом на объявлении типа. Специализация ссылается на НЕПОЛНЫЙ тип (только объявление) —
// это законно: форматтер лишь наследует базовый `formatter<std::string>`, а приведение к базе
// выполняется в точке инстанцирования форматирования, где тип уже определён.

#include <format>
#include <string>

namespace trust {
class StrChar;
} // namespace trust

template <>
struct std::formatter<trust::StrChar> : std::formatter<std::string> {};