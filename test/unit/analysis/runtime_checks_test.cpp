// test/unit/analysis/runtime_checks_test.cpp
// Единый классификатор рантайм-проверок (analysis/runtime_checks.hpp): исчерпывающее покрытие
// BinaryOp — переполнение, деление/остаток на ноль, «нет проверки».
#include "analysis/runtime_checks.hpp"
#include "ast/ast_nodes.hpp"

#include <gtest/gtest.h>

#include <memory>
#include <string>

namespace trust {
namespace {

AstNodePtr literal() {
    return std::make_shared<Literal>(ParserToken::Kind::IntLiteral, std::string("1"));
}

std::unique_ptr<Binary> makeBinary(ParserToken::Kind kind, BinaryOp op, std::optional<bool> overflow) {
    auto binary = std::make_unique<Binary>(kind, literal(), literal());
    binary->m_op = op;
    binary->m_overflowCheck = overflow;
    return binary;
}

TEST(RuntimeChecks, OverflowOpsNeedTheFlag) {
    const BinaryOp ops[] = {BinaryOp::Add, BinaryOp::Sub, BinaryOp::Mul, BinaryOp::AddAssign, BinaryOp::SubAssign, BinaryOp::MulAssign};
    for (const BinaryOp op : ops) {
        auto flagged = makeBinary(isCompoundAssignOp(op) ? ParserToken::Kind::AssignOp : ParserToken::Kind::MathOp, op, true);
        EXPECT_EQ(runtimeCheckOf(*flagged), RuntimeCheckKind::kIntOverflow) << static_cast<int>(op);
        auto unchecked = makeBinary(isCompoundAssignOp(op) ? ParserToken::Kind::AssignOp : ParserToken::Kind::MathOp, op, false);
        EXPECT_EQ(runtimeCheckOf(*unchecked), RuntimeCheckKind::kNone) << static_cast<int>(op);
    }
}

TEST(RuntimeChecks, DivisionAndRemainderNeedZeroCheck) {
    const BinaryOp ops[] = {BinaryOp::IntDiv, BinaryOp::IntDivAssign, BinaryOp::Mod, BinaryOp::ModAssign};
    for (const BinaryOp op : ops) {
        auto binary = makeBinary(isCompoundAssignOp(op) ? ParserToken::Kind::AssignOp : ParserToken::Kind::MathOp, op, std::nullopt);
        EXPECT_EQ(runtimeCheckOf(*binary), RuntimeCheckKind::kDivZero) << static_cast<int>(op);
    }
    // Вещественное деление не порождает проверки.
    auto realDiv = makeBinary(ParserToken::Kind::MathOp, BinaryOp::Div, std::nullopt);
    EXPECT_EQ(runtimeCheckOf(*realDiv), RuntimeCheckKind::kNone);
}

TEST(RuntimeChecks, ClassificationIsExhaustiveOverBinaryOps) {
    // Для КАЖДОГО BinaryOp: переполнение — ровно по флагу семантики, деление/остаток — по op,
    // всё остальное — kNone. Тест ловит новый оператор без решения о проверке.
    for (int value = 0; value <= static_cast<int>(BinaryOp::None); ++value) {
        const auto op = static_cast<BinaryOp>(value);
        // `None` — последний элемент перечисления (маркер «вне покрытия»).
        auto math = makeBinary(ParserToken::Kind::MathOp, op, std::nullopt);
        const RuntimeCheckKind kind = runtimeCheckOf(*math);
        if (isIntDivOp(op) || op == BinaryOp::Mod || op == BinaryOp::ModAssign) {
            EXPECT_EQ(kind, RuntimeCheckKind::kDivZero) << value;
        } else {
            EXPECT_EQ(kind, RuntimeCheckKind::kNone) << value;
        }
        auto flagged = makeBinary(ParserToken::Kind::MathOp, op, true);
        const RuntimeCheckKind flaggedKind = runtimeCheckOf(*flagged);
        EXPECT_EQ(flaggedKind, isOverflowCheckableOp(op) ? RuntimeCheckKind::kIntOverflow : RuntimeCheckKind::kNone) << value;
    }
}

TEST(RuntimeChecks, NonArithmeticKindsCarryNoCheck) {
    auto compare = makeBinary(ParserToken::Kind::CompareOp, BinaryOp::None, std::nullopt);
    EXPECT_EQ(runtimeCheckOf(*compare), RuntimeCheckKind::kNone);
    auto access = makeBinary(ParserToken::Kind::ArrayAccess, BinaryOp::None, std::nullopt);
    EXPECT_EQ(runtimeCheckOf(*access), RuntimeCheckKind::kNone);
}

} // namespace
} // namespace trust
