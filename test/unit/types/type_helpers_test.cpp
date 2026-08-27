// test/unit/types/type_helpers_test.cpp
// Юнит-тесты общих хелперов типизации: isArithmeticGroup, fitsIntegerValue,
// intTypeForWidth, literalType, isAnyType и is_binary_expr_kind.

#include "types/group.hpp"
#include "types/int_literal.hpp"
#include "types/registry.hpp"
#include "types/type_id.hpp"
#include "semantic/type_inference.hpp"
#include "ast/ast_nodes.hpp"
#include "ast/token.hpp"
#include "diag/context.hpp"
#include "gtest/gtest.h"

#include <memory>
#include <string>

namespace trust {
namespace {

TEST(TypeHelperTest, IsArithmeticGroup) {
    EXPECT_TRUE(isArithmeticGroup(Group::kIntegers));
    EXPECT_TRUE(isArithmeticGroup(Group::kUnsigned));
    EXPECT_TRUE(isArithmeticGroup(Group::kNumbers));
    EXPECT_FALSE(isArithmeticGroup(Group::kAny));
    EXPECT_FALSE(isArithmeticGroup(Group::kLogical));
    EXPECT_FALSE(isArithmeticGroup(Group::kVoid));
    EXPECT_FALSE(isArithmeticGroup(Group::kStrChar));
}

TEST(TypeHelperTest, FitsIntegerValue) {
    // Знаковые границы.
    EXPECT_TRUE(fitsIntegerValue(Group::kIntegers, 8, 127));
    EXPECT_FALSE(fitsIntegerValue(Group::kIntegers, 8, 128));
    EXPECT_TRUE(fitsIntegerValue(Group::kIntegers, 16, 32767));
    EXPECT_FALSE(fitsIntegerValue(Group::kIntegers, 16, 32768));
    EXPECT_TRUE(fitsIntegerValue(Group::kIntegers, 32, 2147483647ULL));
    EXPECT_FALSE(fitsIntegerValue(Group::kIntegers, 32, 2147483648ULL));
    EXPECT_TRUE(fitsIntegerValue(Group::kIntegers, 64, 999999999999ULL));
    // Беззнаковые границы.
    EXPECT_TRUE(fitsIntegerValue(Group::kUnsigned, 8, 255));
    EXPECT_FALSE(fitsIntegerValue(Group::kUnsigned, 8, 256));
    EXPECT_TRUE(fitsIntegerValue(Group::kUnsigned, 32, 4294967295ULL));
    EXPECT_FALSE(fitsIntegerValue(Group::kUnsigned, 32, 4294967296ULL));
    // Не-целая группа - целочисленный литерал считается безопасным.
    EXPECT_TRUE(fitsIntegerValue(Group::kNumbers, 64, 123456));
}

class TypeHelpersFixture : public ::testing::Test {
  protected:
    Context m_ctx;
    std::unique_ptr<TypeRegistry> m_types;
    void SetUp() override {
        m_types = std::make_unique<TypeRegistry>(m_ctx.diag(), m_ctx.opts());
        m_ctx.setTypes(m_types.get());
    }
};

TEST_F(TypeHelpersFixture, IntTypeForWidth) {
    TypeRegistry& reg = m_ctx.types();
    EXPECT_EQ(intTypeForWidth(reg, 8), reg.getType("Int8"));
    EXPECT_EQ(intTypeForWidth(reg, 16), reg.getType("Int16"));
    EXPECT_EQ(intTypeForWidth(reg, 32), reg.getType("Int32"));
    EXPECT_EQ(intTypeForWidth(reg, 64), reg.getType("Int64"));
    EXPECT_EQ(intTypeForWidth(reg, 0), reg.getType("Int64")); // default → Int64
}

TEST_F(TypeHelpersFixture, LiteralType) {
    TypeRegistry& reg = m_ctx.types();
    EXPECT_EQ(literalType(Literal(ParserToken::Kind::IntLiteral, "1"), reg), reg.getType("Bool"));
    EXPECT_EQ(literalType(Literal(ParserToken::Kind::IntLiteral, "0"), reg), reg.getType("Bool"));
    EXPECT_EQ(literalType(Literal(ParserToken::Kind::IntLiteral, "42"), reg), reg.getType("Int8"));
    EXPECT_EQ(literalType(Literal(ParserToken::Kind::IntLiteral, "1000"), reg), reg.getType("Int16"));
    EXPECT_EQ(literalType(Literal(ParserToken::Kind::IntLiteral, "100000"), reg), reg.getType("Int32"));
    EXPECT_EQ(literalType(Literal(ParserToken::Kind::IntLiteral, "999999999999"), reg), reg.getType("Int64"));
    EXPECT_EQ(literalType(Literal(ParserToken::Kind::FloatLiteral, "1.5"), reg), reg.getType("Float64"));
    EXPECT_EQ(literalType(Literal(ParserToken::Kind::StrChar, "a"), reg), reg.getType("StrChar"));
    EXPECT_EQ(literalType(Literal(ParserToken::Kind::StrWide, "ab"), reg), reg.getType("StrWide"));
    // Рациональный литерал `num\den` - отдельная лексема → Rational.
    EXPECT_EQ(literalType(Literal(ParserToken::Kind::RationalLiteral, "1\\1"), reg), reg.getType("Rational"));
    EXPECT_EQ(literalType(Literal(ParserToken::Kind::RationalLiteral, "-55\\3"), reg), reg.getType("Rational"));
    // Не-числовой/не-строковый kind → тип не выведен.
    EXPECT_EQ(literalType(Literal(ParserToken::Kind::Ident, "x"), reg), INVALID_TYPE_ID);
}

TEST_F(TypeHelpersFixture, IsAnyType) {
    TypeRegistry& reg = m_ctx.types();
    EXPECT_TRUE(isAnyType(reg.getType("Any"), reg));
    EXPECT_FALSE(isAnyType(reg.getType("Int32"), reg));
    EXPECT_FALSE(isAnyType(INVALID_TYPE_ID, reg));
    // Алиас на Any канонизируется → любой.
    TypeId alias = reg.registerType("AnyAlias", reg.getType("Any"));
    if (alias != INVALID_TYPE_ID) {
        EXPECT_TRUE(isAnyType(alias, reg));
    }
}

// -- Константность (kConstFlag) - ортогональный квалификатор --
TEST_F(TypeHelpersFixture, ConstFlagMechanics) {
    TypeRegistry& reg = m_ctx.types();
    const TypeId i8 = reg.getType("Int8");
    const TypeId c = withConst(i8);

    // Бит ставится/снимается хелперами, ортогонален структурной идентичности.
    EXPECT_TRUE(typeIsConst(c));
    EXPECT_FALSE(typeIsConst(i8));
    EXPECT_EQ(clearConst(c), i8);
    EXPECT_EQ(withConst(i8), c);

    // getIndexFromId снимает const-бит - структурный индекс не меняется.
    EXPECT_EQ(getIndexFromId(c), getIndexFromId(i8));
    // Каноника снимает const-бит - const T и T разделяют канонический тип.
    EXPECT_EQ(reg.getCanonicalTypeId(c), reg.getCanonicalTypeId(i8));

    // getCppTypeName даёт лидирующий `const `.
    auto base = reg.getCppTypeName(i8);
    auto ccpp = reg.getCppTypeName(c);
    ASSERT_TRUE(base.has_value());
    ASSERT_TRUE(ccpp.has_value());
    EXPECT_EQ(*ccpp, "const " + *base);
    EXPECT_NE(*ccpp, *base);

    // Полное trust-имя и lookup по структурному имени от const не зависят.
    EXPECT_EQ(reg.getFullTypeName(c), reg.getFullTypeName(i8));
}

TEST_F(TypeHelpersFixture, ConstAndInferredAreIndependent) {
    // kConstFlag и kInferredFlag - независимые биты одной нижней половины.
    const TypeId c = withConst(withInferred(123));
    EXPECT_TRUE(typeIsConst(c));
    EXPECT_TRUE(typeIsInferred(c));
    EXPECT_EQ(clearConst(c), withInferred(123));
    EXPECT_EQ(clearInferred(c), withConst(123));
    // getIndexFromId снимает оба бита.
    EXPECT_EQ(getIndexFromId(c), getIndexFromId(123));
}

// -- Dict / Dictionary - регистрация универсального словаря --
TEST_F(TypeHelpersFixture, DictTypeRegistered) {
    TypeRegistry& reg = m_ctx.types();
    // Оба имени резолвятся; Dictionary - алиас на Dict.
    TypeId dict = reg.getType("Dict");
    TypeId dictionary = reg.getType("Dictionary");
    EXPECT_NE(dict, INVALID_TYPE_ID);
    EXPECT_EQ(reg.getCanonicalTypeId(dictionary), dict);
    // C++-имя и заголовок (маркер '@' = нужен trust-runtime).
    auto cpp = reg.getCppTypeName(dict);
    ASSERT_TRUE(cpp.has_value());
    EXPECT_EQ(*cpp, "trust::Dict");
    EXPECT_EQ(reg.getPreprocInclude(dict), "@trust/dict.hpp");
    // Группа kDicts - конкретная (Data≠0), категория Containers.
    EXPECT_EQ(getGroup(getKindFromId(dict)), Group::kDicts);
    EXPECT_TRUE(getData(getKindFromId(dict)) != 0);
    EXPECT_TRUE(belongsToCategory(getGroup(getKindFromId(dict)), Category::kContainers));
}

TEST(TypeHelperTest, IsBinaryExprKind) {
    using K = ParserToken::Kind;
    EXPECT_TRUE(is_binary_expr_kind(K::MathOp));
    EXPECT_TRUE(is_binary_expr_kind(K::BitwiseOp));
    EXPECT_TRUE(is_binary_expr_kind(K::CompareOp));
    EXPECT_TRUE(is_binary_expr_kind(K::LogicalOp));
    EXPECT_TRUE(is_binary_expr_kind(K::NameDecl));
    EXPECT_TRUE(is_binary_expr_kind(K::AssignOp));
    // Binary-класс, но НЕ типизируемое выражение (объявление/member access).
    EXPECT_FALSE(is_binary_expr_kind(K::TypeDecl));
    EXPECT_FALSE(is_binary_expr_kind(K::MemberAccess));
    EXPECT_FALSE(is_binary_expr_kind(K::VarDecl));
}

} // namespace
} // namespace trust
