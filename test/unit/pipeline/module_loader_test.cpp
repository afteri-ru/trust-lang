// module_loader_test.cpp - тесты ModuleLoader: parseSourceModule, resolveModules, find*
#include "session/context.hpp"
#include "module_loader/module_export.hpp"
#include "module_loader/module_loader.hpp"
#include "package/package.hpp"
#include "syntax/macro.h"
#include "syntax/parser.h"
#include "types/names.hpp"
#include "utils/error.hpp"
#include "utils/strings.hpp"

#include <gtest/gtest.h>
#include <filesystem>
#include <fstream>
#include <optional>
#include <string>
#include <string_view>
#include <vector>

#include "test_data.hpp"

namespace trust {
namespace {

/// Индекс ЗАГРУЖЕННОГО модуля по логическому имени (заголовку). Публичного lookup-по-spelling
/// API у загрузчика нет; тесту нужна идентичность уже загруженного модуля.
std::optional<ModuleIndex> findLoadedByLogicalName(const ModuleLoader& loader, std::string_view logicalName) {
    for (std::size_t i = 0; i < loader.moduleCount(); ++i) {
        const ModuleIndex idx{i};
        if (loader.isLoaded(idx) && loader.moduleLogicalName(idx) == logicalName) {
            return idx;
        }
    }
    return std::nullopt;
}

class ModuleLoaderTest : public ::testing::Test {
  protected:
    void SetUp() override { m_dir = trust::test::makeTestDataDir("module_loader_test"); }

    std::string writeSrc(const std::string& name, const std::string& content) {
        std::string path = (m_dir / (name + ".src")).string();
        std::ofstream ofs(path);
        ofs << content;
        return path;
    }

    std::string writeSrcAt(const std::string& rel, const std::string& content) {
        const std::filesystem::path path = m_dir / (rel + ".src");
        std::error_code ec;
        std::filesystem::create_directories(path.parent_path(), ec);
        std::ofstream ofs(path);
        ofs << content;
        return path.string();
    }

    std::filesystem::path m_dir;
};

TEST_F(ModuleLoaderTest, ParseSourceModuleLoadsAndCaches) {
    Context ctx;
    ModuleLoader loader(ctx);
    ctx.setModuleResolver(&loader);

    MapperFile modSrc = ctx.source().add_source("\\mod", "func() := {};");
    ModuleIndex idx1 = loader.parseSourceModule("\\mod", modSrc);
    ASSERT_TRUE(loader.isLoaded(idx1));
    EXPECT_EQ(loader.modulePath(idx1), "\\mod");

    ModuleIndex idx2 = loader.parseSourceModule("\\mod", modSrc);
    EXPECT_EQ(idx1, idx2);
}

TEST_F(ModuleLoaderTest, EnsureLoadedReadsSrcFile) {
    Context ctx;
    ModuleLoader loader(ctx);
    ctx.setModuleResolver(&loader);

    std::string mainPath = (m_dir / "main.src").string();
    std::ofstream mainOfs(mainPath);
    mainOfs << "\\mod(func);";
    mainOfs.close();

    writeSrc("mod", "\\mod;\nfunc() := {};");

    MapperFile mainSrc = ctx.source().add_source(mainPath, "\\mod(func);");
    ModuleIndex mainIdx = loader.parseSourceModule(mainPath, mainSrc, {}, m_dir);
    ASSERT_TRUE(loader.isLoaded(mainIdx));

    auto found = findLoadedByLogicalName(loader, "\\mod");
    ASSERT_TRUE(found.has_value());
    ASSERT_TRUE(loader.isLoaded(*found));
}

TEST_F(ModuleLoaderTest, FindLoadedUsesSpellingDomainOnly) {
    Context ctx;
    ModuleLoader loader(ctx);
    ctx.setModuleResolver(&loader);

    const std::string mainPath = (m_dir / "main_lookup.src").string();
    {
        std::ofstream ofs(mainPath);
        ofs << "\\mod(func);";
    }
    writeSrc("mod", "\\mod;\nfunc() := {};");
    MapperFile mainSrc = ctx.source().add_source(mainPath, "\\mod(func);");
    ASSERT_TRUE(loader.isLoaded(loader.parseSourceModule(mainPath, mainSrc, {}, m_dir)));

    const auto dep = findLoadedByLogicalName(loader, "\\mod");
    ASSERT_TRUE(dep.has_value());
    // Ссылка и проверка загруженности — домен spelling (сайт импорта).
    EXPECT_EQ(loader.findLoaded("\\mod"), dep);
    EXPECT_TRUE(loader.isModuleLoaded("\\mod"));
    // Канонический путь — НЕ spelling: в этом домене ссылка не резолвится.
    EXPECT_FALSE(loader.findLoaded(loader.modulePath(*dep)).has_value());
    EXPECT_FALSE(loader.isModuleLoaded(loader.modulePath(*dep)));
    EXPECT_FALSE(loader.isModuleLoaded("\\missing"));
}

TEST_F(ModuleLoaderTest, SiteProjectionIsSharedPerMasks) {
    Context ctx;
    ModuleLoader loader(ctx);
    ctx.setModuleResolver(&loader);

    writeSrc("mod", "\\mod;\nf() := {};\ng() := {};");
    const std::string mainPath = (m_dir / "main_shared_exports.src").string();
    const std::string source = "\\mod(f);";
    {
        std::ofstream ofs(mainPath);
        ofs << source;
    }
    MapperFile mainSrc = ctx.source().add_source(mainPath, source);
    ASSERT_TRUE(loader.isLoaded(loader.parseSourceModule(mainPath, mainSrc, {}, m_dir)));
    const auto dep = findLoadedByLogicalName(loader, "\\mod");
    ASSERT_TRUE(dep.has_value());

    const auto first = loader.siteProjection(*dep, utils::ImportMasks::parse("f,g"));
    const auto second = loader.siteProjection(*dep, utils::ImportMasks::parse("f,g"));
    ASSERT_TRUE(first && first->exports);
    // Один и тот же объект проекции на (модуль, маски): копий нет.
    EXPECT_EQ(first.get(), second.get());
    EXPECT_EQ(first->exports->size(), 2u);

    const auto different = loader.siteProjection(*dep, utils::ImportMasks::parse("f"));
    ASSERT_TRUE(different && different->exports);
    EXPECT_NE(first.get(), different.get());
    EXPECT_EQ(different->exports->size(), 1u);

    // Порядок масок не значим (единая нормализация): тот же кэш-объект, копий нет.
    const auto reordered = loader.siteProjection(*dep, utils::ImportMasks::parse("g,f"));
    EXPECT_EQ(first.get(), reordered.get());
}

// Проекция сайта не расходится с правилом видимости `types/site_visibility.hpp`:
// для каждого набора масок множество узлов проекции ≡ объявления, для которых правило даёт
// `VisibleFlat`; protected-ось (флаг + несовпавшие маски) проверяется тем же правилом.
TEST_F(ModuleLoaderTest, SiteProjectionMatchesVisibilityRule) {
    Context ctx;
    ModuleLoader loader(ctx);
    ctx.setModuleResolver(&loader);

    writeSrc("mod", "\\mod;\nf() := {};\nns:: {\n    g() := {};\n};\n_:: {\n    h() := {};\n};\n");
    const std::string mainPath = (m_dir / "main_projection_rule.src").string();
    const std::string source = "\\mod();";
    {
        std::ofstream ofs(mainPath);
        ofs << source;
    }
    MapperFile mainSrc = ctx.source().add_source(mainPath, source);
    const ModuleIndex importer = loader.parseSourceModule(mainPath, mainSrc, {}, m_dir);
    const auto dep = findLoadedByLogicalName(loader, "\\mod");
    ASSERT_TRUE(dep.has_value());

    const ModuleInterfaceFacts& facts = loader.interfaceFacts(*dep);
    ASSERT_EQ(facts.entries.size(), 2u) << "публичный набор: f, ns::g";
    ASSERT_EQ(facts.protectedEntries.size(), 1u) << "protected-набор: h";

    std::vector<std::string_view> publicNames;
    for (const ExportEntry& entry : facts.entries) {
        publicNames.push_back(entry.qualifiedName);
    }
    std::vector<std::string_view> protectedNames;
    for (const ExportEntry& entry : facts.protectedEntries) {
        protectedNames.push_back(entry.qualifiedName);
    }

    for (const std::string& maskText : {std::string{}, std::string("f"), std::string("ns::*"), std::string("h"), std::string("nomatch")}) {
        const utils::ImportMasks masks = utils::ImportMasks::parse(maskText);
        const auto projection = loader.siteProjection(*dep, masks);
        ASSERT_TRUE(projection) << "masks='" << maskText << "'";

        std::vector<std::string> expected;
        for (const ExportEntry& entry : facts.entries) {
            const SiteVisibilityInput input{importer,
                                            *dep,
                                            SiteDeclVisibility::Public,
                                            utils::namespacePathOf(entry.qualifiedName),
                                            utils::shortNameOf(entry.qualifiedName),
                                            masks,
                                            SiteAccess::Direct,
                                            /*protectedAllowed=*/true};
            if (siteVisibilityOf(input) == SiteVisibility::VisibleFlat) {
                expected.push_back(entry.qualifiedName);
            }
        }
        std::vector<std::string> actual;
        for (const ExportEntry& entry : facts.entries) {
            for (const AstNodePtr& node : *projection->exports) {
                if (node.get() == entry.node.get()) {
                    actual.push_back(entry.qualifiedName);
                    break;
                }
            }
        }
        EXPECT_EQ(actual, expected) << "проекция расходится с правилом для масок '" << maskText << "'";

        bool expectedProtectedVisible = false;
        for (const ExportEntry& entry : facts.protectedEntries) {
            const SiteVisibilityInput input{importer,
                                            *dep,
                                            SiteDeclVisibility::Protected,
                                            utils::namespacePathOf(entry.qualifiedName),
                                            utils::shortNameOf(entry.qualifiedName),
                                            masks,
                                            SiteAccess::Direct,
                                            /*protectedAllowed=*/true};
            expectedProtectedVisible = expectedProtectedVisible || siteVisibilityOf(input) == SiteVisibility::VisibleFlat;
        }
        EXPECT_EQ(projection->hasProtectedExports, expectedProtectedVisible) << "маски '" << maskText << "'";
        EXPECT_EQ(projection->protectedUnmatchedMasks, masks.unmatchedIn(protectedNames)) << "маски '" << maskText << "'";
        EXPECT_EQ(projection->unmatchedMasks, masks.unmatchedIn(publicNames)) << "маски '" << maskText << "'";
    }
}

TEST_F(ModuleLoaderTest, GroupSpellingIsLoadedButNotAReference) {
    Context ctx;
    ModuleLoader loader(ctx);
    ctx.setModuleResolver(&loader);

    writeSrcAt("grp/a", "\\\\grp\\a;\na() := {};");
    writeSrcAt("grp/b", "\\\\grp\\b;\nb() := {};");
    const std::string mainPath = (m_dir / "main_group_ref.src").string();
    const std::string source = R"(\\grp();)";
    {
        std::ofstream ofs(mainPath);
        ofs << source;
    }
    MapperFile mainSrc = ctx.source().add_source(mainPath, source);
    ASSERT_TRUE(loader.isLoaded(loader.parseSourceModule(mainPath, mainSrc, {}, m_dir)));
    // Групповой spelling загружен, но ссылкой не является: `\grp::name` неоднозначен.
    EXPECT_TRUE(loader.isModuleLoaded(R"(\\grp)"));
    EXPECT_FALSE(loader.findLoaded(R"(\\grp)").has_value());
}

TEST_F(ModuleLoaderTest, CyclicDependencyFaults) {
    Context ctx;
    ModuleLoader loader(ctx);
    ctx.setModuleResolver(&loader);

    MapperFile aSrc = ctx.source().add_source("\\a", "\\a(func);");
    EXPECT_THROW((void)loader.parseSourceModule("\\a", aSrc), FatalError);

    bool foundFatal = false;
    bool hasChain = false;
    for (const auto& d : ctx.diag().diagnostics()) {
        if (d.severity == Severity::Fatal && d.message.find("Cyclic module dependency") != std::string::npos) {
            foundFatal = true;
            // Диагностика несёт цепочку импорта: "<путь> -> ... -> <замыкающий id>".
            hasChain = d.message.find(" -> ") != std::string::npos;
            break;
        }
    }
    EXPECT_TRUE(foundFatal) << "expected a Fatal diagnostic about cyclic dependency";
    EXPECT_TRUE(hasChain) << "cyclic dependency diagnostic must include the import chain";
}

TEST_F(ModuleLoaderTest, MacroScopeIsolationOnModuleLoad) {
    Context ctx;
    ModuleLoader loader(ctx);
    ctx.setModuleResolver(&loader);
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

    writeSrc("sub", "\\sub;\n@@sub_macro@@ 1 @@@@;\n");

    std::string mainSource = "@@main_macro@@ 1 @@@@;\n\\sub(func);\n";
    MapperFile mainSrc = ctx.source().add_source(mainPath, mainSource);
    ModuleIndex mainIdx = loader.parseSourceModule(mainPath, mainSrc, {}, m_dir);
    ASSERT_TRUE(loader.isLoaded(mainIdx));

    EXPECT_EQ(1, macro->ScopeCount());
    EXPECT_TRUE(macro->GetMacro({"base_macro"}));
    EXPECT_FALSE(macro->GetMacro({"main_macro"}));
    EXPECT_FALSE(macro->GetMacro({"sub_macro"}));

    ASSERT_NO_THROW((void)loader.parseSourceModule(mainPath, mainSrc, {}, m_dir));
    EXPECT_EQ(1, macro->ScopeCount());
}

TEST_F(ModuleLoaderTest, EnsureLoadedMissingFaults) {
    Context ctx;
    ModuleLoader loader(ctx);
    ctx.setModuleResolver(&loader);

    std::string mainPath = (m_dir / "main2.src").string();
    std::ofstream mainOfs(mainPath);
    mainOfs << "\\nonexistent(func);";
    mainOfs.close();

    MapperFile mainSrc = ctx.source().add_source(mainPath, "\\nonexistent(func);");
    EXPECT_THROW((void)loader.parseSourceModule(mainPath, mainSrc, {}, m_dir), FatalError);

    bool foundFatal = false;
    for (const auto& d : ctx.diag().diagnostics()) {
        if (d.severity == Severity::Fatal && d.message.find("not found") != std::string::npos) {
            foundFatal = true;
            break;
        }
    }
    EXPECT_TRUE(foundFatal) << "expected a Fatal diagnostic about missing module";
}

TEST_F(ModuleLoaderTest, EnsureLoadedFromTrustPackage) {
    Context ctx;
    ModuleLoader loader(ctx);
    ctx.setModuleResolver(&loader);
    loader.setPackageCacheDir(m_dir / "pkgcache");
    const std::filesystem::path pkg_dir = m_dir / "pkgsrc";
    std::error_code ec;
    std::filesystem::create_directories(pkg_dir, ec);
    {
        std::ofstream ofs(pkg_dir / "trust.toml");
        ofs << "name = \"mod\"\n";
    }
    {
        std::ofstream ofs(pkg_dir / "mod.src");
        ofs << "\\mod;\nfunc() := {};\n";
    }
    ASSERT_EQ(createTrustPackage(pkg_dir, m_dir / "mod.trust"), 0);

    const std::string mainPath = (m_dir / "mainpkg.src").string();
    {
        std::ofstream ofs(mainPath);
        ofs << "\\\\\\mod\\mod(func);\n";
    }
    MapperFile mainSrc = ctx.source().add_source(mainPath, "\\\\\\mod\\mod(func);\n");
    ModuleIndex mainIdx = loader.parseSourceModule(mainPath, mainSrc, {}, m_dir);
    ASSERT_TRUE(loader.isLoaded(mainIdx));

    auto found = findLoadedByLogicalName(loader, "\\\\\\mod\\mod");
    ASSERT_TRUE(found.has_value());
    ASSERT_TRUE(loader.isLoaded(*found));
    bool extracted = false;
    for (const auto& entry : std::filesystem::directory_iterator(m_dir / "pkgcache")) {
        if (entry.is_directory() && entry.path().filename().string().rfind("mod-", 0) == 0 && std::filesystem::exists(entry.path() / "mod.src")) {
            extracted = true;
            break;
        }
    }
    EXPECT_TRUE(extracted) << "expected extracted package cache directory mod-<md5>/mod.src";
}
TEST_F(ModuleLoaderTest, ModuleDirectoryFile) {
    Context ctx;
    ModuleLoader loader(ctx);
    ctx.setModuleResolver(&loader);

    writeSrcAt("lib/mod", "\\\\lib\\mod;\nfunc() := {};");
    const std::string source = R"(\\lib\mod();)";
    const std::string mainPath = (m_dir / "main_dir.src").string();
    {
        std::ofstream ofs(mainPath);
        ofs << source;
    }
    MapperFile mainSrc = ctx.source().add_source(mainPath, source);
    const ModuleIndex mainIdx = loader.parseSourceModule(mainPath, mainSrc, {}, m_dir);
    ASSERT_TRUE(loader.isLoaded(mainIdx));

    auto found = findLoadedByLogicalName(loader, R"(\\lib\mod)");
    ASSERT_TRUE(found.has_value());
    ASSERT_TRUE(loader.isLoaded(*found));
}

TEST_F(ModuleLoaderTest, ModuleSearchPathFile) {
    Context ctx;
    ModuleLoader loader(ctx);
    ctx.setModuleResolver(&loader);

    writeSrcAt("ext/mod", "\\mod;\nfunc() := {};");
    loader.setModuleSearchPaths({m_dir / "ext"});
    const std::string source = R"(\mod();)";
    const std::string mainPath = (m_dir / "main_ext.src").string();
    {
        std::ofstream ofs(mainPath);
        ofs << source;
    }
    MapperFile mainSrc = ctx.source().add_source(mainPath, source);
    const ModuleIndex mainIdx = loader.parseSourceModule(mainPath, mainSrc, {}, m_dir);
    ASSERT_TRUE(loader.isLoaded(mainIdx));

    auto found = findLoadedByLogicalName(loader, R"(\mod)");
    ASSERT_TRUE(found.has_value());
    ASSERT_TRUE(loader.isLoaded(*found));
}

TEST_F(ModuleLoaderTest, FilePathCannotHaveSubpaths) {
    Context ctx;
    ModuleLoader loader(ctx);
    ctx.setModuleResolver(&loader);

    const std::string source = R"(\dir\mod();)";
    const std::string mainPath = (m_dir / "main_bad.src").string();
    MapperFile mainSrc = ctx.source().add_source(mainPath, source);
    EXPECT_THROW((void)loader.parseSourceModule(mainPath, mainSrc, {}, m_dir), FatalError);
    bool found = false;
    for (const auto& d : ctx.diag().diagnostics()) {
        if (d.message.find("cannot have subpaths") != std::string::npos) {
            found = true;
            break;
        }
    }
    EXPECT_TRUE(found) << "expected a diagnostic about a file subpath";
}

TEST_F(ModuleLoaderTest, AmbiguousModuleAcrossRoots) {
    Context ctx;
    ModuleLoader loader(ctx);
    ctx.setModuleResolver(&loader);

    writeSrc("mod", "func() := {};");
    writeSrcAt("ext/mod", "func() := {};");
    loader.setModuleSearchPaths({m_dir / "ext"});
    const std::string source = R"(\mod();)";
    const std::string mainPath = (m_dir / "main_amb.src").string();
    MapperFile mainSrc = ctx.source().add_source(mainPath, source);
    EXPECT_THROW((void)loader.parseSourceModule(mainPath, mainSrc, {}, m_dir), FatalError);
    bool found = false;
    for (const auto& d : ctx.diag().diagnostics()) {
        if (d.message.find("ambiguous") != std::string::npos) {
            found = true;
            break;
        }
    }
    EXPECT_TRUE(found) << "expected an ambiguity diagnostic";
}

TEST_F(ModuleLoaderTest, DirectoryImportRequiresModulePath) {
    Context ctx;
    ModuleLoader loader(ctx);
    ctx.setModuleResolver(&loader);

    std::error_code ec;
    std::filesystem::create_directories(m_dir / "lib", ec);
    const std::string source = R"(\lib();)";
    const std::string mainPath = (m_dir / "main_group.src").string();
    MapperFile mainSrc = ctx.source().add_source(mainPath, source);
    EXPECT_THROW((void)loader.parseSourceModule(mainPath, mainSrc, {}, m_dir), FatalError);
    bool found = false;
    for (const auto& d : ctx.diag().diagnostics()) {
        if (d.message.find("directory import requires a module path") != std::string::npos) {
            found = true;
            break;
        }
    }
    EXPECT_TRUE(found) << "expected a diagnostic about a directory needing a module path";
}

TEST_F(ModuleLoaderTest, HeaderMismatchIsDiagnosed) {
    Context ctx;
    ModuleLoader loader(ctx);
    ctx.setModuleResolver(&loader);

    writeSrcAt("lib/mod", "\\wrong;\nfunc() := {};");
    const std::string source = R"(\\lib\mod();)";
    const std::string mainPath = (m_dir / "main_hdr_bad.src").string();
    MapperFile mainSrc = ctx.source().add_source(mainPath, source);
    (void)loader.parseSourceModule(mainPath, mainSrc, {}, m_dir);
    bool found = false;
    for (const auto& d : ctx.diag().diagnostics()) {
        if (d.message.find("does not match path") != std::string::npos) {
            found = true;
            break;
        }
    }
    EXPECT_TRUE(found) << "expected a header/path mismatch diagnostic";
}

TEST_F(ModuleLoaderTest, MissingHeaderIsDiagnosed) {
    Context ctx;
    ModuleLoader loader(ctx);
    ctx.setModuleResolver(&loader);

    writeSrcAt("lib/mod", "func() := {};");
    const std::string source = R"(\\lib\mod();)";
    const std::string mainPath = (m_dir / "main_hdr_missing.src").string();
    MapperFile mainSrc = ctx.source().add_source(mainPath, source);
    (void)loader.parseSourceModule(mainPath, mainSrc, {}, m_dir);
    bool found = false;
    for (const auto& d : ctx.diag().diagnostics()) {
        if (d.message.find("has no module header") != std::string::npos) {
            found = true;
            break;
        }
    }
    EXPECT_TRUE(found) << "expected a missing-header diagnostic";
}

TEST_F(ModuleLoaderTest, ModuleGroupImportLoadsAllFiles) {
    Context ctx;
    ModuleLoader loader(ctx);
    ctx.setModuleResolver(&loader);

    writeSrcAt("lib/a", "\\\\lib\\a;\nfunc() := {};");
    writeSrcAt("lib/b", "\\\\lib\\b;\nfunc() := {};");
    const std::string source = "\\\\lib();";
    const std::string mainPath = (m_dir / "main_group_import.src").string();
    MapperFile mainSrc = ctx.source().add_source(mainPath, source);
    (void)loader.parseSourceModule(mainPath, mainSrc, {}, m_dir);

    const std::vector<ModuleIndex> group = loader.resolveModules("\\\\lib", {});
    EXPECT_EQ(group.size(), 2u);
    for (const ModuleIndex idx : group) {
        EXPECT_TRUE(loader.isLoaded(idx));
    }
}

TEST_F(ModuleLoaderTest, ModuleGroupSubdirectoryLoadsFiles) {
    Context ctx;
    ModuleLoader loader(ctx);
    ctx.setModuleResolver(&loader);

    writeSrcAt("lib/sub/a", "\\\\lib\\sub\\a;\nfunc() := {};");
    const std::string source = "\\\\lib\\sub();";
    const std::string mainPath = (m_dir / "main_group_sub.src").string();
    MapperFile mainSrc = ctx.source().add_source(mainPath, source);
    (void)loader.parseSourceModule(mainPath, mainSrc, {}, m_dir);

    const std::vector<ModuleIndex> group = loader.resolveModules("\\\\lib\\sub", {});
    EXPECT_EQ(group.size(), 1u);
    EXPECT_TRUE(loader.isLoaded(group.front()));
}

TEST_F(ModuleLoaderTest, ModuleGroupFileDirectoryAmbiguityFaults) {
    Context ctx;
    ModuleLoader loader(ctx);
    ctx.setModuleResolver(&loader);

    writeSrc("lib", "func() := {};");
    std::error_code ec;
    std::filesystem::create_directories(m_dir / "lib", ec);
    const std::string source = "\\\\lib();";
    const std::string mainPath = (m_dir / "main_group_amb.src").string();
    MapperFile mainSrc = ctx.source().add_source(mainPath, source);
    EXPECT_THROW((void)loader.parseSourceModule(mainPath, mainSrc, {}, m_dir), FatalError);
}

TEST_F(ModuleLoaderTest, PackageRootGroupLoadsRootAndModuleFiles) {
    Context ctx;
    ModuleLoader loader(ctx);
    ctx.setModuleResolver(&loader);
    loader.setPackageCacheDir(m_dir / "pkgcache_group");

    const std::filesystem::path pkg_dir = m_dir / "pkgsrc_group";
    std::error_code ec;
    std::filesystem::create_directories(pkg_dir / "m", ec);
    {
        std::ofstream ofs(pkg_dir / "trust.toml");
        ofs << "name = \"pkg\"\n";
    }
    {
        std::ofstream ofs(pkg_dir / "a.src");
        ofs << "\\\\\\pkg\\a;\na() := {};";
    }
    {
        std::ofstream ofs(pkg_dir / "m" / "b.src");
        ofs << "\\\\\\pkg\\m\\b;\nb() := {};";
    }
    ASSERT_EQ(createTrustPackage(pkg_dir, m_dir / "pkg.trust"), 0);

    const std::string mainPath = (m_dir / "main_pkg_group.src").string();
    MapperFile mainSrc = ctx.source().add_source(mainPath, "\\\\\\pkg();");
    (void)loader.parseSourceModule(mainPath, mainSrc, {}, m_dir);

    const std::vector<ModuleIndex> group = loader.resolveModules("\\\\\\pkg", {});
    EXPECT_EQ(group.size(), 2u);
    for (const ModuleIndex idx : group) {
        EXPECT_TRUE(loader.isLoaded(idx));
    }
}

TEST_F(ModuleLoaderTest, PackageModuleRootAppliesToHeadersAndPaths) {
    Context ctx;
    ModuleLoader loader(ctx);
    ctx.setModuleResolver(&loader);
    loader.setPackageCacheDir(m_dir / "pkgcache_root");

    const std::filesystem::path pkg_dir = m_dir / "pkgsrc_root";
    std::error_code ec;
    std::filesystem::create_directories(pkg_dir / "src", ec);
    {
        std::ofstream ofs(pkg_dir / "trust.toml");
        ofs << "name = \"pkg\"\nmodule-root = \"src\"\n";
    }
    {
        std::ofstream ofs(pkg_dir / "src" / "a.src");
        ofs << "\\\\\\pkg\\a;\na() := {};";
    }
    ASSERT_EQ(createTrustPackage(pkg_dir, m_dir / "pkg.trust"), 0);

    const std::string mainPath = (m_dir / "main_pkg_root.src").string();
    MapperFile mainSrc = ctx.source().add_source(mainPath, "\\\\\\pkg\\a();");
    (void)loader.parseSourceModule(mainPath, mainSrc, {}, m_dir);

    auto found = findLoadedByLogicalName(loader, "\\\\\\pkg\\a");
    ASSERT_TRUE(found.has_value());
    EXPECT_TRUE(loader.isLoaded(*found));
}

TEST_F(ModuleLoaderTest, PackageRelativeImportPreservesPackageName) {
    Context ctx;
    ModuleLoader loader(ctx);
    ctx.setModuleResolver(&loader);
    loader.setPackageCacheDir(m_dir / "pkgcache_relative");

    const std::filesystem::path pkg_dir = m_dir / "pkgsrc_relative";
    std::error_code ec;
    std::filesystem::create_directories(pkg_dir / "grp", ec);
    {
        std::ofstream ofs(pkg_dir / "trust.toml");
        ofs << "name = \"pkg\"\n";
    }
    {
        std::ofstream ofs(pkg_dir / "a.src");
        ofs << "\\a;\n\\b(fa);\n\\\\grp\\c(fc);\na():Int32 := { fa() };\n";
    }
    {
        std::ofstream ofs(pkg_dir / "b.src");
        ofs << "\\b;\nfa():Int32 := { 1 };\n";
    }
    {
        std::ofstream ofs(pkg_dir / "grp" / "c.src");
        ofs << "\\\\grp\\c;\nfc():Int32 := { 2 };\n";
    }
    ASSERT_EQ(createTrustPackage(pkg_dir, m_dir / "pkg.trust"), 0);

    const std::string mainPath = (m_dir / "main_pkg_relative.src").string();
    MapperFile mainSrc = ctx.source().add_source(mainPath, "\\\\\\pkg\\a(a);");
    (void)loader.parseSourceModule(mainPath, mainSrc, {}, m_dir);

    for (const auto& d : ctx.diag().diagnostics()) {
        EXPECT_TRUE(d.severity != Severity::Error) << d.message;
    }
    const auto fileDep = findLoadedByLogicalName(loader, "\\\\\\pkg\\b");
    ASSERT_TRUE(fileDep.has_value());
    EXPECT_TRUE(loader.isLoaded(*fileDep));
    EXPECT_EQ(loader.moduleLogicalName(*fileDep), "\\\\\\pkg\\b");
    const auto groupDep = findLoadedByLogicalName(loader, "\\\\\\pkg\\grp\\c");
    ASSERT_TRUE(groupDep.has_value());
    EXPECT_TRUE(loader.isLoaded(*groupDep));
    EXPECT_EQ(loader.moduleLogicalName(*groupDep), "\\\\\\pkg\\grp\\c");
}

} // namespace
} // namespace trust
