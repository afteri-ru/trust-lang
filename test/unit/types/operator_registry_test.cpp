// test/unit/types/operator_registry_test.cpp
// Юнит-тесты реестра перегружаемых операторов (types/operator_registry.hpp):
// реализованный набор, C++-имена, арность, правила member/free.

#include "types/call.hpp"
#include "types/type_system.hpp"
#include "types/names.hpp"
#include "session/context.hpp"
#include "gtest/gtest.h"

#include <string_view>

namespace trust::op {
namespace {

TEST(OperatorRegistryTest, ImplementedComparisons) {
    const std::string_view symbols[] = {"==", "!=", "<", ">", "<=", ">="};
    for (const std::string_view s : symbols) {
        const OperatorInfo* op = findOperator(s);
        ASSERT_NE(op, nullptr) << s;
        EXPECT_EQ(op->cppSpelling, s);
        EXPECT_EQ(op->arity, Arity::kBinary);
        // Сравнения допустимы и как член, и как свободная функция.
        EXPECT_TRUE(op->allowMember) << s;
        EXPECT_TRUE(op->allowFree) << s;
        EXPECT_TRUE(isImplementedOperator(s)) << s;
    }
}

TEST(OperatorRegistryTest, ImplementedCallAndIndexAreMemberOnly) {
    const OperatorInfo* call = findOperator("()");
    ASSERT_NE(call, nullptr);
    EXPECT_EQ(call->cppSpelling, "()");
    EXPECT_EQ(call->arity, Arity::kCall);
    EXPECT_TRUE(call->allowMember);
    EXPECT_FALSE(call->allowFree); // C++: operator() не бывает свободным

    const OperatorInfo* index = findOperator("[]");
    ASSERT_NE(index, nullptr);
    EXPECT_EQ(index->cppSpelling, "[]");
    EXPECT_EQ(index->arity, Arity::kIndex);
    EXPECT_TRUE(index->allowMember);
    EXPECT_FALSE(index->allowFree); // C++: operator[] не бывает свободным
}

TEST(OperatorRegistryTest, UnaryAndIncDec) {
    const OperatorInfo* deref = findOperator("*");
    ASSERT_NE(deref, nullptr);
    EXPECT_EQ(deref->cppSpelling, "*");
    EXPECT_EQ(deref->arity, Arity::kUnary);
    EXPECT_TRUE(deref->allowMember);
    EXPECT_TRUE(deref->allowFree);

    for (const std::string_view s : {std::string_view("++"), std::string_view("--")}) {
        const OperatorInfo* op = findOperator(s);
        ASSERT_NE(op, nullptr) << s;
        EXPECT_EQ(op->cppSpelling, s);
        EXPECT_EQ(op->arity, Arity::kIncDec);
        EXPECT_TRUE(op->allowMember);
        EXPECT_FALSE(op->allowFree);
        EXPECT_TRUE(isIncDecOperator(s)) << s;
    }
    EXPECT_FALSE(isIncDecOperator("*"));
}

TEST(OperatorRegistryTest, NotImplementedSymbols) {
    const std::string_view symbols[] = {"+", "-", "/", "%", "<=>", "&&", "||", "=", "&", "|", "^", "~", "!", "->", "new", ",", "hello", ""};
    for (const std::string_view s : symbols) {
        EXPECT_EQ(findOperator(s), nullptr) << s;
        EXPECT_FALSE(isImplementedOperator(s)) << s;
        EXPECT_EQ(operatorCppName(s), std::string{}) << s;
    }
}

TEST(OperatorRegistryTest, CppNames) {
    EXPECT_EQ(operatorCppName("=="), "operator==");
    EXPECT_EQ(operatorCppName("!="), "operator!=");
    EXPECT_EQ(operatorCppName("()"), "operator()");
    EXPECT_EQ(operatorCppName("[]"), "operator[]");
    EXPECT_EQ(operatorCppName("*"), "operator*");
    EXPECT_EQ(operatorCppName("++"), "operator++");
    EXPECT_EQ(operatorCppName("--"), "operator--");
}

TEST(OperatorRegistryTest, DunderNames) {
    const OperatorInfo* eq = findByFunctionName("__eq__");
    ASSERT_NE(eq, nullptr);
    EXPECT_EQ(eq->symbol, "==");
    EXPECT_EQ(operatorCppNameByFunction("__eq__"), "operator==");
    EXPECT_EQ(symbolOfFunction("__getitem__"), "[]");
    EXPECT_EQ(functionNameOfSymbol("[]"), "__getitem__");
    EXPECT_TRUE(isSystemName("__eq__"));
    EXPECT_FALSE(isSystemName("__lambda__0_1")); // внутреннее имя без хвостового '__'
    EXPECT_FALSE(isSystemName("plain"));
    EXPECT_TRUE(isOperatorFunctionName("__call__"));
    EXPECT_FALSE(isOperatorFunctionName("__frob__"));
}

TEST(OperatorRegistryTest, SuffixStableAcrossRegistryOrder) {
    trust::Context ca;
    trust::Context cb;
    TypeSystem ra(ca.diag(), ca.opts());
    TypeSystem rb(cb.diag(), cb.opts());
    const TypeId i32a = ra.read().getType(type::Int32);
    const TypeId i32b = rb.read().getType(type::Int32);
    // Разный порядок создания пользовательских типов - суффикс обязан совпасть.
    const TypeId pa = ra.registrar().registerType("P", i32a);
    const TypeId qa = ra.registrar().registerType("Q", i32a);
    (void)qa;
    const TypeId qb = rb.registrar().registerType("Q", i32b);
    (void)qb;
    const TypeId pb = rb.registrar().registerType("P", i32b);
    // Сигнатура с ОДНИМ И ТЕМ ЖЕ пользовательским алиасом: суффикс стабилен (не зависит от индексов).
    EXPECT_EQ(ra.interner().getOrCreateFunctionType(i32a, {pa}), ra.interner().getOrCreateFunctionType(i32a, {pa}));
    EXPECT_NE(pa, pb); // разные реестры -> разные TypeId (индексы), но identity совпадает по имени
    const FunctionIdentity& ida = ra.identity();
    const FunctionIdentity& idb = rb.identity();
    EXPECT_EQ(ida.cppSuffix(ra.interner().getOrCreateFunctionType(i32a, {pa}), {}), idb.cppSuffix(rb.interner().getOrCreateFunctionType(i32b, {pb}), {}));
    // Пользовательский алиас — НОМИНАЛЬНО отдельный тип: суффикс отличается от базового Int32.
    EXPECT_NE(ida.cppSuffix(ra.interner().getOrCreateFunctionType(i32a, {pa}), {}), ida.cppSuffix(ra.interner().getOrCreateFunctionType(i32a, {i32a}), {}));
    // Встроенный representation-алиас прозрачен: Integer и Int64 дают один суффикс.
    const auto integer = ra.read().findType("Integer");
    ASSERT_TRUE(integer.has_value());
    EXPECT_EQ(ida.cppSuffix(ra.interner().getOrCreateFunctionType(i32a, {*integer}), {}), ida.cppSuffix(ra.interner().getOrCreateFunctionType(i32a, {ra.read().getType(type::Int64)}), {}));
    // Другая сигнатура (Int64) - другой суффикс.
    EXPECT_NE(ida.cppSuffix(ra.interner().getOrCreateFunctionType(i32a, {pa}), {}), ida.cppSuffix(ra.interner().getOrCreateFunctionType(i32a, {ra.read().getType(type::Int64)}), {}));
}

TEST(OperatorRegistryTest, SuffixDistinguishesFunctionTypedParams) {
    trust::Context ctx;
    TypeSystem reg(ctx.diag(), ctx.opts());
    const TypeId i32 = reg.read().getType(type::Int32);
    const TypeId b = reg.read().getType(type::Bool);
    const TypeId fn1 = reg.interner().getOrCreateFunctionType(i32, {i32});
    const TypeId fn2 = reg.interner().getOrCreateFunctionType(b, {i32});
    const TypeId h1 = reg.interner().getOrCreateFunctionType(i32, {fn1});
    const TypeId h2 = reg.interner().getOrCreateFunctionType(i32, {fn2});
    EXPECT_NE(reg.identity().cppSuffix(h1, {}), reg.identity().cppSuffix(h2, {}));
}

TEST(OperatorRegistryTest, SuffixContainsTemplateParamPosition) {
    trust::Context ctx;
    TypeSystem reg(ctx.diag(), ctx.opts());
    const TypeId t = reg.interner().getOrCreateTemplateParamType("T");
    const TypeId sig = reg.interner().getOrCreateFunctionType(t, {t});
    const TemplateBinders binders{{"T", false}};
    const std::string suffix = reg.identity().cppSuffix(sig, binders);
    // Позиционная identity: параметр кодируется позицией (T0), а не свободным именем.
    EXPECT_NE(suffix.find("T0"), std::string::npos);
    EXPECT_EQ(suffix.find("P0"), std::string::npos);
}

TEST(OperatorRegistryTest, Count) {
    EXPECT_EQ(kImplementedOperatorCount, 11u);
}

} // namespace
} // namespace trust::op
