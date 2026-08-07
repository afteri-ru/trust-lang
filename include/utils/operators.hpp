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

/// Составное присваивание: текст оканчивается на '=' и длиннее 1 ("+=", "-=", "*=",
/// "/=", "%=", "//=", "<<=", ">>=", "&=", "|=", "^="). Простое "=" не входит.
constexpr bool isCompoundAssignOp(std::string_view op) noexcept {
    return op.size() > 1 && op.back() == '=';
}

/// Простое присваивание "=" (адрес хранения, без std::any_cast для LHS).
constexpr bool isPlainAssignOp(std::string_view op) noexcept {
    return op == "=";
}

} // namespace utils
} // namespace trust