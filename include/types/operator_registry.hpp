#pragma once

// include/types/operator_registry.hpp
// Реестр ПЕРЕГРУЖАЕМЫХ ОПЕРАТОРОВ TrustLang - ЕДИНЫЙ источник сопоставления backtick-символа
// (лексема REFLECTION) и C++-конструкции `operator<sym>`. Один источник для валидации
// (semantic) и кодогенерации (transpiler) - без дублирования таблиц по месту.
//
// Символ оператора - текст в обратных кавычках: `` `==` ``, `` `()` ``, `` `[]` ``.
// Объявление: `\`==\`(o:T):Bool := { ... };` (member - в теле класса/struct, free - на верхнем
// уровне модуля). Операторы, отсутствующие в kImplementedOperators, объявлять НЕЛЬЗЯ - семантика
// выдаёт явную ошибку «not implemented» (silent fallback запрещён, AGENTS п.5).

#include <cstdint>
#include <string>
#include <string_view>

namespace trust::op {

// Арность оператора (число операндов; неявный *this у member-форм не считается).
enum class Arity : uint8_t {
    kUnary,  // один операнд
    kBinary, // два операнда (сравнения)
    kCall,   // operator() - произвольное число аргументов
    kIndex,  // operator[] - ровно один аргумент
};

struct OperatorInfo {
    std::string_view symbol;      // текст в обратных кавычках (`` `==` `` → "==")
    std::string_view cppSpelling; // печатается после `operator` (`` `()` `` → "()")
    Arity arity;                  // число операндов
    bool allowMember;             // допустима форма-метод в теле класса/struct
    bool allowFree;               // допустима свободная форма на верхнем уровне модуля
};

// -- Реализованные операторы (X-macro) ------------------------------------
// O(symbol, cpp_spelling, arity, allow_member, allow_free)
// `()`/`[]` - только member (требование C++: operator()/operator[] не бывают свободными).
#define TRUST_OPERATOR_IMPLEMENTED(O)         \
    O("==", "==", Arity::kBinary, true, true) \
    O("!=", "!=", Arity::kBinary, true, true) \
    O("<", "<", Arity::kBinary, true, true)   \
    O(">", ">", Arity::kBinary, true, true)   \
    O("<=", "<=", Arity::kBinary, true, true) \
    O(">=", ">=", Arity::kBinary, true, true) \
    O("()", "()", Arity::kCall, true, false)  \
    O("[]", "[]", Arity::kIndex, true, false)

inline constexpr OperatorInfo kImplementedOperators[] = {
#define TRUST_OPERATOR_ENTRY(symbol, cpp, arity, allow_member, allow_free) OperatorInfo{symbol, cpp, arity, allow_member, allow_free},
    TRUST_OPERATOR_IMPLEMENTED(TRUST_OPERATOR_ENTRY)
#undef TRUST_OPERATOR_ENTRY
};

inline constexpr size_t kImplementedOperatorCount = sizeof(kImplementedOperators) / sizeof(kImplementedOperators[0]);

/// Оператор реализован (символ из kImplementedOperators)?
[[nodiscard]] inline const OperatorInfo* findOperator(std::string_view symbol) noexcept {
    for (const OperatorInfo& op : kImplementedOperators) {
        if (op.symbol == symbol) {
            return &op;
        }
    }
    return nullptr;
}

[[nodiscard]] inline bool isImplementedOperator(std::string_view symbol) noexcept {
    return findOperator(symbol) != nullptr;
}

/// C++-имя оператора (`==` → "operator==", `()` → "operator()"). Пусто - символ не реализован.
[[nodiscard]] inline std::string operatorCppName(std::string_view symbol) {
    const OperatorInfo* op = findOperator(symbol);
    if (op == nullptr) {
        return {};
    }
    return "operator" + std::string(op->cppSpelling);
}

} // namespace trust::op
