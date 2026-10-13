#pragma once

// include/ast/binary_op.hpp
// BinaryOp - enum-opcode КОНКРЕТНОЙ бинарной операции (аналог BinaryOperatorKind в clang,
// tree codes в GCC, BinOp/AssignOp в Rust). ParserToken::Kind задаёт КЛАСС операции
// (MathOp/AssignOp/CompareOp/...), а BinaryOp - сам оператор (+ - * // += ...). Заполняется
// ОДИН раз при конвертации Term->AST (Binary::Binary); потребители переключаются по enum.
// text() остаётся для диагностик/рендера/форматтера.
#include <cstdint>
#include <string_view>

namespace trust {

enum class BinaryOp : uint8_t {
    None = 0, // оператор вне покрытия (сравнение/битовые/не-арифметика)
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
    // -- Инкремент/декремент (++ / --, непрерывный диапазон) --
    // Префикс возвращает НОВОЕ значение, постфикс - СТАРОЕ. Операнд: префикс - m_right,
    // постфикс - m_left (слот кодирует позицию; глиф оператора - только в m_op, текста у Binary нет).
    PreIncrement,  // ++x
    PostIncrement, // x++
    PreDecrement,  // --x
    PostDecrement, // x--
    // -- Специальные --
    PlainAssign, // =
    Swap,        // :=:
    // -- Операторы сравнения типов (результат Bool) --
    TypeIsA,    // <~  : номинальная проверка (value/type LHS; с наследованием)
    TypeDuck,   // ~~  : утиная (нестрогая) / структурная
    TypeStrict, // ~~~ : строгая (тождество типа / строгое структурное)
    // -- Логические (результат Bool) --
    LogicalAnd, // &&
    LogicalOr,  // ||
    LogicalXor, // ^^
    // -- Объявление/определение, доступ, append --
    Declare,    // :=   (NameDecl)
    TypeCreate, // ::=  (TypeDecl)
    TypeAlias,  // ::-  (TypeDecl: второе имя существующего типа, новый TypeId не создаётся)
    Append,     // []=  (AppendStmt)
    Member,     // .    (MemberAccess)
    Index,      // [    (ArrayAccess)
    // -- Сравнение значений --
    Eq,       // ==
    EqExact,  // ===
    Ne,       // !=
    NeExact,  // !==
    NeNoCast, // =/=
    Lt,       // <
    Gt,       // >
    Le,       // <=
    Ge,       // >=
    // -- Битовые (trust-глифы с точками; кодоген пока выводит глиф как есть) --
    BitAnd, // .&.
    BitOr,  // .|.
    BitXor, // .^.
    BitNot, // .~.
    Shl,    // .<.
    Shr,    // .>>.
    ShrAlt, // .>.
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

// Инкремент/декремент `++`/`--` (префикс/постфикс - непрерывный диапазон PreIncrement..PostDecrement).
constexpr bool isIncDecOp(BinaryOp op) noexcept {
    return op >= BinaryOp::PreIncrement && op <= BinaryOp::PostDecrement;
}
// Инкремент (++), а не декремент (--).
constexpr bool isIncrementOp(BinaryOp op) noexcept {
    return op == BinaryOp::PreIncrement || op == BinaryOp::PostIncrement;
}
// Постфиксная форма (`x++`/`x--`) - результат СТАРОЕ значение; иначе префикс.
constexpr bool isPostfixIncDecOp(BinaryOp op) noexcept {
    return op == BinaryOp::PostIncrement || op == BinaryOp::PostDecrement;
}

// ЕДИНЫЙ источник соответствия «enum-opcode <-> trust-глиф» (обе стороны: parse/spelling).
// O(enum_name, spelling). Намеренно не #undef: переиспользуется round-trip тестами/реестром.
#define TRUST_BINARY_OP_SPELLINGS(O) \
    O(Add, "+")                      \
    O(Sub, "-")                      \
    O(Mul, "*")                      \
    O(Div, "/")                      \
    O(IntDiv, "//")                  \
    O(Mod, "%")                      \
    O(AddAssign, "+=")               \
    O(SubAssign, "-=")               \
    O(MulAssign, "*=")               \
    O(DivAssign, "/=")               \
    O(IntDivAssign, "//=")           \
    O(ModAssign, "%=")               \
    O(ShlAssign, "<<=")              \
    O(ShrAssign, ">>=")              \
    O(AndAssign, "&=")               \
    O(OrAssign, "|=")                \
    O(XorAssign, "^=")               \
    O(PlainAssign, "=")              \
    O(Swap, ":=:")                   \
    O(TypeIsA, "<~")                 \
    O(TypeDuck, "~~")                \
    O(TypeStrict, "~~~")             \
    O(LogicalAnd, "&&")              \
    O(LogicalOr, "||")               \
    O(LogicalXor, "^^")              \
    O(Declare, ":=")                 \
    O(TypeCreate, "::=")             \
    O(TypeAlias, "::-")              \
    O(Append, "[]=")                 \
    O(Member, ".")                   \
    O(Index, "[")                    \
    O(Eq, "==")                      \
    O(EqExact, "===")                \
    O(Ne, "!=")                      \
    O(NeExact, "!==")                \
    O(NeNoCast, "=/=")               \
    O(Lt, "<")                       \
    O(Gt, ">")                       \
    O(Le, "<=")                      \
    O(Ge, ">=")                      \
    O(BitAnd, ".&.")                 \
    O(BitOr, ".|.")                  \
    O(BitXor, ".^.")                 \
    O(BitNot, ".~.")                 \
    O(Shl, ".<.")                    \
    O(Shr, ".>>.")                   \
    O(ShrAlt, ".>.")

// Текст оператора -> BinaryOp. Непокрытый глиф -> None (вызывающий обязан диагностировать:
// терм-backed Binary не имеет права молча остаться без opcode).
[[nodiscard]] inline BinaryOp parseBinaryOp(std::string_view text) noexcept {
#define TRUST_BINARY_OP_PARSE(name, spelling) \
    if (text == spelling) {                   \
        return BinaryOp::name;                \
    }
    TRUST_BINARY_OP_SPELLINGS(TRUST_BINARY_OP_PARSE)
#undef TRUST_BINARY_OP_PARSE
    return BinaryOp::None;
}

/// Trust-глиф оператора по opcode (обратная к parseBinaryOp; None -> пусто).
/// Единственный источник написания для кодогена/семантики/диагностик.
[[nodiscard]] inline std::string_view binaryOpSpelling(BinaryOp op) noexcept {
#define TRUST_BINARY_OP_SPELLING(name, spelling) \
    case BinaryOp::name:                         \
        return spelling;
    switch (op) {
    case BinaryOp::PreIncrement:
    case BinaryOp::PostIncrement:
        return "++";
    case BinaryOp::PreDecrement:
    case BinaryOp::PostDecrement:
        return "--";
        TRUST_BINARY_OP_SPELLINGS(TRUST_BINARY_OP_SPELLING)
    case BinaryOp::None:
        return {};
    }
#undef TRUST_BINARY_OP_SPELLING
    return {};
}

} // namespace trust
