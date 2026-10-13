// module_export_test.cpp - тесты сбора экспорт-интерфейса и glob-фильтра модулей
#include "module_loader/module_export.hpp"
#include "utils/import_masks.hpp"
#include "module_loader/module_loader.hpp"
#include "ast/term_to_ast.hpp"
#include "session/context.hpp"
#include "syntax/term/term.h"
#include "utils/strings.hpp"

#include <gtest/gtest.h>
#include <memory>
#include <string_view>
#include <vector>

namespace trust {
namespace {

TEST(ModuleExportTest, GlobExactAndWildcards) {
    EXPECT_TRUE(utils::globMatch("func", "func"));
    EXPECT_FALSE(utils::globMatch("func", "func2"));
    EXPECT_TRUE(utils::globMatch("func*", "func2"));
    EXPECT_TRUE(utils::globMatch("f*", "func"));
    EXPECT_TRUE(utils::globMatch("f?nc", "func"));
    EXPECT_FALSE(utils::globMatch("f?nc", "func2"));
    EXPECT_TRUE(utils::globMatch("*", "anything"));
    EXPECT_TRUE(utils::globMatch("a*b*c", "axxbyyc"));
    EXPECT_TRUE(utils::globMatch("", ""));
    EXPECT_FALSE(utils::globMatch("", "x"));
}

TEST(ModuleExportTest, ImportMasksParsing) {
    const auto masks = utils::ImportMasks::parse("func, x");
    EXPECT_TRUE(masks.matches("x"));
    // Маска без `::` матчится с КОРОТКИМ именем (в т.ч. namespaced объявления).
    EXPECT_TRUE(masks.matches("ns::x"));
    EXPECT_TRUE(masks.matches("func"));
    EXPECT_FALSE(masks.matches("y"));

    const auto glob = utils::ImportMasks::parse(" na*e , other");
    EXPECT_TRUE(glob.matches("name"));

    const auto empty = utils::ImportMasks::parse("");
    EXPECT_TRUE(empty.empty());
    EXPECT_TRUE(empty.matches("anything")); // пустой фильтр = всё

    const auto blanks = utils::ImportMasks::parse("   ");
    EXPECT_TRUE(blanks.matches("anything"));

    // Пустые элементы между запятыми игнорируются (не превращаются в «без фильтра»).
    const auto commas = utils::ImportMasks::parse("a,,b");
    EXPECT_EQ(commas.patterns().size(), 2u);
    EXPECT_TRUE(commas.matches("a"));
    EXPECT_TRUE(commas.matches("b"));
    EXPECT_FALSE(commas.matches("anything"));
}

TEST(ModuleExportTest, ImportMasksQualifiedMatching) {
    // Маска с `::` матчится с ПОЛНЫМ trust-именем, без `::` - с коротким.
    EXPECT_TRUE(utils::ImportMasks::parse("f").matches("ns::f"));
    EXPECT_TRUE(utils::ImportMasks::parse("*").matches("ns::f"));
    EXPECT_TRUE(utils::ImportMasks::parse("ns::f").matches("ns::f"));
    EXPECT_FALSE(utils::ImportMasks::parse("ns::g").matches("ns::f"));
    EXPECT_FALSE(utils::ImportMasks::parse("other::*").matches("ns::f"));
    EXPECT_TRUE(utils::ImportMasks::parse("").matches("ns::f")); // пустой фильтр = всё
}

TEST(ModuleExportTest, ImportMasksUnmatchedIn) {
    // Несовпавшие маски считаются по views (копий qualified-имён нет).
    const std::vector<std::string_view> names{"f", "ns::g"};
    EXPECT_TRUE(utils::ImportMasks::parse("f, ns::g").unmatchedIn(names).empty());

    const auto unmatched = utils::ImportMasks::parse("f, missing").unmatchedIn(names);
    ASSERT_EQ(unmatched.size(), 1u);
    EXPECT_EQ(unmatched[0], "missing");

    // Пустой фильтр = «без фильтра»: несовпавших масок нет (и имена не нужны).
    EXPECT_TRUE(utils::ImportMasks::parse("").unmatchedIn({}).empty());
}

TEST(ModuleExportTest, ImportMasksNormalizedKeyIsOrderInsensitive) {
    // ЕДИНАЯ нормализация масок: порядок и дубликаты не значимы (ключ кэша фильтра и
    // дедупликация сайтов импорта опираются на неё).
    const auto a = utils::ImportMasks::parse("f, ns::g");
    const auto b = utils::ImportMasks::parse("ns::g,f");
    EXPECT_EQ(a.normalizedKey(), b.normalizedKey());
    EXPECT_TRUE(a.sameAs(b));
    EXPECT_EQ(utils::ImportMasks::parse("f, f,  ns::g").normalizedKey(), a.normalizedKey());
    EXPECT_NE(utils::ImportMasks::parse("f").normalizedKey(), a.normalizedKey());
    // Кодирование длиной исключает коллизии вида "ab,c" vs "a,bc".
    EXPECT_NE(utils::ImportMasks::parse("ab,c").normalizedKey(), utils::ImportMasks::parse("a,bc").normalizedKey());
}

TEST(ModuleExportTest, ImportMasksParsedOncePerImportSite) {
    // Сайт импорта хранит только разобранный набор масок: `utils::ImportMasks` - единственный
    // парсер, поэтому потребители (импорт-экспорт, семантика) не разбирают строку повторно.
    ModuleNode site(ModuleIndex{0}, std::string("\\mod"));
    site.setImportMasks("func, ns::x");
    const std::vector<std::string> patterns = site.importMaskSet().patterns();
    ASSERT_EQ(patterns.size(), 2u);
    EXPECT_EQ(patterns[0], "func");
    EXPECT_EQ(patterns[1], "ns::x");
    EXPECT_TRUE(site.importMaskSet().matches("ns::x"));
}

TEST(ModuleExportTest, PartSharesModuleBodyWithoutCopy) {
    // Тело модуля разделяется: часть держит тот же вектор узлов, что и владелец (копий списка нет).
    ModuleNode node(ModuleIndex{0}, std::string("\\mod"));
    auto first = std::make_shared<Sequence>(ParserToken::Kind::sequence);
    auto second = std::make_shared<Sequence>(ParserToken::Kind::sequence);
    auto body = std::make_shared<std::vector<AstNodePtr>>();
    body->push_back(first);
    body->push_back(second);
    node.parts().push_back(ModuleNode::ModulePart{ModuleIndex{0}, body, {}});
    node.parts().push_back(ModuleNode::ModulePart{ModuleIndex{1}, body, {}});
    node.parts().push_back(ModuleNode::ModulePart{ModuleIndex{2}, nullptr, {}});

    ASSERT_NE(node.parts()[0].body, nullptr);
    EXPECT_EQ(node.parts()[0].body, node.parts()[1].body); // один и тот же вектор
    ASSERT_EQ(node.parts()[0].body->size(), 2u);
    EXPECT_EQ((*node.parts()[1].body)[1].get(), second.get());
    EXPECT_EQ(node.parts()[2].body, nullptr);
}

TEST(ModuleExportTest, ScanSeparatesInterfaceEntriesByOrigin) {
    // ОДИН обход тела: происхождение записи (top-level узел) отделяет объявления блока интерфейса
    // от остального тела — второй обход дерева для ownExports не нужен.
    auto iface_fn = std::make_shared<FuncDecl>(std::string("iface_fn"));
    auto block = std::make_shared<ScopeBlock>(std::string("\\facade"));
    block->setModuleInterface();
    block->m_body.push_back(iface_fn);
    auto body_fn = std::make_shared<FuncDecl>(std::string("body_fn"));

    const std::vector<AstNodePtr> body{block, body_fn};
    const ModuleBodyScan scan = scanModuleBody(body);
    ASSERT_EQ(scan.entries.size(), 2u);
    EXPECT_EQ(scan.entries[0].container, block.get());
    EXPECT_EQ(scan.entries[1].container, body_fn.get());
    EXPECT_EQ(scan.interfaceBlock, block.get());
    EXPECT_TRUE(scan.namespaceAliases.empty());
}

TEST(ModuleExportTest, NamespaceAliasOutsideInterfaceIsNotExported) {
    // Namespace-алиас экспортируется ТОЛЬКО из блока интерфейса: алиас в теле не попадает в
    // namespaceAliases единого обхода.
    auto target = std::make_shared<IdentName>(std::string("\\lib::sub"));
    target->moduleRef = ModuleIndex{1};
    auto alias = std::make_shared<FuncDecl>(std::string("ns::"));
    alias->setAlias(AliasKind::Namespace, target, {});

    const ModuleBodyScan scan = scanModuleBody({alias});
    EXPECT_TRUE(scan.entries.empty());
    EXPECT_TRUE(scan.namespaceAliases.empty());
}

TEST(ModuleExportTest, CollectsExportedNamespaceAlias) {
    auto target = std::make_shared<IdentName>(std::string("\\lib::sub"));
    target->moduleRef = ModuleIndex{1};
    auto alias = std::make_shared<FuncDecl>(std::string("ns::"));
    alias->setAlias(AliasKind::Namespace, target, {});

    auto block = std::make_shared<ScopeBlock>(std::string("\\facade"));
    block->setModuleInterface();
    block->m_body.push_back(alias);
    const ModuleInterfaceFacts facts = collectModuleInterface({block});
    ASSERT_EQ(facts.namespaceAliases.size(), 1u);
    EXPECT_EQ(facts.namespaceAliases.front().alias, "ns");
    EXPECT_EQ(facts.namespaceAliases.front().targetModule, ModuleIndex{1});
    EXPECT_EQ(facts.namespaceAliases.front().path, "\\lib::sub");
}

TEST(ModuleExportTest, CollectsNestedExportedNamespaceAlias) {
    auto target = std::make_shared<IdentName>(std::string("\\lib::sub"));
    target->moduleRef = ModuleIndex{1};
    auto alias = std::make_shared<FuncDecl>(std::string("ns::"));
    alias->setAlias(AliasKind::Namespace, target, {});

    auto nested = std::make_shared<ScopeBlock>(std::string("outer::"));
    nested->m_body.push_back(alias);

    auto block = std::make_shared<ScopeBlock>(std::string("\\facade"));
    block->setModuleInterface();
    block->m_body.push_back(nested);
    const ModuleInterfaceFacts facts = collectModuleInterface({block});
    ASSERT_EQ(facts.namespaceAliases.size(), 1u);
    EXPECT_EQ(facts.namespaceAliases.front().alias, "outer::ns");
    EXPECT_EQ(facts.namespaceAliases.front().targetModule, ModuleIndex{1});
    EXPECT_EQ(facts.namespaceAliases.front().path, "\\lib::sub");
}

// -- Интеграция: сбор экспортов из загруженного модуля --

class ModuleExportIntegrationTest : public ::testing::Test {
  protected:
    void SetUp() override {
        m_ctx = std::make_unique<Context>();
        m_loader = std::make_unique<ModuleLoader>(*m_ctx);
        m_ctx->setModuleResolver(m_loader.get());
    }

    std::vector<AstNodePtr> collect(const char* src, const char* masks) {
        MapperFile modSrc = m_ctx->source().add_source("\\mod", src);
        ModuleIndex idx = m_loader->parseSourceModule("\\mod", modSrc);
        return filterExportEntries(scanModuleBody(m_loader->bodyAst(idx)).entries, {}, utils::ImportMasks::parse(masks));
    }

    std::unique_ptr<Context> m_ctx;
    std::unique_ptr<ModuleLoader> m_loader;
};

TEST_F(ModuleExportIntegrationTest, CollectsGlobalExportsExcludingAnonymous) {
    // x и func - экспортируемые; hidden в private-области `__` - нет.
    auto all = collect("x:Int32 := 42;\n%func():Int32 := { 42 };\n__ { hidden:Int32 := 1; };\n", "");
    EXPECT_EQ(all.size(), 2u);
}

TEST_F(ModuleExportIntegrationTest, FilterByMask) {
    auto funcs = collect("x:Int32 := 42;\n%func():Int32 := { 42 };\n", "func");
    ASSERT_EQ(funcs.size(), 1u);
    // Имя функции - без `%`-префикса в сопоставлении масок; терм-источник нативного func.
    auto x = collect("x:Int32 := 42;\n%func():Int32 := { 42 };\n", "x");
    ASSERT_EQ(x.size(), 1u);
    auto none = collect("x:Int32 := 42;\n", "nope");
    EXPECT_TRUE(none.empty());
}

TEST_F(ModuleExportIntegrationTest, FilterWildcard) {
    auto matches = collect("alpha:Int32 := 1;\nalpha2:Int32 := 2;\nbeta:Int32 := 3;\n", "alpha*");
    EXPECT_EQ(matches.size(), 2u);
}

TEST_F(ModuleExportIntegrationTest, FilterEntriesWithParsedMasks) {
    MapperFile modSrc = m_ctx->source().add_source("\\mod", "x:Int32 := 1;\nfunc():Int32 := { 1 };\n");
    const ModuleIndex idx = m_loader->parseSourceModule("\\mod", modSrc);
    const auto entries = scanModuleBody(m_loader->bodyAst(idx)).entries;
    std::vector<std::string> unmatched;
    const auto filtered = filterExportEntries(entries, {}, utils::ImportMasks::parse("func, nope"), &unmatched);
    ASSERT_EQ(filtered.size(), 1u);
    ASSERT_EQ(unmatched.size(), 1u);
    EXPECT_EQ(unmatched.front(), "nope");
}

} // namespace
} // namespace trust