#pragma once

// BinaryOp - enum-opcode КОНКРЕТНОЙ бинарной операции (аналог BinaryOperatorKind в clang,
// tree codes в GCC, BinOp/AssignOp в Rust). ParserToken::Kind задаёт КЛАСС операции
// (MathOp/AssignOp/CompareOp/...), а BinaryOp - сам оператор (+ - * // += ...). Заполняется
// ОДИН раз при конвертации Term->AST (Binary::Binary); потребители переключаются по enum.
// text() остаётся для диагностик/рендера/форматтера.
#include <cstdint>
#include <string_view>

#include "utils/operator_spelling.hpp"

namespace trust {

enum class BinaryOp : uint8_t {
    None = 0, // оператор вне покрытия (сравнение/битовые/не-арифметика)
    Add,
    Sub,
    Mul,
    Div,
    IntDiv,
    Mod,
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
    // Префикс возвращает НОВОЕ значение, постфикс - СТАРОЕ. Операнд: префикс - m_right,
    // постфикс - m_left (слот кодирует позицию; глиф оператора - только в m_op, текста у Binary нет).
    PreIncrement,  // ++x
    PostIncrement, // x++
    PreDecrement,  // --x
    PostDecrement, // x--
    PlainAssign, // =
    Swap,        // :=:
    TypeIsA,    // <~  : номинальная проверка (value/type LHS; с наследованием)
    TypeDuck,   // ~~  : утиная (нестрогая) / структурная
    TypeStrict, // ~~~ : строгая (тождество типа / строгое структурное)
    LogicalAnd, // &&
    LogicalOr,  // ||
    LogicalXor, // ^^
    Declare,    // :=   (NameDecl)
    TypeCreate, // ::=  (TypeDecl)
    TypeAlias,  // ::-  (TypeDecl: второе имя существующего типа, новый TypeId не создаётся)
    Append,     // []=  (AppendStmt)
    Member,     // .    (MemberAccess)
    Index,      // [    (ArrayAccess)
    Eq,       // ==
    EqExact,  // ===
    Ne,       // !=
    NeExact,  // !==
    NeNoCast, // =/=
    Lt,       // <
    Gt,       // >
    Le,       // <=
    Ge,       // >=
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
    O(Add, spell::kAdd)                      \
    O(Sub, spell::kSub)                      \
    O(Mul, spell::kMul)                      \
    O(Div, spell::kDiv)                      \
    O(IntDiv, spell::kIntDiv)                \
    O(Mod, spell::kMod)                      \
    O(AddAssign, spell::kAddAssign)          \
    O(SubAssign, spell::kSubAssign)          \
    O(MulAssign, spell::kMulAssign)          \
    O(DivAssign, spell::kDivAssign)          \
    O(IntDivAssign, spell::kIntDivAssign)    \
    O(ModAssign, spell::kModAssign)          \
    O(ShlAssign, spell::kShlAssign)          \
    O(ShrAssign, spell::kShrAssign)          \
    O(AndAssign, spell::kAndAssign)          \
    O(OrAssign, spell::kOrAssign)            \
    O(XorAssign, spell::kXorAssign)          \
    O(PlainAssign, spell::kAssign)           \
    O(Swap, spell::kSwap)                    \
    O(TypeIsA, spell::kTypeIsA)              \
    O(TypeDuck, spell::kTypeDuck)            \
    O(TypeStrict, spell::kTypeStrict)        \
    O(LogicalAnd, spell::kLogicalAnd)        \
    O(LogicalOr, spell::kLogicalOr)          \
    O(LogicalXor, spell::kLogicalXor)        \
    O(Declare, spell::kDeclare)              \
    O(TypeCreate, spell::kTypeCreate)        \
    O(TypeAlias, spell::kTypeAlias)          \
    O(Append, spell::kAppend)                \
    O(Member, spell::kMember)                \
    O(Index, spell::kIndexOpen)              \
    O(Eq, spell::kEq)                        \
    O(EqExact, spell::kEqExact)              \
    O(Ne, spell::kNe)                        \
    O(NeExact, spell::kNeExact)              \
    O(NeNoCast, spell::kNeNoCast)            \
    O(Lt, spell::kLt)                        \
    O(Gt, spell::kGt)                        \
    O(Le, spell::kLe)                        \
    O(Ge, spell::kGe)                        \
    O(BitAnd, spell::kBitAnd)                \
    O(BitOr, spell::kBitOr)                  \
    O(BitXor, spell::kBitXor)                \
    O(BitNot, spell::kBitNot)                \
    O(Shl, spell::kShl)                      \
    O(Shr, spell::kShr)                      \
    O(ShrAlt, spell::kShrAlt)

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
        return spell::kInc;
    case BinaryOp::PreDecrement:
    case BinaryOp::PostDecrement:
        return spell::kDec;
        TRUST_BINARY_OP_SPELLINGS(TRUST_BINARY_OP_SPELLING)
    case BinaryOp::None:
        return {};
    }
#undef TRUST_BINARY_OP_SPELLING
    return {};
}

} // namespace trust
