#pragma once

// include/ast/binary_op.hpp
// BinaryOp - enum-opcode КОНКРЕТНОЙ бинарной операции (аналог BinaryOperatorKind в clang,
// tree codes в GCC, BinOp/AssignOp в Rust). ParserToken::Kind задаёт КЛАСС операции
// (MathOp/AssignOp/CompareOp/...), а BinaryOp - сам оператор (+ - * // += ...). Заполняется
// ОДИН раз при конвертации Term->AST (Binary::Binary); потребители переключаются по enum,
// без сравнений строк. text() остаётся для диагностик/рендера/форматтера.
#include <cstdint>
#include <string_view>

namespace trust {

enum class BinaryOp : uint8_t {
    None = 0, // оператор вне покрытия (сравнение/логика/битовые/не-арифметика)
    // -- Арифметика (value): + - * / // % --
    Add,
    Sub,
    Mul,
    Div,
    IntDiv,
    Mod,
    // -- Составные присваивания (непрерывный диапазон): += -= *= /= //= %= <<= >>= &= |= ^= --
    AddAssign,
    SubAssign,
    MulAssign,
    DivAssign,
    IntDivAssign,
    ModAssign,
    ShlAssign,
    ShrAssign,
    AndAssign,
    OrAssign,
    XorAssign,
    // -- Специальные --
    PlainAssign, // =
    Swap,        // :=:
    // -- Операторы сравнения типов (результат Bool) --
    TypeIsA,     // <~  : номинальная проверка (value/type LHS; с наследованием)
    TypeDuck,    // ~~  : утиная (нестрогая) / структурная
    TypeStrict,  // ~~~ : строгая (тождество типа / строгое структурное)
};

// Составное присваивание (AddAssign..XorAssign - непрерывный диапазон enum).
constexpr bool isCompoundAssignOp(BinaryOp op) noexcept {
    return op >= BinaryOp::AddAssign && op <= BinaryOp::XorAssign;
}
// Целочисленное деление "//"/"//=".
constexpr bool isIntDivOp(BinaryOp op) noexcept {
    return op == BinaryOp::IntDiv || op == BinaryOp::IntDivAssign;
}
// Знаковая машинная арифметика, для которой возможна детекция переполнения (-foverflow-check):
// value `+ - *` и составные `+= -= *=`. Семантика классифицирует такие узлы и пишет готовый
// признак в `Binary::m_overflowCheck`; кодоген признак только читает.
constexpr bool isOverflowCheckableOp(BinaryOp op) noexcept {
    return op == BinaryOp::Add || op == BinaryOp::Sub || op == BinaryOp::Mul || op == BinaryOp::AddAssign || op == BinaryOp::SubAssign ||
           op == BinaryOp::MulAssign;
}
// Простое присваивание "=".
constexpr bool isPlainAssignOp(BinaryOp op) noexcept {
    return op == BinaryOp::PlainAssign;
}
// Swap ":=:".
constexpr bool isSwapOp(BinaryOp op) noexcept {
    return op == BinaryOp::Swap;
}
// Оператор сравнения типов ("<~"/"~~"/"~~~") - результат Bool, спец-семантика/кодоген.
constexpr bool isTypeCheckOp(BinaryOp op) noexcept {
    return op == BinaryOp::TypeIsA || op == BinaryOp::TypeDuck || op == BinaryOp::TypeStrict;
}


// Текст оператора -> BinaryOp. ЕДИНСТВЕННОЕ место сопоставления (на этапе Term->AST).
// Непокрытые операторы (сравнение/логика/битовые не-присваивания) -> None.
[[nodiscard]] inline BinaryOp parseBinaryOp(std::string_view text) noexcept {
    if (text == "+") {
        return BinaryOp::Add;
    }
    if (text == "-") {
        return BinaryOp::Sub;
    }
    if (text == "*") {
        return BinaryOp::Mul;
    }
    if (text == "/") {
        return BinaryOp::Div;
    }
    if (text == "//") {
        return BinaryOp::IntDiv;
    }
    if (text == "%") {
        return BinaryOp::Mod;
    }
    if (text == "+=") {
        return BinaryOp::AddAssign;
    }
    if (text == "-=") {
        return BinaryOp::SubAssign;
    }
    if (text == "*=") {
        return BinaryOp::MulAssign;
    }
    if (text == "/=") {
        return BinaryOp::DivAssign;
    }
    if (text == "//=") {
        return BinaryOp::IntDivAssign;
    }
    if (text == "%=") {
        return BinaryOp::ModAssign;
    }
    if (text == "<<=") {
        return BinaryOp::ShlAssign;
    }
    if (text == ">>=") {
        return BinaryOp::ShrAssign;
    }
    if (text == "&=") {
        return BinaryOp::AndAssign;
    }
    if (text == "|=") {
        return BinaryOp::OrAssign;
    }
    if (text == "^=") {
        return BinaryOp::XorAssign;
    }
    if (text == "=") {
        return BinaryOp::PlainAssign;
    }
    if (text == ":=:") {
        return BinaryOp::Swap;
    }
    if (text == "<~") {
        return BinaryOp::TypeIsA;
    }
    if (text == "~~") {
        return BinaryOp::TypeDuck;
    }
    if (text == "~~~") {
        return BinaryOp::TypeStrict;
    }

    return BinaryOp::None;
}

} // namespace trust
