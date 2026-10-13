// test/unit/types/stack_check_test.cpp
// Тесты контроля переполнения стека: интринсики (types/intrinsics.hpp) и режим
// --stack-check (semantic/ref/stack_check.hpp).
#include "types/intrinsics.hpp"
#include "semantic/ref/stack_check.hpp"
#include "gtest/gtest.h"

#include <set>
#include <string>

namespace trust {
namespace {

// Компайлтайм-инварианты: новые интринсики имеют корректные имена и требуют рантайм-заголовок.
static_assert(intrinsicName(IntrinsicId::kTrustStackCheck) == "trust::intrinsic_stack_check");
static_assert(intrinsicName(IntrinsicId::kTrustStackCheckLimit) == "trust::intrinsic_stack_check_limit");
static_assert(intrinsicName(IntrinsicId::kTrustStackCheckReserve) == "trust::intrinsic_stack_check_reserve");

TEST(StackCheckIntrinsicsTest, NamesAndHeadersResolve) {
    EXPECT_TRUE(findIntrinsicByName("trust::intrinsic_stack_check").has_value());
    EXPECT_TRUE(findIntrinsicByName("trust::intrinsic_stack_check_limit").has_value());
    EXPECT_TRUE(findIntrinsicByName("trust::intrinsic_stack_check_reserve").has_value());
    EXPECT_FALSE(findIntrinsicByName("trust::intrinsic_bogus").has_value());

    for (const IntrinsicId id : {IntrinsicId::kTrustStackCheck, IntrinsicId::kTrustStackCheckLimit, IntrinsicId::kTrustStackCheckReserve}) {
        const auto headers = intrinsicHeaders(id);
        ASSERT_FALSE(headers.empty());
        EXPECT_EQ(headers[0], "@trust/stack_check.hpp");
    }
}

TEST(StackCheckModeTest, ParseAndNamesAreTotal) {
    EXPECT_EQ(analysis::parseStackCheckMode("off"), analysis::StackCheckMode::kOff);
    EXPECT_EQ(analysis::parseStackCheckMode("explicit"), analysis::StackCheckMode::kExplicit);
    EXPECT_EQ(analysis::parseStackCheckMode("recursion"), analysis::StackCheckMode::kRecursion);
    EXPECT_EQ(analysis::parseStackCheckMode("auto"), analysis::StackCheckMode::kAuto);
    EXPECT_FALSE(analysis::parseStackCheckMode("bogus").has_value());
    EXPECT_FALSE(analysis::parseStackCheckMode("").has_value());

    std::set<std::string> names;
    for (std::size_t i = 0; i < analysis::kStackCheckModeCount; ++i) {
        const auto mode = static_cast<analysis::StackCheckMode>(i);
        const std::string name(analysis::stackCheckModeName(mode));
        EXPECT_FALSE(name.empty());
        EXPECT_TRUE(names.insert(name).second);
    }
}

} // namespace
} // namespace trust
