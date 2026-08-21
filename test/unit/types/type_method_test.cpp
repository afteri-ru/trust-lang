// test/unit/types/type_method_test.cpp
// Юнит-тесты нативных методов на встроенных типах (TypeRegistry::addMethod/findMethod):
// тип CString (const char*), метод StrChar.%c_str() и односторонний поиск.

#include "types/registry.hpp"
#include "types/type_names.hpp"
#include "utils/strings.hpp"
#include "diag/context.hpp"
#include "gtest/gtest.h"

#include <memory>
#include <stdexcept>

namespace trust {
namespace {

class TypeMethodFixture : public ::testing::Test {
  protected:
    void SetUp() override {
        m_types = std::make_unique<TypeRegistry>(m_ctx.diag(), m_ctx.opts());
        m_ctx.setTypes(m_types.get());
    }
    Context m_ctx;
    std::unique_ptr<TypeRegistry> m_types;
};

TEST_F(TypeMethodFixture, CStringTypeRegistered) {
    TypeRegistry& reg = m_ctx.types();
    const TypeId cstr = reg.getType("CString");
    ASSERT_NE(cstr, INVALID_TYPE_ID);
    // CString транслируется в `const char*`.
    EXPECT_EQ(reg.getCppTypeName(cstr).value(), "const char*");
}

TEST_F(TypeMethodFixture, StrCharHasCStrMethod) {
    TypeRegistry& reg = m_ctx.types();
    const TypeId strChar = reg.getType("StrChar");
    const TypeId cstr = reg.getType("CString");
    ASSERT_NE(strChar, INVALID_TYPE_ID);

    // Обычное имя находит нативный метод (ОДНА СТОРОНА). Метод - функциональный тип.
    const TypeId m = reg.findMethod(strChar, "c_str");
    ASSERT_NE(m, INVALID_TYPE_ID);
    const auto* fd = reg.getTypeDataAs<FunctionTypeData>(m);
    ASSERT_NE(fd, nullptr);
    EXPECT_EQ(fd->returnType, cstr);
    EXPECT_TRUE(fd->paramTypes.empty());

    // Нативное имя - точное совпадение.
    const TypeId mn = reg.findMethod(strChar, "%c_str");
    ASSERT_NE(mn, INVALID_TYPE_ID);
    EXPECT_EQ(mn, m); // одна и та же сигнатура → один функциональный тип (интернирование)

    // Несуществующий метод - INVALID_TYPE_ID.
    EXPECT_EQ(reg.findMethod(strChar, "nope"), INVALID_TYPE_ID);
}

TEST_F(TypeMethodFixture, StrCharStringMethods) {
    TypeRegistry& reg = m_ctx.types();
    const TypeId strChar = reg.getType("StrChar");
    const TypeId u64 = reg.getType("UInt64");
    const TypeId bo = reg.getType("Bool");
    const TypeId cstr = reg.getType("CString");
    ASSERT_NE(strChar, INVALID_TYPE_ID);

    // size()/length() → UInt64 (размер std::string). Общие сигнатуры интернируются в один тип.
    const auto* sz = reg.getTypeDataAs<FunctionTypeData>(reg.findMethod(strChar, "size"));
    const auto* len = reg.getTypeDataAs<FunctionTypeData>(reg.findMethod(strChar, "length"));
    ASSERT_NE(sz, nullptr);
    ASSERT_NE(len, nullptr);
    EXPECT_EQ(reg.findMethod(strChar, "size"), reg.findMethod(strChar, "length"));
    EXPECT_EQ(sz->returnType, u64);
    EXPECT_EQ(len->returnType, u64);

    // empty() → Bool.
    const auto* em = reg.getTypeDataAs<FunctionTypeData>(reg.findMethod(strChar, "empty"));
    ASSERT_NE(em, nullptr);
    EXPECT_EQ(em->returnType, bo);

    // data() → CString (const char*).
    const auto* dt = reg.getTypeDataAs<FunctionTypeData>(reg.findMethod(strChar, "data"));
    ASSERT_NE(dt, nullptr);
    EXPECT_EQ(dt->returnType, cstr);
    EXPECT_EQ(reg.findMethod(strChar, "data"), reg.findMethod(strChar, "c_str"));
}

// Инвариант «одна форма имени»: повторная регистрация метода в любой из двух форм - ошибка.
// EXPECT бросает std::runtime_error, тесты падают сразу.
TEST_F(TypeMethodFixture, AddMethodRejectsBothNameForms) {
    TypeRegistry& reg = m_ctx.types();
    const TypeId strChar = reg.getType("StrChar");
    const TypeId cstr = reg.getType("CString");
    // "%c_str" уже зарегистрирован в registerBuiltinTypes → попытка "c_str" обязана упасть.
    EXPECT_THROW(reg.addMethod(strChar, "c_str", reg.getOrCreateFunctionType(cstr, {})), std::runtime_error);
}

TEST_F(TypeMethodFixture, AddMethodRejectsExactDuplicate) {
    TypeRegistry& reg = m_ctx.types();
    const TypeId strChar = reg.getType("StrChar");
    const TypeId cstr = reg.getType("CString");
    // Повторная регистрация того же "%c_str" - ошибка.
    EXPECT_THROW(reg.addMethod(strChar, "%c_str", reg.getOrCreateFunctionType(cstr, {})), std::runtime_error);
}

// Универсальный диапазон `:Range` имеет встроенные методы (trust::Range<Elem>): count()/size() →
// Int64, empty() → Bool; алиас trust `length` → нативное C++-имя `count`. Ключи - полная форма
// ('%' нативный, '^' константный); нативность/константность выводятся из ключа (bare_name/
// is_const_name/is_native_name), отдельно не хранятся.
TEST_F(TypeMethodFixture, RangeHasBuiltinMethods) {
    TypeRegistry& reg = m_ctx.types();
    const TypeId range = reg.getType("Range");
    const TypeId int64 = reg.getType("Int64");
    ASSERT_NE(range, INVALID_TYPE_ID);

    const auto count = reg.findMethodInfo(range, "count");
    ASSERT_TRUE(count.has_value());
    EXPECT_EQ(count->funcType, reg.findMethod(range, "count"));
    EXPECT_EQ(utils::bare_name(count->key), "count");
    EXPECT_TRUE(utils::is_native_name(count->key));
    EXPECT_TRUE(utils::is_const_name(count->key)); // %count^
    const auto* fd = reg.getTypeDataAs<FunctionTypeData>(count->funcType);
    ASSERT_NE(fd, nullptr);
    EXPECT_EQ(fd->returnType, int64);
    EXPECT_TRUE(fd->paramTypes.empty());

    // Алиас: trust `length` → нативное `count` (алиас повторяет семантику цели: нативный+константный);
    // findMethodInfo возвращает ключ ЦЕЛИ ("%count^") → нативное имя "count".
    const auto length = reg.findMethodInfo(range, "length");
    ASSERT_TRUE(length.has_value());
    EXPECT_EQ(length->key, count->key);
    EXPECT_EQ(utils::bare_name(length->key), "count");
    EXPECT_EQ(length->funcType, count->funcType);

    // size() - собственное нативное имя.
    const auto size = reg.findMethodInfo(range, "size");
    ASSERT_TRUE(size.has_value());
    EXPECT_EQ(utils::bare_name(size->key), "size");

    // Несуществующий метод.
    EXPECT_FALSE(reg.findMethodInfo(range, "nope").has_value());
}

// Регистрация const ('^') и не-const методов с ОДИНАКОВЫМИ аргументами (перегрузки по
// константности) допустима; точный дубль (та же константность + имя) - ошибка EXPECT.
TEST_F(TypeMethodFixture, AddMethodConstNonConstOverloads) {
    TypeRegistry& reg = m_ctx.types();
    const TypeId base = reg.getType("Int64");
    // Пользовательский (не-алиас) тип: enum имеет собственный дескриптор (не канонизируется в базовый).
    const TypeId t = reg.registerEnumType("MyType", base, std::vector<EnumMemberData>{{"A", "0"}});
    ASSERT_NE(t, INVALID_TYPE_ID);
    const TypeId sig = reg.getOrCreateFunctionType(base, {});
    reg.addMethod(t, "%get^", sig); // const-перегрузка
    reg.addMethod(t, "%get", sig);  // не-const перегрузка - допустима (другая константность)
    const auto info = reg.findMethodInfo(t, "get");
    ASSERT_TRUE(info.has_value());
    EXPECT_EQ(utils::bare_name(info->key), "get");
    EXPECT_EQ(info->funcType, sig);
    // Точный дубль (та же константность) - ошибка.
    EXPECT_THROW(reg.addMethod(t, "%get^", sig), std::runtime_error);
}

// Параметризованный Range<Elem> (C++-модель шаблонов): структурный тип, интернируемый по
// элементному типу; методы объявлены на абстрактном :Range с типовым параметром T и
// подставляются (T→Elem) в instantiateRangeMethod → точные сигнатуры.
TEST_F(TypeMethodFixture, RangeParametricSubstitution) {
    TypeRegistry& reg = m_ctx.types();
    const TypeId int64 = reg.getType("Int64");
    const TypeId rational = reg.getType("Rational");
    const TypeId ri = reg.getOrCreateRangeType(int64);
    const TypeId rr = reg.getOrCreateRangeType(rational);
    EXPECT_TRUE(reg.isRangeType(ri));
    EXPECT_TRUE(reg.isRangeType(rr));
    EXPECT_NE(ri, rr); // Range<Int64> и Range<Rational> - разные типы
    EXPECT_EQ(reg.rangeElementType(ri), int64);
    EXPECT_EQ(reg.rangeElementType(rr), rational);
    EXPECT_EQ(reg.getOrCreateRangeType(int64), ri); // интернирование по elementType

    // `at(Int64) → T`: findMethodInfo(Range<Int64>) находит метод на абстрактном :Range,
    // instantiateRangeMethod подставляет T→Int64.
    const auto at = reg.findMethodInfo(ri, "at");
    ASSERT_TRUE(at.has_value());
    const auto* fd = reg.getTypeDataAs<FunctionTypeData>(reg.instantiateRangeMethod(ri, at->funcType));
    ASSERT_NE(fd, nullptr);
    EXPECT_EQ(fd->returnType, int64); // at(i) → Int64 (не типовой параметр)
    ASSERT_EQ(fd->paramTypes.size(), 1u);
    EXPECT_EQ(fd->paramTypes[0], int64);

    // start() → T → Int64.
    const auto start = reg.findMethodInfo(ri, "start");
    ASSERT_TRUE(start.has_value());
    const auto* sfd = reg.getTypeDataAs<FunctionTypeData>(reg.instantiateRangeMethod(ri, start->funcType));
    ASSERT_NE(sfd, nullptr);
    EXPECT_EQ(sfd->returnType, int64);

    // contains(T) → Bool: параметр T→Int64.
    const auto contains = reg.findMethodInfo(ri, "contains");
    ASSERT_TRUE(contains.has_value());
    const auto* cfd = reg.getTypeDataAs<FunctionTypeData>(reg.instantiateRangeMethod(ri, contains->funcType));
    ASSERT_NE(cfd, nullptr);
    EXPECT_EQ(cfd->returnType, reg.getType("Bool"));
    ASSERT_EQ(cfd->paramTypes.size(), 1u);
    EXPECT_EQ(cfd->paramTypes[0], int64);

    // count() не зависит от элемента → подстановка не меняет сигнатуру (Int64).
    const auto count = reg.findMethodInfo(ri, "count");
    ASSERT_TRUE(count.has_value());
    EXPECT_EQ(reg.instantiateRangeMethod(ri, count->funcType), count->funcType);
}

} // namespace
} // namespace trust
