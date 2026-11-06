// test/unit/types/type_helpers_test.cpp
// Юнит-тесты общих хелперов типизации: isArithmeticGroup,
// intTypeForWidth, literalType, isAnyType и is_binary_expr_kind.

#include "support/cpp_facts_test_util.hpp"
#include "types/group.hpp"
#include "type_flag_test_utils.hpp"
#include "types/call.hpp"
#include "types/type_system.hpp"
#include "types/type_id.hpp"
#include "semantic/typer/type_inference.hpp"
#include "semantic/typer/literal_annotation.hpp"
#include "transpiler/type/cpp_type_renderer.hpp"
#include "ast/ast_nodes.hpp"
#include "ast/token.hpp"
#include "session/context.hpp"
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

class TypeHelpersFixture : public ::testing::Test {
  protected:
    Context m_ctx;
    std::unique_ptr<TypeSystem> m_types;
    TargetContract m_target;
    void SetUp() override {
        m_types = std::make_unique<TypeSystem>(m_ctx.diag(), m_ctx.opts());
        m_ctx.setTypes(m_types.get());
        test::installTargetFacts(m_ctx, m_target);
    }
};

TEST_F(TypeHelpersFixture, IntTypeForWidth) {
    TypeSystem& reg = *m_types;
    EXPECT_EQ(intTypeForWidth(reg, 8), reg.read().getType("Int8"));
    EXPECT_EQ(intTypeForWidth(reg, 16), reg.read().getType("Int16"));
    EXPECT_EQ(intTypeForWidth(reg, 32), reg.read().getType("Int32"));
    EXPECT_EQ(intTypeForWidth(reg, 64), reg.read().getType("Int64"));
    EXPECT_EQ(intTypeForWidth(reg, 0), reg.read().getType("Int64")); // default → Int64
}

TEST_F(TypeHelpersFixture, LiteralType) {
    TypeSystem& reg = *m_types;
    EXPECT_EQ(literalType(Literal(ParserToken::Kind::IntLiteral, "1"), reg), reg.read().getType("Int8"));
    EXPECT_EQ(literalType(Literal(ParserToken::Kind::IntLiteral, "0"), reg), reg.read().getType("Int8"));
    EXPECT_EQ(literalType(Literal(ParserToken::Kind::IntLiteral, "42"), reg), reg.read().getType("Int8"));
    EXPECT_EQ(literalType(Literal(ParserToken::Kind::IntLiteral, "1000"), reg), reg.read().getType("Int16"));
    EXPECT_EQ(literalType(Literal(ParserToken::Kind::IntLiteral, "100000"), reg), reg.read().getType("Int32"));
    EXPECT_EQ(literalType(Literal(ParserToken::Kind::IntLiteral, "999999999999"), reg), reg.read().getType("Int64"));
    // Разделители разрядов '_' срезаются до парсинга (единый хелпер stripDigitSeparators).
    EXPECT_EQ(literalType(Literal(ParserToken::Kind::IntLiteral, "10_000"), reg), reg.read().getType("Int16"));
    EXPECT_EQ(literalType(Literal(ParserToken::Kind::IntLiteral, "2_147_483_647"), reg), reg.read().getType("Int32"));
    EXPECT_EQ(literalType(Literal(ParserToken::Kind::IntLiteral, "2_147_483_648"), reg), reg.read().getType("Int64"));
    EXPECT_EQ(literalType(Literal(ParserToken::Kind::IntLiteral, "100_000"), reg), reg.read().getType("Int32"));
    EXPECT_EQ(literalType(Literal(ParserToken::Kind::IntLiteral, "999_999_999_999"), reg), reg.read().getType("Int64"));
    EXPECT_EQ(literalType(Literal(ParserToken::Kind::FloatLiteral, "1.5"), reg), reg.read().getType("Float64"));
    EXPECT_EQ(literalType(Literal(ParserToken::Kind::StrChar, "a"), reg), reg.read().getType("StrChar"));
    EXPECT_EQ(literalType(Literal(ParserToken::Kind::StrWide, "ab"), reg), reg.read().getType("StrWide"));
    // Рациональный литерал `num\den` - отдельная лексема → Rational.
    EXPECT_EQ(literalType(Literal(ParserToken::Kind::RationalLiteral, "1\\1"), reg), reg.read().getType("Rational"));
    EXPECT_EQ(literalType(Literal(ParserToken::Kind::RationalLiteral, "-55\\3"), reg), reg.read().getType("Rational"));
    // Не-числовой/не-строковый kind → тип не выведен.
    EXPECT_EQ(literalType(Literal(ParserToken::Kind::Ident, "x"), reg), INVALID_TYPE_ID);
}

TEST_F(TypeHelpersFixture, IsAnyType) {
    TypeSystem& reg = *m_types;
    EXPECT_TRUE(isAnyType(reg.read().getType("Any"), reg));
    EXPECT_FALSE(isAnyType(reg.read().getType("Int32"), reg));
    EXPECT_FALSE(isAnyType(INVALID_TYPE_ID, reg));
    // Алиас на Any канонизируется → любой.
    TypeId alias = reg.registrar().registerType("AnyAlias", reg.read().getType("Any"));
    if (alias != INVALID_TYPE_ID) {
        EXPECT_TRUE(isAnyType(alias, reg));
    }
}

// -- Константность (kConstFlag) - ортогональный квалификатор --
TEST_F(TypeHelpersFixture, ConstFlagMechanics) {
    TypeSystem& reg = *m_types;
    const TypeId i8 = reg.read().getType("Int8");
    const TypeId c = setFlag(i8, TypeFlag::Const);

    // Бит ставится/снимается хелперами, ортогонален структурной идентичности.
    EXPECT_TRUE(testFlag(c, TypeFlag::Const));
    EXPECT_FALSE(testFlag(i8, TypeFlag::Const));
    EXPECT_EQ(clearFlag(c, TypeFlag::Const), i8);
    EXPECT_EQ(setFlag(i8, TypeFlag::Const), c);
    // TypeId — identity-value: const-бит не входит в значение, только в raw().
    EXPECT_EQ(c, i8);
    EXPECT_NE(c.raw(), i8.raw());

    // getRegistryIndex снимает const-бит - структурный индекс не меняется.
    EXPECT_EQ(getRegistryIndex(c), getRegistryIndex(i8));
    // Каноника снимает const-бит - const T и T разделяют канонический тип.
    EXPECT_EQ(reg.read().store().desugaredTypeId(c), reg.read().store().desugaredTypeId(i8));

    // CppTypeRenderer::render(QualType) даёт лидирующий `const `; TypeId-версия — identity-only.
    auto base = CppTypeRenderer(reg.read(), test::cppFactsViewOf(m_ctx)).render(i8);
    auto ccpp = CppTypeRenderer(reg.read(), test::cppFactsViewOf(m_ctx)).render(QualType::fromTypeId(i8).withConst());
    ASSERT_TRUE(base.has_value());
    ASSERT_TRUE(ccpp.has_value());
    EXPECT_EQ(*ccpp, "const " + *base);
    EXPECT_NE(*ccpp, *base);
    // TypeId-перегрузка — identity-only: raw state не влияет на C++-имя.
    EXPECT_EQ(CppTypeRenderer(reg.read(), test::cppFactsViewOf(m_ctx)).render(c).value(), *base);

    // Полное trust-имя и lookup по структурному имени от const не зависят.
    EXPECT_EQ(reg.read().printer().typeNameOf(c), reg.read().printer().typeNameOf(i8));
}

TEST_F(TypeHelpersFixture, ConstAndInferredAreIndependent) {
    // kConstFlag и kInferredFlag - независимые биты одной нижней половины.
    const TypeId c = setFlag(setFlag(TypeId::fromRaw(123), TypeFlag::Inferred), TypeFlag::Const);
    EXPECT_TRUE(testFlag(c, TypeFlag::Const));
    EXPECT_TRUE(testFlag(c, TypeFlag::Inferred));
    EXPECT_EQ(clearFlag(c, TypeFlag::Const), setFlag(TypeId::fromRaw(123), TypeFlag::Inferred));
    EXPECT_EQ(clearFlag(c, TypeFlag::Inferred), setFlag(TypeId::fromRaw(123), TypeFlag::Const));
    // getRegistryIndex снимает оба бита.
    EXPECT_EQ(getRegistryIndex(c), getRegistryIndex(TypeId::fromRaw(123)));
}

// -- Dict / Dictionary - регистрация универсального словаря --
TEST_F(TypeHelpersFixture, DictTypeRegistered) {
    TypeSystem& reg = *m_types;
    // Оба имени резолвятся; Dictionary - алиас на Dict.
    TypeId dict = reg.read().getType("Dict");
    TypeId dictionary = reg.read().getType("Dictionary");
    EXPECT_NE(dict, INVALID_TYPE_ID);
    EXPECT_EQ(reg.read().store().desugaredTypeId(dictionary), dict);
    // C++-имя и заголовок появляются только ПОСЛЕ реализации (stdlib/builtin/dict.src).
    EXPECT_FALSE(CppTypeRenderer(reg.read(), test::cppFactsViewOf(m_ctx)).render(dict).has_value());
    ASSERT_EQ(test::defineBuiltinImplChecked(reg.read(), m_target, "Dict", "trust::Dict", {"@stdlib/builtin/dict.hpp"}, {}), dict);
    auto cpp = CppTypeRenderer(reg.read(), test::cppFactsViewOf(m_ctx)).render(dict);
    ASSERT_TRUE(cpp.has_value());
    EXPECT_EQ(*cpp, "trust::Dict");
    EXPECT_EQ(test::cppFactsViewOf(m_ctx).preprocIncludeOf(dict), "@stdlib/builtin/dict.hpp");
    // Группа kDicts - конкретная (Data≠0), категория Containers.
    EXPECT_EQ(typeGroupOf(dict), Group::kDicts);
    EXPECT_TRUE(typeDataOf(dict) != 0);
    EXPECT_TRUE(belongsToCategory(typeGroupOf(dict), Category::kContainers));
}

TEST(TypeHelperTest, ParseIntegerLiteralUnified) {
    // Единый разбор: знак, точный магнитуд, флаг превышения UINT64_MAX.
    ParsedIntLiteral v;
    ASSERT_TRUE(parseIntegerLiteral("42", v));
    EXPECT_FALSE(v.negative);
    EXPECT_EQ(v.magnitude, 42ULL);
    EXPECT_FALSE(v.exceedsUInt64);
    ASSERT_TRUE(parseIntegerLiteral("-5", v));
    EXPECT_TRUE(v.negative);
    EXPECT_EQ(v.magnitude, 5ULL);
    ASSERT_TRUE(parseIntegerLiteral("0_0_1", v));
    EXPECT_EQ(v.magnitude, 1ULL);
    ASSERT_TRUE(parseIntegerLiteral("18446744073709551615", v)); // UINT64_MAX
    EXPECT_FALSE(v.exceedsUInt64);
    ASSERT_TRUE(parseIntegerLiteral("18446744073709551616", v)); // UINT64_MAX+1
    EXPECT_TRUE(v.exceedsUInt64);
    ASSERT_TRUE(parseIntegerLiteral("-9223372036854775808", v)); // 2^63
    EXPECT_TRUE(v.negative);
    EXPECT_EQ(v.magnitude, 9223372036854775808ULL);
    EXPECT_FALSE(v.exceedsUInt64);
    // Не-литерал.
    EXPECT_FALSE(parseIntegerLiteral("", v));
    EXPECT_FALSE(parseIntegerLiteral("12a", v));
}

TEST(TypeHelperTest, FitsSignedIntMagnitude) {
    // Знак-учитывающая проверка ширины.
    ParsedIntLiteral pos, neg;
    ASSERT_TRUE(parseIntegerLiteral("127", pos));  // INT8_MAX
    ASSERT_TRUE(parseIntegerLiteral("-128", neg)); // INT8_MIN
    EXPECT_TRUE(fitsSignedIntMagnitude(pos, 8));
    EXPECT_TRUE(fitsSignedIntMagnitude(neg, 8));
    ASSERT_TRUE(parseIntegerLiteral("128", pos));
    ASSERT_TRUE(parseIntegerLiteral("-129", neg));
    EXPECT_FALSE(fitsSignedIntMagnitude(pos, 8));
    EXPECT_FALSE(fitsSignedIntMagnitude(neg, 8));
    EXPECT_TRUE(fitsSignedIntMagnitude(pos, 16));
    EXPECT_TRUE(fitsSignedIntMagnitude(neg, 16));
    // Превышение UINT64_MAX → не влезает.
    ASSERT_TRUE(parseIntegerLiteral("99999999999999999999999999", pos));
    EXPECT_TRUE(pos.exceedsUInt64);
    EXPECT_FALSE(fitsSignedIntMagnitude(pos, 64));
}

TEST(TypeHelperTest, IntFitsTargetSignAware) {
    // Знак-учитывающий intFitsTarget (для аннотаций `-1000 :Int16`, `-9223372036854775808 :Int64`).
    const TypeKind int16 = makeTypeKind(Group::kIntegers, 16);
    const TypeKind int64 = makeTypeKind(Group::kIntegers, 64);
    const TypeKind uint8 = makeTypeKind(Group::kUnsigned, 8);
    // Отрицательные влезают в знаковый.
    EXPECT_TRUE(intFitsTarget("-1000", int16));
    EXPECT_TRUE(intFitsTarget("-9223372036854775808", int64)); // == INT64_MIN
    EXPECT_FALSE(intFitsTarget("-9223372036854775809", int64));
    // Положительные границы.
    EXPECT_TRUE(intFitsTarget("32767", int16));
    EXPECT_FALSE(intFitsTarget("32768", int16));
    EXPECT_TRUE(intFitsTarget("9223372036854775807", int64));
    EXPECT_FALSE(intFitsTarget("9223372036854775808", int64));
    // Отрицательное не влезает в беззнаковое; положительные - по границе.
    EXPECT_FALSE(intFitsTarget("-1000", uint8));
    EXPECT_TRUE(intFitsTarget("255", uint8));
    EXPECT_FALSE(intFitsTarget("256", uint8));
}

TEST_F(TypeHelpersFixture, IntLiteralTypeConverter) {
    // Единый конвертер типа целочисленного литерала (int_literal.hpp).
    TypeSystem& reg = *m_types;
    // 0/1 → минимальный знаковый Int (Int8); НЕ Bool (Bool - только явная аннотация).
    EXPECT_EQ(intLiteralType(reg, "0"), reg.read().getType("Int8"));
    EXPECT_EQ(intLiteralType(reg, "1"), reg.read().getType("Int8"));
    // Минимальный знаковый Int, вмещающий значение.
    EXPECT_EQ(intLiteralType(reg, "42"), reg.read().getType("Int8"));
    EXPECT_EQ(intLiteralType(reg, "1000"), reg.read().getType("Int16"));
    EXPECT_EQ(intLiteralType(reg, "100000"), reg.read().getType("Int32"));
    EXPECT_EQ(intLiteralType(reg, "999999999999"), reg.read().getType("Int64"));
    // Разделители '_' срезаются.
    EXPECT_EQ(intLiteralType(reg, "10_000"), reg.read().getType("Int16"));
    // Сверхразрядный (модуль > 2^63) → BigInteger.
    EXPECT_EQ(intLiteralType(reg, "9223372036854775808"), reg.read().getType("BigInteger"));
    EXPECT_EQ(intLiteralType(reg, "9_223_372_036_854_775_808"), reg.read().getType("BigInteger"));
    EXPECT_EQ(intLiteralType(reg, "99999999999999999999999999"), reg.read().getType("BigInteger"));
    // Отрицательные литералы типизируются (знак-учитывающий выбор ширины).
    EXPECT_EQ(intLiteralType(reg, "-5"), reg.read().getType("Int8"));
    EXPECT_EQ(intLiteralType(reg, "-128"), reg.read().getType("Int8"));                  // == INT8_MIN
    EXPECT_EQ(intLiteralType(reg, "-129"), reg.read().getType("Int16"));                 // < INT8_MIN
    EXPECT_EQ(intLiteralType(reg, "-9223372036854775808"), reg.read().getType("Int64")); // == INT64_MIN
    EXPECT_EQ(intLiteralType(reg, "-9223372036854775809"), reg.read().getType("BigInteger"));
    // intTypeForLiteral: значение >= 2^63 → BigInteger (исправлено, раньше Int64).
    EXPECT_EQ(intTypeForLiteral(reg, 9223372036854775808ULL), reg.read().getType("BigInteger"));
}

// -- Срезание разделителей разрядов '_' в числовых литералах (единый хелпер) --
TEST(TypeHelperTest, StripDigitSeparatorsAndParse) {
    // stripDigitSeparators удаляет '_' из любого числа (единая точка для всех чисел).
    EXPECT_EQ(stripDigitSeparators("10_000"), "10000");
    EXPECT_EQ(stripDigitSeparators("9_223_372_036_854_775_808"), "9223372036854775808");
    EXPECT_EQ(stripDigitSeparators("2_2"), "22");
    EXPECT_EQ(stripDigitSeparators("42"), "42");
    EXPECT_EQ(stripDigitSeparators("-1_000"), "-1000");
    // parseDecimalUInt (с base 0) корректно парсит литерал с '_' и hex с '_'.
    unsigned long long v = 0;
    EXPECT_TRUE(parseDecimalUInt("10_000", v));
    EXPECT_EQ(v, 10000ULL);
    EXPECT_TRUE(parseDecimalUInt("0x1_0", v));
    EXPECT_EQ(v, 16ULL);
    EXPECT_TRUE(parseDecimalUInt("100_000", v));
    EXPECT_EQ(v, 100000ULL);
    // Переполнение (вне UInt64) - false.
    EXPECT_FALSE(parseDecimalUInt("99999999999999999999999999", v));
}

// -- BigInteger - регистрация в той же группе kArbitraryPrecision, что и Rational --
TEST_F(TypeHelpersFixture, BigIntegerTypeRegistered) {
    TypeSystem& reg = *m_types;
    TypeId bi = reg.read().getType("BigInteger");
    TypeId rat = reg.read().getType("Rational");
    EXPECT_NE(bi, INVALID_TYPE_ID);
    EXPECT_NE(rat, INVALID_TYPE_ID);
    // Та же группа kArbitraryPrecision (BigInteger - составная часть Rational); Data различаются.
    EXPECT_EQ(typeGroupOf(bi), Group::kArbitraryPrecision);
    EXPECT_EQ(typeGroupOf(rat), Group::kArbitraryPrecision);
    EXPECT_NE(bi, rat);
    // C++-имя и заголовок появляются только ПОСЛЕ реализации (stdlib/builtin/rational.src):
    // сам тип — лишь предобъявление.
    EXPECT_FALSE(CppTypeRenderer(reg.read(), test::cppFactsViewOf(m_ctx)).render(bi).has_value());
    ASSERT_EQ(test::defineBuiltinImplChecked(reg.read(), m_target, "BigInteger", "trust::BigInteger", {"@stdlib/builtin/big_integer.hpp"}), bi);
    auto cpp = CppTypeRenderer(reg.read(), test::cppFactsViewOf(m_ctx)).render(bi);
    ASSERT_TRUE(cpp.has_value());
    EXPECT_EQ(*cpp, "trust::BigInteger");
    EXPECT_EQ(test::cppFactsViewOf(m_ctx).preprocIncludeOf(bi), "@stdlib/builtin/big_integer.hpp");
    // Категория Arithmetics (как у Rational).
    EXPECT_TRUE(belongsToCategory(typeGroupOf(bi), Category::kArithmetics));
}

// -- Строгая типизация арифметики BigInteger/Rational (не std::any) --
TEST_F(TypeHelpersFixture, ArbitraryPrecisionArithmetic) {
    TypeSystem& reg = *m_types;
    const TypeId bi = reg.read().getType("BigInteger");
    const TypeId rat = reg.read().getType("Rational");
    const TypeId i64 = reg.read().getType("Int64");
    const TypeId i32 = reg.read().getType("Int32");
    const TypeId f64 = reg.read().getType("Float64");
    const TypeId biC = reg.read().store().desugaredTypeId(bi);
    const TypeId ratC = reg.read().store().desugaredTypeId(rat);
    // BigInteger op BigInteger → BigInteger; Rational op Rational → Rational.
    EXPECT_EQ(arbitraryPrecisionArithmeticType(reg, bi, bi), biC);
    EXPECT_EQ(arbitraryPrecisionArithmeticType(reg, rat, rat), ratC);
    // Смешанные BigInteger/Rational → Rational (Rational "шире").
    EXPECT_EQ(arbitraryPrecisionArithmeticType(reg, bi, rat), ratC);
    EXPECT_EQ(arbitraryPrecisionArithmeticType(reg, rat, bi), ratC);
    // X op машинное целое → X.
    EXPECT_EQ(arbitraryPrecisionArithmeticType(reg, bi, i64), biC);
    EXPECT_EQ(arbitraryPrecisionArithmeticType(reg, i32, rat), ratC);
    // X op float → INVALID (нужен явный каст).
    EXPECT_EQ(arbitraryPrecisionArithmeticType(reg, bi, f64), INVALID_TYPE_ID);
    EXPECT_EQ(arbitraryPrecisionArithmeticType(reg, f64, rat), INVALID_TYPE_ID);
}

TEST_F(TypeHelpersFixture, ResultTypeBinaryBigRational) {
    TypeSystem& reg = *m_types;
    const TypeId bi = reg.read().getType("BigInteger");
    const TypeId rat = reg.read().getType("Rational");
    const TypeId biC = reg.read().store().desugaredTypeId(bi);
    const TypeId ratC = reg.read().store().desugaredTypeId(rat);
    // z := x*y (BigInteger) → BigInteger (не std::any).
    EXPECT_EQ(resultTypeBinary(ParserToken::Kind::MathOp, BinaryOp::Mul, bi, bi, reg), biC);
    // Rational / Rational → Rational (точное деление).
    EXPECT_EQ(resultTypeBinary(ParserToken::Kind::MathOp, BinaryOp::Div, rat, rat, reg), ratC);
    // BigInteger * Rational → Rational.
    EXPECT_EQ(resultTypeBinary(ParserToken::Kind::MathOp, BinaryOp::Mul, bi, rat, reg), ratC);
    // Rational + Int64 → Rational.
    EXPECT_EQ(resultTypeBinary(ParserToken::Kind::MathOp, BinaryOp::Add, rat, reg.read().getType("Int64"), reg), ratC);
    // // на BigInteger → INVALID (целочисленное деление недоступно).
    EXPECT_EQ(resultTypeBinary(ParserToken::Kind::MathOp, BinaryOp::IntDiv, bi, bi, reg), INVALID_TYPE_ID);
    // Compare → Bool.
    EXPECT_EQ(resultTypeBinary(ParserToken::Kind::CompareOp, BinaryOp::None, bi, bi, reg), reg.read().getType("Bool"));
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

TEST(TypeHelperTest, QualTypeKeepsIdentitySeparateFromState) {
    const TypeId int8 = makeTypeId(makeTypeKind(Group::kIntegers, 8));
    const QualType state = QualType::fromTypeId(int8).withConst().withInferred();
    EXPECT_EQ(state.getTypeId(), int8); // identity-проекция снимает все state-флаги
    EXPECT_TRUE(state.isConst());
    EXPECT_TRUE(state.isInferred());

    const QualType other = QualType::fromTypeId(int8);
    EXPECT_EQ(state.getTypeId(), other.getTypeId()); // identity совпадает
    EXPECT_NE(state.raw(), other.raw());             // state различается
}

} // namespace
} // namespace trust
