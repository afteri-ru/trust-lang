// test/unit/pipeline/link_plan_test.cpp
// Юнит-тесты резолвера link-plan: [[artifact]] / [[link.headers]] -> LIBS.
// Ключевой сценарий - `requires`: артефакт тянет свои зависимости даже когда
// использованный заголовок их не перечисляет (архив содержит gmp-зависимые объекты).
#include "toolchain/build/link_plan.hpp"
#include "trust/version.h"

#include <gtest/gtest.h>

#include <filesystem>
#include <fstream>
#include <string>
#include <vector>

#include "test_data.hpp"

namespace fs = std::filesystem;
using namespace trust;

namespace {

void writeFile(const fs::path& p, const std::string& content) {
    fs::create_directories(p.parent_path());
    std::ofstream ofs(p, std::ios::binary);
    ofs << content;
}

/// Пакет-провайдер в <root>/stdlib (именно этот каталог сканирует resolveLinkPlan).
/// Готовый пакет требует версию компилятора: упаковщик всегда проставляет её сам.
void writeProvider(const fs::path& stdlib_root, const std::string& manifest) {
    writeFile(stdlib_root / "trust.toml", "name = \"stdlib\"\nversion = \"" + std::string(TRUST_VERSION_SHORT) + "\"\n" + manifest);
}

} // namespace

TEST(LinkPlan, RequiresIsPulledByArtifactNotByHeader) {
    const fs::path root = trust::test::makeTestDataDir("link_plan_requires");
    const fs::path stdlib = root / "stdlib";
    writeProvider(stdlib, "[[artifact]]\n"
                          "provides = [\"rt\"]\n"
                          "kind = \"system\"\n"
                          "libs = [\"rt\"]\n"
                          "requires = [\"gmp\"]\n"
                          "[[artifact]]\n"
                          "provides = [\"gmp\"]\n"
                          "kind = \"system\"\n"
                          "libs = [\"gmp\"]\n"
                          "[[link.headers]]\n"
                          "header = \"trust/io.hpp\"\n"
                          "libs = [\"rt\"]\n");

    toolchain::LinkPlan plan;
    std::string error;
    ASSERT_TRUE(toolchain::resolveLinkPlan(root, stdlib, {"trust/io.hpp"}, {}, plan, error)) << error;

    // gmp не упомянут в [[link.headers]], но обязан попасть в LIBS через requires.
    const std::vector<std::string> expected = {"-lrt", "-lgmp"};
    EXPECT_EQ(plan.libs, expected);
}

TEST(LinkPlan, UnusedArtifactDoesNotContributeRequires) {
    const fs::path root = trust::test::makeTestDataDir("link_plan_unused");
    const fs::path stdlib = root / "stdlib";
    writeProvider(stdlib, "[[artifact]]\n"
                          "provides = [\"rt\"]\n"
                          "kind = \"system\"\n"
                          "libs = [\"rt\"]\n"
                          "requires = [\"gmp\"]\n"
                          "[[artifact]]\n"
                          "provides = [\"gmp\"]\n"
                          "kind = \"system\"\n"
                          "libs = [\"gmp\"]\n");
    writeFile(stdlib / "unrelated.hpp", "");

    toolchain::LinkPlan plan;
    std::string error;
    ASSERT_TRUE(toolchain::resolveLinkPlan(root, stdlib, {"unrelated.hpp"}, {}, plan, error)) << error;
    EXPECT_TRUE(plan.libs.empty()) << "extraction alone must not add link flags";
}

TEST(LinkPlan, TransitiveRequiresAcrossArtifacts) {
    const fs::path root = trust::test::makeTestDataDir("link_plan_transitive");
    const fs::path stdlib = root / "stdlib";
    writeProvider(stdlib, "[[artifact]]\n"
                          "provides = [\"a\"]\n"
                          "kind = \"system\"\n"
                          "libs = [\"a\"]\n"
                          "requires = [\"b\"]\n"
                          "[[artifact]]\n"
                          "provides = [\"b\"]\n"
                          "kind = \"system\"\n"
                          "libs = [\"b\"]\n"
                          "requires = [\"c\"]\n"
                          "[[artifact]]\n"
                          "provides = [\"c\"]\n"
                          "kind = \"system\"\n"
                          "libs = [\"c\"]\n"
                          "[[link.headers]]\n"
                          "header = \"h.hpp\"\n"
                          "libs = [\"a\"]\n");

    toolchain::LinkPlan plan;
    std::string error;
    ASSERT_TRUE(toolchain::resolveLinkPlan(root, stdlib, {"h.hpp"}, {}, plan, error)) << error;
    const std::vector<std::string> expected = {"-la", "-lb", "-lc"};
    EXPECT_EQ(plan.libs, expected);
}

TEST(LinkPlan, RequiresCycleTerminates) {
    const fs::path root = trust::test::makeTestDataDir("link_plan_cycle");
    const fs::path stdlib = root / "stdlib";
    writeProvider(stdlib, "[[artifact]]\n"
                          "provides = [\"a\"]\n"
                          "kind = \"system\"\n"
                          "libs = [\"a\"]\n"
                          "requires = [\"b\"]\n"
                          "[[artifact]]\n"
                          "provides = [\"b\"]\n"
                          "kind = \"system\"\n"
                          "libs = [\"b\"]\n"
                          "requires = [\"a\"]\n"
                          "[[link.headers]]\n"
                          "header = \"h.hpp\"\n"
                          "libs = [\"a\"]\n");

    toolchain::LinkPlan plan;
    std::string error;
    ASSERT_TRUE(toolchain::resolveLinkPlan(root, stdlib, {"h.hpp"}, {}, plan, error)) << error;
    const std::vector<std::string> expected = {"-la", "-lb"};
    EXPECT_EQ(plan.libs, expected);
}
