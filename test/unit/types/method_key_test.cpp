#include "types/type_data.hpp"

#include <gtest/gtest.h>

namespace trust {
namespace {

TEST(MethodKeyTest, NativeIsNotInMethodName) {
    // Нативное имя — биндинг/запись; '%' в имени метода — ошибка (EXPECT), не разбор.
    EXPECT_EQ(methodKeyFromSpelling("count^"), trustMethodKey("count", true));
    EXPECT_THROW(methodKeyFromSpelling("%count^"), std::runtime_error);
    const MethodKey key = methodKeyFromSpelling("count^");
    EXPECT_TRUE(key.isConst);
    EXPECT_FALSE(key.isStatic);
}

TEST(MethodKeyTest, ParsesTrustSpelling) {
    const MethodKey key = methodKeyFromSpelling("size");
    EXPECT_EQ(key.name, "size");
    EXPECT_FALSE(key.isConst);
    EXPECT_FALSE(key.isStatic);
}

TEST(MethodKeyTest, ParsesStaticFullName) {
    const MethodKey key = methodKeyFromSpelling("ns::Class::st");
    EXPECT_EQ(key.name, "ns::Class::st");
    EXPECT_TRUE(key.isStatic);
    EXPECT_FALSE(key.isConst);
}

TEST(MethodKeyTest, StructuralEqualityAndOrdering) {
    EXPECT_EQ(trustMethodKey("count", true), methodKeyFromSpelling("count^"));
    EXPECT_NE(trustMethodKey("count", true), trustMethodKey("count", false));
    EXPECT_NE(trustMethodKey("count", false), staticMethodKey("ns::Class::count"));
    // map-ключ: const/non-const варианты одного имени различаются и упорядочены стабильно.
    EXPECT_LT(trustMethodKey("get", false), trustMethodKey("get", true));
}

TEST(MethodKeyTest, NativeIsEntryPropertyNotIdentity) {
    // Один и тот же ключ может быть только в одной форме проекции; нативность живёт в записи.
    const MethodKey key = methodKeyFromSpelling("size");
    const MethodInfo nativeEntry{{}, true};
    EXPECT_EQ(key, trustMethodKey("size", false));
    EXPECT_TRUE(nativeEntry.isNative);
    EXPECT_TRUE(nativeEntry.overloads.empty());
}

TEST(MethodKeyTest, StaticFactoryKeepsQualifiedName) {
    const MethodKey key = staticMethodKey("ns::Class::st");
    EXPECT_EQ(key.name, "ns::Class::st");
    EXPECT_TRUE(key.isStatic);
}

} // namespace
} // namespace trust