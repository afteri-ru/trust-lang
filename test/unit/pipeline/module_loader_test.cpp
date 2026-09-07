// module_loader_test.cpp - тесты ModuleLoader: parseSourceModule, ensureLoaded, indexOf
#include "diag/context.hpp"
#include "module_loader/module_loader.hpp"
#include "syntax/macro.h"
#include "syntax/parser.h"
#include "utils/error.hpp"

#include <gtest/gtest.h>
#include <filesystem>
#include <fstream>

namespace trust {
namespace {

class ModuleLoaderTest : public ::testing::Test {
  protected:
    void SetUp() override {
        namespace fs = std::filesystem;
        m_dir = fs::path(TEST_DATA_DIR) / "module_loader_test";
        fs::create_directories(m_dir);
    }

    std::string writeSrc(const std::string& name, const std::string& content) {
        std::string path = (m_dir / (name + ".src")).string();
        std::ofstream ofs(path);
        ofs << content;
        return path;
    }

    std::filesystem::path m_dir;
};

TEST_F(ModuleLoaderTest, ParseSourceModuleLoadsAndCaches) {
    Context ctx;
    ModuleLoader loader(ctx);
    ctx.setLoader(&loader);

    MapperFile modSrc = ctx.source().add_source("\\mod", "func() := {};");
    std::size_t idx1 = ctx.loader().parseSourceModule("\\mod", modSrc);
    ASSERT_TRUE(ctx.loader().isLoaded(idx1));
    ASSERT_NE(ctx.loader().body(idx1), nullptr);
    EXPECT_EQ(ctx.loader().moduleName(idx1), "\\mod");

    auto found = ctx.loader().indexOf("\\mod");
    ASSERT_TRUE(found.has_value());
    EXPECT_EQ(*found, idx1);

    std::size_t idx2 = ctx.loader().parseSourceModule("\\mod", modSrc);
    EXPECT_EQ(idx1, idx2);
}

TEST_F(ModuleLoaderTest, EnsureLoadedReadsSrcFile) {
    Context ctx;
    ModuleLoader loader(ctx);
    ctx.setLoader(&loader);

    std::string mainPath = (m_dir / "main.src").string();
    std::ofstream mainOfs(mainPath);
    mainOfs << "\\mod(func);";
    mainOfs.close();

    writeSrc("mod", "func() := {};");

    MapperFile mainSrc = ctx.source().add_source(mainPath, "\\mod(func);");
    std::size_t mainIdx = ctx.loader().parseSourceModule(mainPath, mainSrc);
    ASSERT_TRUE(ctx.loader().isLoaded(mainIdx));

    auto found = ctx.loader().indexOf("\\mod");
    ASSERT_TRUE(found.has_value());
    ASSERT_TRUE(ctx.loader().isLoaded(*found));
    ASSERT_NE(ctx.loader().body(*found), nullptr);
}

TEST_F(ModuleLoaderTest, CyclicDependencyFaults) {
    Context ctx;
    ModuleLoader loader(ctx);
    ctx.setLoader(&loader);

    MapperFile aSrc = ctx.source().add_source("\\a", "\\a(func);");
    EXPECT_THROW((void)ctx.loader().parseSourceModule("\\a", aSrc), FatalError);

    bool foundFatal = false;
    for (const auto& d : ctx.diag().diagnostics()) {
        if (d.severity == Severity::Fatal && d.message.find("Cyclic module dependency") != std::string::npos) {
            foundFatal = true;
            break;
        }
    }
    EXPECT_TRUE(foundFatal) << "expected a Fatal diagnostic about cyclic dependency";
}

TEST_F(ModuleLoaderTest, MacroScopeIsolationOnModuleLoad) {
    Context ctx;
    ModuleLoader loader(ctx);
    ctx.setLoader(&loader);
    auto macro = std::make_shared<Macro>(ctx);
    ctx.setMacro(macro);

    {
        Parser p(ctx);
        p.ParseText("@@base_macro@@ 1 @@@@;");
    }
    ASSERT_EQ(1, macro->ScopeCount());
    ASSERT_TRUE(macro->GetMacro({"base_macro"}));

    std::string mainPath = (m_dir / "main.src").string();
    std::ofstream mainOfs(mainPath);
    mainOfs << "@@main_macro@@ 1 @@@@;\n"
            << "\\sub(func);\n";
    mainOfs.close();

    writeSrc("sub", "@@sub_macro@@ 1 @@@@;\n");

    std::string mainSource = "@@main_macro@@ 1 @@@@;\n\\sub(func);\n";
    MapperFile mainSrc = ctx.source().add_source(mainPath, mainSource);
    std::size_t mainIdx = ctx.loader().parseSourceModule(mainPath, mainSrc);
    ASSERT_TRUE(ctx.loader().isLoaded(mainIdx));

    EXPECT_EQ(1, macro->ScopeCount());
    EXPECT_TRUE(macro->GetMacro({"base_macro"}));
    EXPECT_FALSE(macro->GetMacro({"main_macro"}));
    EXPECT_FALSE(macro->GetMacro({"sub_macro"}));

    ASSERT_NO_THROW((void)ctx.loader().parseSourceModule(mainPath, mainSrc));
    EXPECT_EQ(1, macro->ScopeCount());
}

TEST_F(ModuleLoaderTest, EnsureLoadedMissingFaults) {
    Context ctx;
    ModuleLoader loader(ctx);
    ctx.setLoader(&loader);

    std::string mainPath = (m_dir / "main2.src").string();
    std::ofstream mainOfs(mainPath);
    mainOfs << "\\nonexistent(func);";
    mainOfs.close();

    MapperFile mainSrc = ctx.source().add_source(mainPath, "\\nonexistent(func);");
    EXPECT_THROW((void)ctx.loader().parseSourceModule(mainPath, mainSrc), FatalError);

    bool foundFatal = false;
    for (const auto& d : ctx.diag().diagnostics()) {
        if (d.severity == Severity::Fatal && d.message.find("not found") != std::string::npos) {
            foundFatal = true;
            break;
        }
    }
    EXPECT_TRUE(foundFatal) << "expected a Fatal diagnostic about missing module";
}

} // namespace
} // namespace trust
