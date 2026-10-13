#pragma once

// include/types/call/operator_registry.hpp
// Реестр ПЕРЕГРУЖАЕМЫХ ОПЕРАТОРОВ TrustLang - ЕДИНЫЙ источник сопоставления:
//   системное имя (dunder) <-> символ оператора <-> C++-конструкция `operator<sym>`.
// Один источник для валидации (semantic) и кодогенерации (transpiler).
//
// Объявление оператора - обычная функция с системным именем:
//   `__eq__(o:T):Bool := { ... };` (member - в теле класса/struct, free - на верхнем уровне).
// Символ (`==`, `[]`, `()`, ...) остаётся ТОЛЬКО синтаксическим сахаром в выражениях и в
// диагностике; идентичность перегрузки - системное имя. Операторы, отсутствующие в
// kImplementedOperators, объявлять НЕЛЬЗЯ (silent fallback запрещён, AGENTS п.5).

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
    kIncDec, // ++/--: prefix (0 member / 1 free) и postfix (1 member / 2 free)
};

struct OperatorInfo {
    std::string_view symbol;       // символ-сахар в выражениях ("==", "[]", "()")
    std::string_view functionName; // системное имя-идентичность ("__eq__", "__getitem__", ...)
    std::string_view cppSpelling;  // печатается после `operator` ("==", "[]", "()")
    Arity arity;                   // число операндов
    bool allowMember;              // допустима форма-метод в теле класса/struct
    bool allowFree;                // допустима свободная форма на верхнем уровне модуля
};

// -- Реализованные операторы (X-macro) ------------------------------------
// O(symbol, function_name, cpp_spelling, arity, allow_member, allow_free)
// `()`/`[]`/`++`/`--` - только member (в текущей модели; free-формы ++/-- требуют
// reference-параметра и отдельно не поддержаны). `*` - member и free.
#define TRUST_OPERATOR_IMPLEMENTED(O)                        \
    O("==", "__eq__", "==", Arity::kBinary, true, true)      \
    O("!=", "__ne__", "!=", Arity::kBinary, true, true)      \
    O("<", "__lt__", "<", Arity::kBinary, true, true)        \
    O(">", "__gt__", ">", Arity::kBinary, true, true)        \
    O("<=", "__le__", "<=", Arity::kBinary, true, true)      \
    O(">=", "__ge__", ">=", Arity::kBinary, true, true)      \
    O("()", "__call__", "()", Arity::kCall, true, false)     \
    O("[]", "__getitem__", "[]", Arity::kIndex, true, false) \
    O("*", "__deref__", "*", Arity::kUnary, true, true)      \
    O("++", "__inc__", "++", Arity::kIncDec, true, false)    \
    O("--", "__dec__", "--", Arity::kIncDec, true, false)

inline constexpr OperatorInfo kImplementedOperators[] = {
#define TRUST_OPERATOR_ENTRY(symbol, function_name, cpp, arity, allow_member, allow_free) \
    OperatorInfo{symbol, function_name, cpp, arity, allow_member, allow_free},
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

/// Системная функция оператора (`__eq__`, `__getitem__`, ...). nullptr - имя не системное.
[[nodiscard]] inline const OperatorInfo* findByFunctionName(std::string_view functionName) noexcept {
    for (const OperatorInfo& op : kImplementedOperators) {
        if (op.functionName == functionName) {
            return &op;
        }
    }
    return nullptr;
}

/// Имя вида `__...__` (ведущие и хвостовые двойные подчёркивания) - системное пространство имён.
[[nodiscard]] inline bool isSystemName(std::string_view name) noexcept {
    return name.size() > 4 && name.substr(0, 2) == "__" && name.substr(name.size() - 2) == "__";
}

/// Системное имя зарегистрировано как оператор.
[[nodiscard]] inline bool isOperatorFunctionName(std::string_view name) noexcept {
    return findByFunctionName(name) != nullptr;
}

[[nodiscard]] inline bool isImplementedOperator(std::string_view symbol) noexcept {
    return findOperator(symbol) != nullptr;
}

/// Символ по системному имени (`__eq__` → "=="). Пусто - имя не оператор.
[[nodiscard]] inline std::string_view symbolOfFunction(std::string_view functionName) noexcept {
    const OperatorInfo* op = findByFunctionName(functionName);
    return op != nullptr ? op->symbol : std::string_view{};
}

/// Системное имя по символу (`==` → "__eq__"). Пусто - символ не реализован.
[[nodiscard]] inline std::string_view functionNameOfSymbol(std::string_view symbol) noexcept {
    const OperatorInfo* op = findOperator(symbol);
    return op != nullptr ? op->functionName : std::string_view{};
}

/// `++`/`--`: prefix и postfix различаются наличием dummy-параметра.
[[nodiscard]] inline bool isIncDecOperator(std::string_view symbol) noexcept {
    const OperatorInfo* op = findOperator(symbol);
    return op != nullptr && op->arity == Arity::kIncDec;
}

[[nodiscard]] inline bool isIncDecFunction(std::string_view functionName) noexcept {
    const OperatorInfo* op = findByFunctionName(functionName);
    return op != nullptr && op->arity == Arity::kIncDec;
}

/// C++-имя оператора (`==` → "operator==", `()` → "operator()"). Пусто - символ не реализован.
[[nodiscard]] inline std::string operatorCppName(std::string_view symbol) {
    const OperatorInfo* op = findOperator(symbol);
    if (op == nullptr) {
        return {};
    }
    return "operator" + std::string(op->cppSpelling);
}

/// C++-имя по системному имени-идентичности (`__eq__` → "operator=="). Пусто - не оператор.
[[nodiscard]] inline std::string operatorCppNameByFunction(std::string_view functionName) {
    const OperatorInfo* op = findByFunctionName(functionName);
    if (op == nullptr) {
        return {};
    }
    return "operator" + std::string(op->cppSpelling);
}

} // namespace trust::op
