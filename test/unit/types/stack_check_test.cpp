// test/unit/types/stack_check_test.cpp
// Тесты контроля переполнения стека: интринсики (types/intrinsics.hpp) и режим
// --stack-check (semantic/stack_check.hpp).

#include "types/intrinsics.hpp"
#include "semantic/stack_check.hpp"
#include "gtest/gtest.h"

#include <set>
#include <string>

namespace trust {
namespace {

// Компайлтайм-инварианты: новые интринсики имеют корректные имена и требуют рантайм-заголовок.
static_assert(intrinsicName(IntrinsicId::kTrustStackCheck) == "trust::intrinsic_stack_check");
static_assert(intrinsicName(IntrinsicId::kTrustStackCheckLimit) == "trust::intrinsic_stack_check_limit");
static_assert(intrinsicName(IntrinsicId::kTrustStackCheckReserve) == "trust::intrinsic_stack_check_reserve");

TEST(StackCheckIntrinsicsTest, NamesResolve) {
    EXPECT_TRUE(findIntrinsicByName("trust::intrinsic_stack_check").has_value());
    EXPECT_TRUE(findIntrinsicByName("trust::intrinsic_stack_check_limit").has_value());
    EXPECT_TRUE(findIntrinsicByName("trust::intrinsic_stack_check_reserve").has_value());
    EXPECT_FALSE(findIntrinsicByName("trust::intrinsic_bogus").has_value());
}

TEST(StackCheckIntrinsicsTest, HeadersRequireStackCheckHeader) {
    for (const IntrinsicId id : {IntrinsicId::kTrustStackCheck, IntrinsicId::kTrustStackCheckLimit, IntrinsicId::kTrustStackCheckReserve}) {
        const auto headers = intrinsicHeaders(id);
        ASSERT_FALSE(headers.empty());
        EXPECT_EQ(headers[0], "@trust/stack_check.hpp");
    }
}

TEST(StackCheckModeTest, ParseAcceptsKnownModes) {
    EXPECT_EQ(semantic::parseStackCheckMode("off"), semantic::StackCheckMode::kOff);
    EXPECT_EQ(semantic::parseStackCheckMode("explicit"), semantic::StackCheckMode::kExplicit);
    EXPECT_EQ(semantic::parseStackCheckMode("recursion"), semantic::StackCheckMode::kRecursion);
    EXPECT_EQ(semantic::parseStackCheckMode("auto"), semantic::StackCheckMode::kAuto);
    EXPECT_FALSE(semantic::parseStackCheckMode("bogus").has_value());
    EXPECT_FALSE(semantic::parseStackCheckMode("").has_value());
}

TEST(StackCheckModeTest, NamesAreUnique) {
    std::set<std::string> names;
    for (std::size_t i = 0; i < semantic::kStackCheckModeCount; ++i) {
        const auto m = static_cast<semantic::StackCheckMode>(i);
        const std::string n(semantic::stackCheckModeName(m));
        EXPECT_FALSE(n.empty());
        EXPECT_TRUE(names.insert(n).second);
    }
}

} // namespace
} // namespace trust
