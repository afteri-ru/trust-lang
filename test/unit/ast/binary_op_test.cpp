// test/unit/ast/binary_op_test.cpp
// Единый реестр операторов: spelling <-> BinaryOp (round-trip) и MatchOp.
#include "ast/ast_nodes.hpp"
#include "ast/binary_op.hpp"
#include "syntax/term/term.h"

#include <gtest/gtest.h>

#include <stdexcept>

namespace trust {
namespace {

TEST(BinaryOpTest, SpellingRoundTrip) {
#define CHECK_OP(name, spelling)                                    \
    EXPECT_EQ(parseBinaryOp(spelling), BinaryOp::name) << spelling; \
    EXPECT_EQ(binaryOpSpelling(BinaryOp::name), spelling) << #name;
    TRUST_BINARY_OP_SPELLINGS(CHECK_OP)
#undef CHECK_OP
}

TEST(BinaryOpTest, PreviouslyUncoveredGlyphsHaveOpcodes) {
    EXPECT_EQ(parseBinaryOp(":="), BinaryOp::Declare);
    EXPECT_EQ(parseBinaryOp("::="), BinaryOp::TypeCreate);
    EXPECT_EQ(parseBinaryOp("[]="), BinaryOp::Append);
    EXPECT_EQ(parseBinaryOp("."), BinaryOp::Member);
    EXPECT_EQ(parseBinaryOp("["), BinaryOp::Index);
    EXPECT_EQ(parseBinaryOp("=="), BinaryOp::Eq);
    EXPECT_EQ(parseBinaryOp("==="), BinaryOp::EqExact);
    EXPECT_EQ(parseBinaryOp("=/="), BinaryOp::NeNoCast);
    EXPECT_EQ(parseBinaryOp("<"), BinaryOp::Lt);
    EXPECT_EQ(parseBinaryOp(">="), BinaryOp::Ge);
    EXPECT_EQ(parseBinaryOp(".&."), BinaryOp::BitAnd);
    EXPECT_EQ(parseBinaryOp(".>>."), BinaryOp::Shr);
    EXPECT_EQ(parseBinaryOp(".>."), BinaryOp::ShrAlt);
}

TEST(BinaryOpTest, UnknownGlyphIsNone) {
    EXPECT_EQ(parseBinaryOp("??"), BinaryOp::None);
    EXPECT_TRUE(binaryOpSpelling(BinaryOp::None).empty());
}

TEST(BinaryOpTest, TermBackedBinaryRequiresKnownGlyph) {
    const TermPtr op = Term::Create(TermID::OP_MATH, std::string("??"));
    EXPECT_THROW(Binary(ParserToken::Kind::MathOp, op), std::runtime_error);
}

TEST(MatchOpTest, TermBackedMatchRequiresKnownOperator) {
    const TermPtr term = Term::Create(TermID::MATCHING, std::string("??"));
    EXPECT_THROW(MatchStmt(ParserToken::Kind::MatchingStmt, term), std::runtime_error);
}

TEST(MatchOpTest, ClassifiesSpellings) {
    EXPECT_EQ(matchOpFromSpelling("==>"), MatchOp::Value);
    EXPECT_EQ(matchOpFromSpelling("===>"), MatchOp::ValueExact);
    EXPECT_EQ(matchOpFromSpelling("~>"), MatchOp::TypeIsA);
    EXPECT_EQ(matchOpFromSpelling("~~>"), MatchOp::TypeDuck);
    EXPECT_EQ(matchOpFromSpelling("~~~>"), MatchOp::TypeStrict);

    // Терм-backed match всегда несёт известный оператор: неизвестный/пустой - внутренняя ошибка.
    EXPECT_THROW((void)matchOpFromSpelling(""), std::runtime_error);
    EXPECT_THROW((void)matchOpFromSpelling("??"), std::runtime_error);

    EXPECT_FALSE(isTypeMatchOp(MatchOp::ValueDefault));
    EXPECT_FALSE(isTypeMatchOp(MatchOp::Value));
    EXPECT_FALSE(isTypeMatchOp(MatchOp::ValueExact));
    EXPECT_TRUE(isTypeMatchOp(MatchOp::TypeIsA));
    EXPECT_TRUE(isTypeMatchOp(MatchOp::TypeDuck));
    EXPECT_TRUE(isTypeMatchOp(MatchOp::TypeStrict));
}

TEST(MatchOpTest, SpellingsRoundTrip) {
    EXPECT_EQ(matchOpSpelling(MatchOp::ValueDefault), "==");
    EXPECT_EQ(matchOpSpelling(MatchOp::Value), "==>");
    EXPECT_EQ(matchOpSpelling(MatchOp::ValueExact), "===>");
    EXPECT_EQ(matchOpSpelling(MatchOp::TypeIsA), "~>");
    EXPECT_EQ(matchOpSpelling(MatchOp::TypeDuck), "~~>");
    EXPECT_EQ(matchOpSpelling(MatchOp::TypeStrict), "~~~>");
}

TEST(MatchOpTest, InvalidSpellingFaults) {
    EXPECT_THROW((void)matchOpSpelling(static_cast<MatchOp>(255)), std::runtime_error);
}

} // namespace
} // namespace trust
