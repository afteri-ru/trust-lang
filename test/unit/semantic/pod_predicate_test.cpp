// test/unit/semantic/pod_predicate_test.cpp
// R4/R13 cross-check: частичный POD-предикат (semantic/decl/pod_predicate.hpp) согласован с
// языковой моделью — точно-не-POD типы (класс-инстанс, строка, Any, изменяемый массив) отвергаются,
// скаляры и вложенные Struct — нет. Если предикат будет изменён неверно, тест падает.
#include "semantic/decl/pod_predicate.hpp"
#include "types/type_registry_fixture.hpp"

#include <gtest/gtest.h>

namespace trust::test {
namespace {

TEST_F(TypeSystemFixture, PodPredicateClassifiesFieldTypes) {
    const TypeId i32 = m_ctx.readTypes().getType("Int32");
    const TypeId str = m_ctx.readTypes().getType("StrChar");
    const TypeId any = m_ctx.readTypes().getType("Any");
    ASSERT_NE(i32, INVALID_TYPE_ID);
    ASSERT_NE(str, INVALID_TYPE_ID);
    ASSERT_NE(any, INVALID_TYPE_ID);

    EXPECT_FALSE(semantic::isDefinitelyNonPodFieldType(i32, model())); // скаляр — POD
    EXPECT_TRUE(semantic::isDefinitelyNonPodFieldType(str, model()));  // std::string
    EXPECT_TRUE(semantic::isDefinitelyNonPodFieldType(any, model()));  // std::any

    const TypeId cls = types().registrar().defineRecord("PodCheckClass", Group::kClassDefs, {});
    ASSERT_NE(cls, INVALID_TYPE_ID);
    EXPECT_TRUE(semantic::isDefinitelyNonPodFieldType(cls, model())); // класс-инстанс

    const TypeId arr = types().interner().getOrCreateArrayType(i32);
    ASSERT_NE(arr, INVALID_TYPE_ID);
    EXPECT_TRUE(semantic::isDefinitelyNonPodFieldType(arr, model())); // std::vector

    const TypeId st = types().registrar().defineRecord("PodCheckStruct", Group::kStructs, {});
    ASSERT_NE(st, INVALID_TYPE_ID);
    EXPECT_FALSE(semantic::isDefinitelyNonPodFieldType(st, model())); // вложенный Struct — POD
    EXPECT_FALSE(semantic::isDefinitelyNonPodFieldType(INVALID_TYPE_ID, model()));
}

} // namespace
} // namespace trust::test
