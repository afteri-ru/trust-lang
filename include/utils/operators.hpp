#pragma once

// include/utils/operators.hpp
// Единый источник классификации бинарных операторов TrustLang по тексту (text()).
// Устраняет дублирование операторных строк-литералов ("//", "//=", "=") между
// семантическим анализатором (типизация результата) и транспилятором (кодогенерация):
// оба используют эти предикаты вместо хардкод-сравнений текста оператора.

#include <string_view>

namespace trust {
namespace utils {

/// Целочисленное деление "//" (или его составное присваивание "//="):
/// результат Int64, эмитится как static_cast<int64_t>l / static_cast<int64_t>r.
constexpr bool isIntDivOp(std::string_view op) noexcept {
    return op == "//" || op == "//=";
}

/// Составное присваивание: один из набора `op=` операторов (`+=`, `-=`, `*=`, `/=`, `//=`,
/// `%=`, `<<=`, `>>=`, `&=`, `|=`, `^=`). Проверяется ЯВНЫМ списком, а НЕ по суффиксу `=`,
/// иначе операторы сравнения `==`, `<=`, `>=` (тоже оканчиваются на `=`) ошибочно считались бы
/// составными присваиваниями и LHS расширялся бы до типа результата сравнения (Bool).
constexpr bool isCompoundAssignOp(std::string_view op) noexcept {
    return op == "+=" || op == "-=" || op == "*=" || op == "/=" || op == "//=" || op == "%=" || op == "<<=" || op == ">>=" || op == "&=" || op == "|=" ||
           op == "^=";
}

/// Простое присваивание "=" (адрес хранения, без std::any_cast для LHS).
constexpr bool isPlainAssignOp(std::string_view op) noexcept {
    return op == "=";
}

/// Swap `:=:` - обмен двух ссылок/переменных (`x :=: y`). Не арифметика и не простое
/// присваивание; семантика требует, чтобы оба операнда были ссылками (см. typeBinaryResult),
/// транспилятор эмитит std::swap.
constexpr bool isSwapOp(std::string_view op) noexcept {
    return op == ":=:";
}

} // namespace utils
} // namespace trust