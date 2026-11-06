#include "pipeline/backend/backend.hpp"
#include "pipeline/run/target_options.hpp"
#include "target/target.hpp"

#include "driver/options.hpp"

#include <gtest/gtest.h>

#include <string>
#include <type_traits>
#include <utility>

namespace trust {
namespace {

// Целевой контракт — данные владельца `TargetContract` (R37/E20): значения считаются один раз из
// нейтральной модели опций и хранятся у владельца; стадии получают невладеющую ссылку на контракт.
TEST(TargetContract, DefaultValues) {
    const TargetContract contract;
    EXPECT_EQ(contract.targetInfo().cpp_standard, std::string(kDefaultCppStandard));
    EXPECT_TRUE(contract.targetInfo().triple.empty());
    EXPECT_TRUE(contract.targetInfo().features.empty());
}

TEST(TargetContract, OwnerStoresValuesFromOptions) {
    PipelineOpts opts;
    opts.compiler = "clang++-22";
    const TargetContract contract(targetInfoFromOptions(opts));
    EXPECT_EQ(contract.targetInfo().provider, "clang++-22");
    EXPECT_EQ(contract.targetInfo().cpp_standard, std::string(kDefaultCppStandard));
}

TEST(TargetContract, StageRequestReferencesOwner) {
    // Контракт — невладеющая ссылка на владельца значений, а не копия его фактов (R1/R37).
    static_assert(std::is_same_v<decltype(std::declval<backend::BackendRequest>().target), const TargetContract*>,
                  "BackendRequest must reference the target contract owner, not copy its values (R1/R37)");
}

} // namespace
} // namespace trust