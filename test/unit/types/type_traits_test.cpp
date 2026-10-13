// test/unit/types/type_traits_test.cpp
// Compile-time таблица «альтернатива TypeData <-> TypeDataKind»: значение вида = индекс варианта.
#include "types/registry/type_traits.hpp"
#include "types/type_data.hpp"

#include "gtest/gtest.h"

#include <cstddef>
#include <variant>

namespace trust {
namespace {

TEST(TypeTraitsTest, KindTableMatchesVariant) {
    EXPECT_EQ(kTypeDataKindCount, std::variant_size_v<TypeData>);
    // Границы порядка и совпадение значения с индексом альтернативы.
    EXPECT_EQ(typeDataKindOf<SimpleTypeData>(), TypeDataKind::kSimple);
    EXPECT_EQ(typeDataKindOf<TemplateParamTypeData>(), TypeDataKind::kTemplateParam);

    const TypeData ref{RefTypeData{}};
    EXPECT_EQ(static_cast<std::size_t>(typeDataKindOf<RefTypeData>()), ref.index());
    const TypeData fn{FunctionTypeData{}};
    EXPECT_EQ(static_cast<std::size_t>(typeDataKindOf<FunctionTypeData>()), fn.index());
    const TypeData tuple{TupleTypeData{}};
    EXPECT_EQ(static_cast<std::size_t>(typeDataKindOf<TupleTypeData>()), tuple.index());

    EXPECT_TRUE(isValidTypeDataKind(TypeDataKind::kSimple));
    EXPECT_TRUE(isValidTypeDataKind(TypeDataKind::kTemplateParam));
}

} // namespace
} // namespace trust
