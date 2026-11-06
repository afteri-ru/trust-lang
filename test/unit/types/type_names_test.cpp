// test/unit/types/type_names_test.cpp
// Единый предикат «C++-имя встроенного контейнера `:Array`» (types/names/type_names.hpp):
// std::vector / std::array. Второй копии сравнения строк быть не должно (R5).
#include "types/names/type_names.hpp"

#include <gtest/gtest.h>

namespace trust {
namespace {

TEST(TypeNamesBuiltinContainer, MatchesOnlyVectorAndArray) {
    EXPECT_TRUE(trust::type::isBuiltinContainerCppName("std::vector"));
    EXPECT_TRUE(trust::type::isBuiltinContainerCppName("std::array"));
    EXPECT_FALSE(trust::type::isBuiltinContainerCppName("std::pair"));
    EXPECT_FALSE(trust::type::isBuiltinContainerCppName("std::vector<int>"));
    EXPECT_FALSE(trust::type::isBuiltinContainerCppName(""));
}

} // namespace
} // namespace trust
