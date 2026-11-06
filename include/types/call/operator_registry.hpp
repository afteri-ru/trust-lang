#pragma once

// Реестр ПЕРЕГРУЖАЕМЫХ ОПЕРАТОРОВ TrustLang - ЕДИНЫЙ источник сопоставления:
//   системное имя (dunder) <-> символ оператора <-> C++-конструкция `operator<sym>`.
// Один источник для валидации (semantic) и кодогенерации (transpiler).
// Объявление оператора - обычная функция с системным именем:
//   `__eq__(o:T):Bool := { ... };` (member - в теле класса/struct, free - на верхнем уровне).
// Символ (`==`, `[]`, `()`, ...) остаётся ТОЛЬКО синтаксическим сахаром в выражениях и в
// диагностике; идентичность перегрузки - системное имя. Операторы, отсутствующие в
// kImplementedOperators, объявлять НЕЛЬЗЯ (silent fallback запрещён, AGENTS п.5).
// Строки-глифы задаются в ОДНОМ месте - `utils/operator_spelling.hpp` (`trust::spell`).

#include "utils/operator_spelling.hpp"

#include <cstddef>
#include <cstdint>
#include <optional>
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
    std::string_view symbol;       // символ-сахар в выражениях == C++-написание ("==", "[]", "()")
    std::string_view functionName; // системное имя-идентичность ("__eq__", "__getitem__", ...)
    Arity arity;                   // число операндов
    bool allowMember;              // допустима форма-метод в теле класса/struct
    bool allowFree;                // допустима свободная форма на верхнем уровне модуля
};

// O(symbol, function_name, arity, allow_member, allow_free)
// Строка-глиф хранится ОДИН раз (symbol) и берётся из `trust::spell`; C++-написание
// (`operator<symbol>`) и число параметров ВЫВОДЯТСЯ из неё.
// `()`/`[]`/`++`/`--` - только member (в текущей модели; free-формы ++/-- требуют
// reference-параметра и отдельно не поддержаны). `*` - member и free.
#define TRUST_OPERATOR_IMPLEMENTED(O)                           \
    O(spell::kEq, "__eq__", Arity::kBinary, true, true)         \
    O(spell::kNe, "__ne__", Arity::kBinary, true, true)         \
    O(spell::kLt, "__lt__", Arity::kBinary, true, true)         \
    O(spell::kGt, "__gt__", Arity::kBinary, true, true)         \
    O(spell::kLe, "__le__", Arity::kBinary, true, true)         \
    O(spell::kGe, "__ge__", Arity::kBinary, true, true)         \
    O(spell::kCall, "__call__", Arity::kCall, true, false)      \
    O(spell::kIndex, "__getitem__", Arity::kIndex, true, false) \
    O(spell::kMul, "__deref__", Arity::kUnary, true, true)      \
    O(spell::kInc, "__inc__", Arity::kIncDec, true, false)      \
    O(spell::kDec, "__dec__", Arity::kIncDec, true, false)

inline constexpr OperatorInfo kImplementedOperators[] = {
#define TRUST_OPERATOR_ENTRY(symbol, function_name, arity, allow_member, allow_free) \
    OperatorInfo{symbol, function_name, arity, allow_member, allow_free},
    TRUST_OPERATOR_IMPLEMENTED(TRUST_OPERATOR_ENTRY)
#undef TRUST_OPERATOR_ENTRY
};

inline constexpr size_t kImplementedOperatorCount = sizeof(kImplementedOperators) / sizeof(kImplementedOperators[0]);

// --- Единый примитив доступа к таблице по двум ключам (symbol/functionName) + проекции полей. ---
// Поиск, сборка C++-имени и классификация IncDec определены РОВНО один раз и переиспользуются.
namespace detail {

[[nodiscard]] inline const OperatorInfo* findBySymbol(std::string_view symbol) noexcept {
    for (const OperatorInfo& op : kImplementedOperators) {
        if (op.symbol == symbol) {
            return &op;
        }
    }
    return nullptr;
}

[[nodiscard]] inline const OperatorInfo* findByFunction(std::string_view functionName) noexcept {
    for (const OperatorInfo& op : kImplementedOperators) {
        if (op.functionName == functionName) {
            return &op;
        }
    }
    return nullptr;
}

/// ЕДИНАЯ точка сборки C++-имени (`operator<symbol>`). nullptr/пусто - не оператор.
[[nodiscard]] inline std::string cppNameOf(const OperatorInfo* op) {
    return op != nullptr ? "operator" + std::string(op->symbol) : std::string{};
}

[[nodiscard]] inline bool isIncDecOf(const OperatorInfo* op) noexcept {
    return op != nullptr && op->arity == Arity::kIncDec;
}

} // namespace detail

/// Оператор реализован (символ из kImplementedOperators)?
[[nodiscard]] inline const OperatorInfo* findOperator(std::string_view symbol) noexcept {
    return detail::findBySymbol(symbol);
}

/// Системная функция оператора (`__eq__`, `__getitem__`, ...). nullptr - имя не системное.
[[nodiscard]] inline const OperatorInfo* findByFunctionName(std::string_view functionName) noexcept {
    return detail::findByFunction(functionName);
}

/// Имя вида `__...__` (ведущие и хвостовые двойные подчёркивания) - системное пространство имён.
[[nodiscard]] inline bool isSystemName(std::string_view name) noexcept {
    return name.size() > 4 && name.substr(0, 2) == "__" && name.substr(name.size() - 2) == "__";
}

/// Системное имя зарегистрировано как оператор.
[[nodiscard]] inline bool isOperatorFunctionName(std::string_view name) noexcept {
    return detail::findByFunction(name) != nullptr;
}

[[nodiscard]] inline bool isImplementedOperator(std::string_view symbol) noexcept {
    return detail::findBySymbol(symbol) != nullptr;
}

/// Символ по системному имени (`__eq__` → "=="). Пусто - имя не оператор.
[[nodiscard]] inline std::string_view symbolOfFunction(std::string_view functionName) noexcept {
    const OperatorInfo* op = detail::findByFunction(functionName);
    return op != nullptr ? op->symbol : std::string_view{};
}

/// Системное имя по символу (`==` → "__eq__"). Пусто - символ не реализован.
[[nodiscard]] inline std::string_view functionNameOfSymbol(std::string_view symbol) noexcept {
    const OperatorInfo* op = detail::findBySymbol(symbol);
    return op != nullptr ? op->functionName : std::string_view{};
}

/// `++`/`--`: prefix и postfix различаются наличием dummy-параметра.
[[nodiscard]] inline bool isIncDecOperator(std::string_view symbol) noexcept {
    return detail::isIncDecOf(detail::findBySymbol(symbol));
}

[[nodiscard]] inline bool isIncDecFunction(std::string_view functionName) noexcept {
    return detail::isIncDecOf(detail::findByFunction(functionName));
}

/// C++-имя оператора (`==` → "operator==", `()` → "operator()"). Пусто - символ не реализован.
[[nodiscard]] inline std::string operatorCppName(std::string_view symbol) {
    return detail::cppNameOf(detail::findBySymbol(symbol));
}

/// C++-имя по системному имени-идентичности (`__eq__` → "operator=="). Пусто - не оператор.
[[nodiscard]] inline std::string operatorCppNameByFunction(std::string_view functionName) {
    return detail::cppNameOf(detail::findByFunction(functionName));
}

/// Dunder-имя индексного оператора (`__getitem__`) - без символьного литерала на call-site.
[[nodiscard]] inline std::string_view indexFunctionName() noexcept {
    return functionNameOfSymbol(spell::kIndex);
}

/// Dunder-имя оператора вызова (`__call__`).
[[nodiscard]] inline std::string_view callFunctionName() noexcept {
    return functionNameOfSymbol(spell::kCall);
}

/// Ожидаемое число ПАРАМЕТРОВ объявления. Arity считает ОПЕРАНДЫ: member-форма имеет неявный
/// *this (на один параметр меньше), free-форма - все операнды. nullopt - не фиксировано.
[[nodiscard]] inline std::optional<size_t> operatorParamCount(Arity arity, bool isMember) noexcept {
    switch (arity) {
    case Arity::kUnary:
        return isMember ? 0u : 1u;
    case Arity::kBinary:
        return isMember ? 1u : 2u;
    case Arity::kIndex:
        return 1u; // всегда member, ровно один индекс
    case Arity::kCall:
        return std::nullopt; // operator() - произвольное число аргументов
    case Arity::kIncDec:
        return std::nullopt; // prefix/postfix - разное число параметров (обрабатывается отдельно)
    }
    return std::nullopt;
}

} // namespace trust::op
