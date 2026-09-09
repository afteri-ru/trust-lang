// test/unit/types/operator_registry_test.cpp
// Юнит-тесты реестра перегружаемых операторов (types/operator_registry.hpp):
// реализованный набор, C++-имена, арность, правила member/free.

#include "types/operator_registry.hpp"
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

TEST(OperatorRegistryTest, NotImplementedSymbols) {
    const std::string_view symbols[] = {"+", "-", "*", "/", "%", "<=>", "++", "--", "&&", "||", "=", "&", "|", "^", "~", "!",
                                        "->", "new", ",", "hello", ""};
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
}

TEST(OperatorRegistryTest, Count) {
    EXPECT_EQ(kImplementedOperatorCount, 8u);
}

} // namespace
} // namespace trust::op
