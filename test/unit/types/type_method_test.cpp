// test/unit/types/type_method_test.cpp
// Юнит-тесты нативных методов на встроенных типах (MethodTable::addMethod/findMethodInfo):
// тип CString (const char*), метод StrChar.%c_str() и односторонний поиск.

#include "types/type_system.hpp"
#include "types/names.hpp"
#include "utils/strings.hpp"
#include "session/context.hpp"
#include "gtest/gtest.h"

#include <memory>
#include <stdexcept>

namespace trust {
namespace {

class TypeMethodFixture : public ::testing::Test {
  protected:
    void SetUp() override {
        m_types = std::make_unique<TypeSystem>(m_ctx.diag(), m_ctx.opts());
        m_ctx.setTypes(m_types.get());
    }
    Context m_ctx;
    std::unique_ptr<TypeSystem> m_types;
};

// Единственная сигнатура метода по имени (встроенные методы не перегружены).
static TypeId findMethodSig(TypeSystem& reg, TypeId type, std::string_view name) {
    const auto m = reg.read().methods().findMethodInfo(type, name);
    return m ? m->entries.front().signature : INVALID_TYPE_ID;
}

TEST_F(TypeMethodFixture, CStringTypeRegistered) {
    TypeSystem& reg = m_ctx.types();
    const TypeId cstr = reg.read().getType("CString");
    ASSERT_NE(cstr, INVALID_TYPE_ID);
    // CString транслируется в `const char*`.
    EXPECT_EQ(reg.read().printer().getCppTypeName(cstr).value(), "const char*");
}

TEST_F(TypeMethodFixture, StrCharHasCStrMethod) {
    TypeSystem& reg = m_ctx.types();
    const TypeId strChar = reg.read().getType("StrChar");
    const TypeId cstr = reg.read().getType("CString");
    ASSERT_NE(strChar, INVALID_TYPE_ID);

    // Обычное имя находит нативный метод (ОДНА СТОРОНА). Метод - функциональный тип.
    const TypeId m = findMethodSig(reg, strChar, "c_str");
    ASSERT_NE(m, INVALID_TYPE_ID);
    const auto* fd = reg.read().store().getTypeDataAs<FunctionTypeData>(m);
    ASSERT_NE(fd, nullptr);
    EXPECT_EQ(fd->returnType, cstr);
    EXPECT_TRUE(fd->paramTypes.empty());

    // Нативное имя - точное совпадение.
    const TypeId mn = findMethodSig(reg, strChar, "%c_str");
    ASSERT_NE(mn, INVALID_TYPE_ID);
    EXPECT_EQ(mn, m); // одна и та же сигнатура → один функциональный тип (интернирование)

    // Несуществующий метод - INVALID_TYPE_ID.
    EXPECT_EQ(findMethodSig(reg, strChar, "nope"), INVALID_TYPE_ID);
}

TEST_F(TypeMethodFixture, StrCharStringMethods) {
    TypeSystem& reg = m_ctx.types();
    const TypeId strChar = reg.read().getType("StrChar");
    const TypeId u64 = reg.read().getType("UInt64");
    const TypeId bo = reg.read().getType("Bool");
    const TypeId cstr = reg.read().getType("CString");
    ASSERT_NE(strChar, INVALID_TYPE_ID);

    // size()/length() → UInt64 (размер std::string). Общие сигнатуры интернируются в один тип.
    const auto* sz = reg.read().store().getTypeDataAs<FunctionTypeData>(findMethodSig(reg, strChar, "size"));
    const auto* len = reg.read().store().getTypeDataAs<FunctionTypeData>(findMethodSig(reg, strChar, "length"));
    ASSERT_NE(sz, nullptr);
    ASSERT_NE(len, nullptr);
    EXPECT_EQ(findMethodSig(reg, strChar, "size"), findMethodSig(reg, strChar, "length"));
    EXPECT_EQ(sz->returnType, u64);
    EXPECT_EQ(len->returnType, u64);

    // empty() → Bool.
    const auto* em = reg.read().store().getTypeDataAs<FunctionTypeData>(findMethodSig(reg, strChar, "empty"));
    ASSERT_NE(em, nullptr);
    EXPECT_EQ(em->returnType, bo);

    // data() → CString (const char*).
    const auto* dt = reg.read().store().getTypeDataAs<FunctionTypeData>(findMethodSig(reg, strChar, "data"));
    ASSERT_NE(dt, nullptr);
    EXPECT_EQ(dt->returnType, cstr);
    EXPECT_EQ(findMethodSig(reg, strChar, "data"), findMethodSig(reg, strChar, "c_str"));
}

// Инвариант «одна форма имени»: повторная регистрация метода в любой из двух форм - ошибка.
// EXPECT бросает std::runtime_error, тесты падают сразу.
TEST_F(TypeMethodFixture, AddMethodRejectsBothNameForms) {
    TypeSystem& reg = m_ctx.types();
    const TypeId strChar = reg.read().getType("StrChar");
    const TypeId cstr = reg.read().getType("CString");
    // "%c_str" уже зарегистрирован в BuiltinCoreBuilder::registerBuiltinTypes → попытка "c_str" обязана упасть.
    EXPECT_THROW(reg.methods().addMethod(strChar, "c_str", reg.interner().getOrCreateFunctionType(cstr, {})), std::runtime_error);
}

TEST_F(TypeMethodFixture, AddMethodRejectsExactDuplicate) {
    TypeSystem& reg = m_ctx.types();
    const TypeId strChar = reg.read().getType("StrChar");
    const TypeId cstr = reg.read().getType("CString");
    // Повторная регистрация того же "%c_str" - ошибка.
    EXPECT_THROW(reg.methods().addMethod(strChar, "%c_str", reg.interner().getOrCreateFunctionType(cstr, {})), std::runtime_error);
}

// Универсальный диапазон `:Range` имеет встроенные методы (trust::Range<Elem>): count()/size() →
// Int64, empty() → Bool; алиас trust `length` → нативное C++-имя `count`. Ключи - полная форма
// ('%' нативный, '^' константный); нативность/константность выводятся из ключа (bareName/
// isConstName/isNativeName), отдельно не хранятся.
TEST_F(TypeMethodFixture, RangeHasBuiltinMethods) {
    TypeSystem& reg = m_ctx.types();
    const TypeId range = reg.read().getType("Range");
    const TypeId int64 = reg.read().getType("Int64");
    ASSERT_NE(range, INVALID_TYPE_ID);

    const auto count = reg.read().methods().findMethodInfo(range, "count");
    ASSERT_TRUE(count.has_value());
    EXPECT_EQ(count->entries.front().signature, findMethodSig(reg, range, "count"));
    EXPECT_EQ(utils::bareName(count->key), "count");
    EXPECT_TRUE(utils::isNativeName(count->key));
    EXPECT_TRUE(utils::isConstName(count->key)); // %count^
    const auto* fd = reg.read().store().getTypeDataAs<FunctionTypeData>(count->entries.front().signature);
    ASSERT_NE(fd, nullptr);
    EXPECT_EQ(fd->returnType, int64);
    EXPECT_TRUE(fd->paramTypes.empty());

    // Алиас: trust `length` → нативное `count` (алиас повторяет семантику цели: нативный+константный);
    // findMethodInfo возвращает ключ ЦЕЛИ ("%count^") → нативное имя "count".
    const auto length = reg.read().methods().findMethodInfo(range, "length");
    ASSERT_TRUE(length.has_value());
    EXPECT_EQ(length->key, count->key);
    EXPECT_EQ(utils::bareName(length->key), "count");
    EXPECT_EQ(length->entries.front().signature, count->entries.front().signature);

    // size() - собственное нативное имя.
    const auto size = reg.read().methods().findMethodInfo(range, "size");
    ASSERT_TRUE(size.has_value());
    EXPECT_EQ(utils::bareName(size->key), "size");

    // Несуществующий метод.
    EXPECT_FALSE(reg.read().methods().findMethodInfo(range, "nope").has_value());
}

// Регистрация const ('^') и не-const методов с ОДИНАКОВЫМИ аргументами (перегрузки по
// константности) допустима; точный дубль (та же константность + имя) - ошибка EXPECT.
TEST_F(TypeMethodFixture, AddMethodConstNonConstOverloads) {
    TypeSystem& reg = m_ctx.types();
    const TypeId base = reg.read().getType("Int64");
    // Пользовательский (не-алиас) тип: enum имеет собственный дескриптор (не канонизируется в базовый).
    const TypeId t = reg.registrar().registerEnumType("MyType", base, std::vector<EnumMemberData>{{"A", "0"}});
    ASSERT_NE(t, INVALID_TYPE_ID);
    const TypeId sig = reg.interner().getOrCreateFunctionType(base, {});
    reg.methods().addMethod(t, "%get^", sig); // const-перегрузка
    reg.methods().addMethod(t, "%get", sig);  // не-const перегрузка - допустима (другая константность)
    const auto info = reg.read().methods().findMethodInfo(t, "get");
    ASSERT_TRUE(info.has_value());
    EXPECT_EQ(utils::bareName(info->key), "get");
    EXPECT_EQ(info->entries.front().signature, sig);
    // Точный дубль (та же константность) - ошибка.
    EXPECT_THROW(reg.methods().addMethod(t, "%get^", sig), std::runtime_error);
}

// Параметризованный Range<Elem> (C++-модель шаблонов): структурный тип, интернируемый по
// элементному типу; методы объявлены на абстрактном :Range с типовым параметром T и
// подставляются (T→Elem) в instantiateRangeMethod → точные сигнатуры.
TEST_F(TypeMethodFixture, RangeParametricSubstitution) {
    TypeSystem& reg = m_ctx.types();
    const TypeId int64 = reg.read().getType("Int64");
    const TypeId rational = reg.read().getType("Rational");
    const TypeId ri = reg.interner().getOrCreateRangeType(int64);
    const TypeId rr = reg.interner().getOrCreateRangeType(rational);
    EXPECT_TRUE(reg.read().store().isRangeType(ri));
    EXPECT_TRUE(reg.read().store().isRangeType(rr));
    EXPECT_NE(ri, rr); // Range<Int64> и Range<Rational> - разные типы
    EXPECT_EQ(reg.read().store().rangeElementType(ri), int64);
    EXPECT_EQ(reg.read().store().rangeElementType(rr), rational);
    EXPECT_EQ(reg.interner().getOrCreateRangeType(int64), ri); // интернирование по elementType

    // `at(Int64) → T`: findMethodInfo(Range<Int64>) находит метод на абстрактном :Range,
    // instantiateRangeMethod подставляет T→Int64.
    const auto at = reg.read().methods().findMethodInfo(ri, "at");
    ASSERT_TRUE(at.has_value());
    const auto* fd = reg.read().store().getTypeDataAs<FunctionTypeData>(reg.interner().instantiateRangeMethod(ri, at->entries.front().signature));
    ASSERT_NE(fd, nullptr);
    EXPECT_EQ(fd->returnType, int64); // at(i) → Int64 (не типовой параметр)
    ASSERT_EQ(fd->paramTypes.size(), 1u);
    EXPECT_EQ(fd->paramTypes[0], int64);

    // start() → T → Int64.
    const auto start = reg.read().methods().findMethodInfo(ri, "start");
    ASSERT_TRUE(start.has_value());
    const auto* sfd = reg.read().store().getTypeDataAs<FunctionTypeData>(reg.interner().instantiateRangeMethod(ri, start->entries.front().signature));
    ASSERT_NE(sfd, nullptr);
    EXPECT_EQ(sfd->returnType, int64);

    // contains(T) → Bool: параметр T→Int64.
    const auto contains = reg.read().methods().findMethodInfo(ri, "contains");
    ASSERT_TRUE(contains.has_value());
    const auto* cfd = reg.read().store().getTypeDataAs<FunctionTypeData>(reg.interner().instantiateRangeMethod(ri, contains->entries.front().signature));
    ASSERT_NE(cfd, nullptr);
    EXPECT_EQ(cfd->returnType, reg.read().getType("Bool"));
    ASSERT_EQ(cfd->paramTypes.size(), 1u);
    EXPECT_EQ(cfd->paramTypes[0], int64);

    // count() не зависит от элемента → подстановка не меняет сигнатуру (Int64).
    const auto count = reg.read().methods().findMethodInfo(ri, "count");
    ASSERT_TRUE(count.has_value());
    EXPECT_EQ(reg.interner().instantiateRangeMethod(ri, count->entries.front().signature), count->entries.front().signature);
}

TEST_F(TypeMethodFixture, BigIntegerHasDisplayMethod) {
    TypeSystem& reg = m_ctx.types();
    const TypeId bigInt = reg.read().getType(type::BigInteger);
    const TypeId strChar = reg.read().getType(type::StrChar);
    const TypeId i64 = reg.read().getType(type::Int64);
    ASSERT_NE(bigInt, INVALID_TYPE_ID);

    // display(L) → StrChar, const-нативный метод (ключ "%display^").
    const auto info = reg.read().methods().findMethodInfo(bigInt, "display");
    ASSERT_TRUE(info.has_value());
    EXPECT_EQ(utils::bareName(info->key), "display");
    EXPECT_TRUE(utils::isNativeName(info->key));
    EXPECT_TRUE(utils::isConstName(info->key));

    const auto* fd = reg.read().store().getTypeDataAs<FunctionTypeData>(info->entries.front().signature);
    ASSERT_NE(fd, nullptr);
    EXPECT_EQ(fd->returnType, strChar);
    ASSERT_EQ(fd->paramTypes.size(), 1u);
    EXPECT_EQ(fd->paramTypes[0], i64);

    // Обычное имя находит нативный метод (односторонний поиск).
    EXPECT_EQ(findMethodSig(reg, bigInt, "display"), info->entries.front().signature);
}

TEST_F(TypeMethodFixture, RationalHasDisplayMethod) {
    TypeSystem& reg = m_ctx.types();
    const TypeId rational = reg.read().getType(type::Rational);
    const TypeId strChar = reg.read().getType(type::StrChar);
    const TypeId i64 = reg.read().getType(type::Int64);
    ASSERT_NE(rational, INVALID_TYPE_ID);

    // display(L) → StrChar, const-нативный метод (ключ "%display^").
    const auto info = reg.read().methods().findMethodInfo(rational, "display");
    ASSERT_TRUE(info.has_value());
    EXPECT_EQ(utils::bareName(info->key), "display");
    EXPECT_TRUE(utils::isNativeName(info->key));
    EXPECT_TRUE(utils::isConstName(info->key));

    const auto* fd = reg.read().store().getTypeDataAs<FunctionTypeData>(info->entries.front().signature);
    ASSERT_NE(fd, nullptr);
    EXPECT_EQ(fd->returnType, strChar);
    ASSERT_EQ(fd->paramTypes.size(), 1u);
    EXPECT_EQ(fd->paramTypes[0], i64);
}

TEST_F(TypeMethodFixture, NativeMemberAliasDuplicateIsRejected) {
    // External C++ does not distinguish `using`: a native member declared with an alias and with
    // its base is ONE C++ symbol. Both the pre-check and addMethod must agree (no EXPECT-FAULT).
    TypeSystem& reg = m_ctx.types();
    const TypeId i32 = reg.read().getType(type::Int32);
    const TypeId cls = reg.registrar().registerNativeClass("NativeBox", "native_box", MapperRange{});
    ASSERT_NE(cls, INVALID_TYPE_ID);
    const TypeId alias = reg.registrar().registerType("MyInt", i32);
    ASSERT_NE(alias, INVALID_TYPE_ID);
    reg.methods().addMethod(cls, "f", reg.interner().getOrCreateFunctionType(i32, {i32}));
    EXPECT_TRUE(reg.read().methods().hasOwnMethodSignature(cls, "f", reg.interner().getOrCreateFunctionType(i32, {alias})));
    EXPECT_THROW(reg.methods().addMethod(cls, "f", reg.interner().getOrCreateFunctionType(i32, {alias})), std::runtime_error);
}

} // namespace
} // namespace trust
