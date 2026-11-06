#include "symbols/decl/name_resolver.hpp"

#include "ast/ast_nodes.hpp"
#include "ast/token.hpp"
#include "gtest/gtest.h"
#include "session/context.hpp"
#include "syntax/term/term.h"
#include "syntax/term/term_types.h"
#include "types/type_system.hpp"
#include "types/names.hpp"

#include <memory>

namespace trust {
namespace {

// -- NameResolver: единая точка резолва имён --------------------------------------------------

class NameResolverTest : public ::testing::Test {
  protected:
    void SetUp() override {
        m_types = std::make_unique<TypeSystem>(m_ctx.diag(), m_ctx.opts());
        m_module.setCurrentModule(kMain);
    }

    std::shared_ptr<FuncDecl> makeFunc(std::string_view name, ModuleIndex module) {
        auto term = Term::Create(TermID::NAME, std::string(name), {}, parser::token_type::NAME);
        auto func = std::make_shared<FuncDecl>(std::move(term));
        func->m_definition = Definition::fromBody({});
        func->module = module;
        return func;
    }

    Symbol symbolOf(std::string_view name, FuncDecl* decl, TypeId signature) {
        Symbol sym;
        sym.name = trust::stableSymbolName(name);
        sym.type = QualType::fromTypeId(signature);
        sym.decl = decl;
        sym.overloads.push_back(SymbolOverload{signature, {}, decl});
        return sym;
    }

    const ModuleIndex kMain = ModuleIndex{1};
    const ModuleIndex kLib = ModuleIndex{2};

    Context m_ctx;
    std::unique_ptr<TypeSystem> m_types;
    ScopeStack m_scopes;
    AliasRegistry m_aliases;
    ModuleSurface m_surface;
    ImportSiteRegistry m_imports;
    ModuleContext m_module;
    SymbolArena m_arena;
    StorageKeyFactory m_keys;
};

TEST_F(NameResolverTest, FlatNameIsResolvedFromScopes) {
    NameResolver resolver(m_scopes, m_aliases, m_surface, m_imports, m_module, m_arena, m_keys);
    VarDecl node("x");
    node.module = kMain;
    Symbol sym;
    sym.name = "x";
    sym.decl = &node;
    const StorageKey flat = exportedKey("", "x");
    m_scopes.current().symbols.emplace(flat, sym);

    ASSERT_EQ(resolver.resolvePlain("x"), &m_scopes.current().symbols.at(flat));
    EXPECT_EQ(resolver.resolvePlain("y"), nullptr);
}

TEST_F(NameResolverTest, LongestAliasPrefixResolvesAndMaterializesOnlyOnBinding) {
    NameResolver resolver(m_scopes, m_aliases, m_surface, m_imports, m_module, m_arena, m_keys);
    const TypeId i32 = m_types->read().getType(type::Int32);
    auto target = makeFunc("f", kLib);
    Symbol targetSym = symbolOf("f", target.get(), m_types->interner().getOrCreateFunctionType(i32, {i32}));
    targetSym.overloads.clear(); // набор перегрузок материализует резолв в точке привязки
    m_surface.recordDeclaration(kLib, ExportRecord{moduleExportKey(kLib, "ns::sub", "f"), &targetSym});
    ASSERT_TRUE(m_aliases.recordAlias(m_scopes.current(), NamespaceAliasRecord{kMain, "a", kLib, "ns::sub", MapperRange{}, AliasOrigin::Local}));

    // const-probe не мутирует найденный символ.
    ASSERT_EQ(resolver.resolvePlain("a::f"), &targetSym);
    EXPECT_TRUE(targetSym.overloads.empty());

    // Точка привязки материализует «символ как значение» (путь объявления у перегрузки).
    EXPECT_EQ(resolver.resolveBinding("a::f"), &targetSym);
    ASSERT_EQ(targetSym.overloads.size(), 1u);
    EXPECT_EQ(targetSym.overloads.front().namespacePath, "ns::sub");
}

TEST_F(NameResolverTest, AmbiguityInBlockScopeMergesCallableSets) {
    NameResolver resolver(m_scopes, m_aliases, m_surface, m_imports, m_module, m_arena, m_keys);
    const TypeId i32 = m_types->read().getType(type::Int32);
    const TypeId i64 = m_types->read().getType(type::Int64);
    auto first = makeFunc("f", kLib);
    auto second = makeFunc("f", ModuleIndex{3});
    const StorageKey flat = exportedKey("", "f");
    // Кандидаты неоднозначности живут в разных ключах одного скоупа (как экспорты разных модулей).
    m_scopes.current().symbols.emplace(flat, symbolOf("f", first.get(), m_types->interner().getOrCreateFunctionType(i32, {i32})));
    m_scopes.current().symbols.emplace(moduleExportKey(ModuleIndex{3}, "", "f"), symbolOf("f", second.get(), m_types->interner().getOrCreateFunctionType(i32, {i64})));

    // Импорт внутри блока: запись неоднозначности — в ТОМ ЖЕ скоупе, а не только на модульном уровне.
    auto block = std::make_shared<ScopeBlock>("{");
    m_scopes.push(block.get());
    Scope& scope = m_scopes.current();
    scope.ambiguous[flat] = AmbiguousBinding{{first.get(), second.get()}, {kLib, ModuleIndex{3}}};

    const Symbol* merged = resolver.resolvePlain("f");
    ASSERT_NE(merged, nullptr);
    EXPECT_EQ(merged->overloads.size(), 2u);
    const std::vector<ModuleIndex>* candidates = resolver.ambiguousModules("f");
    ASSERT_NE(candidates, nullptr);
    EXPECT_EQ(candidates->size(), 2u);
}

TEST_F(NameResolverTest, ExportOfForeignModuleRequiresVisibleSiteOrAlias) {
    NameResolver resolver(m_scopes, m_aliases, m_surface, m_imports, m_module, m_arena, m_keys);
    const TypeId i32 = m_types->read().getType(type::Int32);
    auto target = makeFunc("f", kLib);
    Symbol targetSym = symbolOf("f", target.get(), m_types->interner().getOrCreateFunctionType(i32, {i32}));
    m_surface.recordDeclaration(kLib, ExportRecord{moduleExportKey(kLib, "", "f"), &targetSym});

    // Прямого сайта импорта нет: обычная ссылка не видна, явная привязка авторизует доступ.
    EXPECT_EQ(resolver.findExported(kLib, "f"), nullptr);
    EXPECT_EQ(resolver.findExported(kLib, "f", ResolveOptions{.viaAlias = true}), &targetSym);
}

TEST_F(NameResolverTest, MaskBypassFindsNameHiddenBySiteMask) {
    NameResolver resolver(m_scopes, m_aliases, m_surface, m_imports, m_module, m_arena, m_keys);
    const TypeId i32 = m_types->read().getType(type::Int32);
    auto target = makeFunc("f", kLib);
    Symbol targetSym = symbolOf("f", target.get(), m_types->interner().getOrCreateFunctionType(i32, {i32}));
    m_surface.recordDeclaration(kLib, ExportRecord{moduleExportKey(kLib, "", "f"), &targetSym});

    // Прямой сайт с маской, не покрывающей имя: плоской привязки нет, есть только hidden-ключ.
    ImportSite site;
    site.importer = kMain;
    site.imported = kLib;
    site.masks = utils::ImportMasks::parse("other");
    site.protectedAllowed = true; // невалидный scopeRange — сайт уровня файла (активен)
    m_imports.push(site);
    m_scopes.current().symbols.emplace(moduleKey(kLib, "", "f", /*importHidden=*/true), targetSym);

    EXPECT_EQ(resolver.findExported(kLib, "f"), nullptr) << "маска скрывает имя от обычного резолва";
    EXPECT_NE(resolver.findExported(kLib, "f", ResolveOptions{.maskBypass = true}), nullptr)
        << "maskBypass находит имя, скрытое маской сайта (только для диагностики)";
}

TEST_F(NameResolverTest, ProtectedNameRequiresGroupAccess) {
    const TypeId i32 = m_types->read().getType(type::Int32);
    auto target = makeFunc("h", kLib);
    target->visibility = AstNodeBase::Visibility::Protected;
    Symbol targetSym = symbolOf("h", target.get(), m_types->interner().getOrCreateFunctionType(i32, {i32}));
    m_surface.recordDeclaration(kLib, ExportRecord{moduleExportKey(kLib, "", "h"), &targetSym});

    // protectedAllowed — производная от пары (импортёр, импортируемый) и НЕ входит в идентичность
    // сайта, поэтому разные оси доступа проверяются на отдельных реестрах.
    ImportSite site;
    site.importer = kMain;
    site.imported = kLib;
    site.protectedAllowed = false;
    ImportSiteRegistry deniedSites;
    deniedSites.push(site);
    const NameResolver denied(m_scopes, m_aliases, m_surface, deniedSites, m_module, m_arena, m_keys);
    EXPECT_EQ(denied.findExported(kLib, "h"), nullptr) << "`_::` без доступа группы невидимо на сайте";

    ImportSite groupSite = site;
    groupSite.protectedAllowed = true;
    ImportSiteRegistry groupSites;
    groupSites.push(groupSite);
    const NameResolver allowed(m_scopes, m_aliases, m_surface, groupSites, m_module, m_arena, m_keys);
    EXPECT_NE(allowed.findExported(kLib, "h"), nullptr) << "сайт той же группы/пакета видит `_::`";
}

TEST_F(NameResolverTest, FilePrivateIsInvisibleAtImportSite) {
    NameResolver resolver(m_scopes, m_aliases, m_surface, m_imports, m_module, m_arena, m_keys);
    const TypeId i32 = m_types->read().getType(type::Int32);
    auto target = makeFunc("p", kLib);
    target->visibility = AstNodeBase::Visibility::Private;
    Symbol targetSym = symbolOf("p", target.get(), m_types->interner().getOrCreateFunctionType(i32, {i32}));
    const StorageKey privateKey = moduleKey(kLib, "", "p", /*importHidden=*/false);
    m_surface.recordDeclaration(kLib, ExportRecord{privateKey, &targetSym});
    m_scopes.current().symbols.emplace(privateKey, targetSym);

    ImportSite site;
    site.importer = kMain;
    site.imported = kLib;
    site.protectedAllowed = true;
    m_imports.push(site);
    EXPECT_EQ(resolver.findExported(kLib, "p"), nullptr) << "`__::` не экспортируется: чужой модуль не видит";

    m_module.setCurrentModule(kLib);
    EXPECT_NE(resolver.findExported(kLib, "p"), nullptr) << "своему модулю file-private имя видно";
}

TEST_F(NameResolverTest, SiteIsActiveOnlyInsideItsScope) {
    NameResolver resolver(m_scopes, m_aliases, m_surface, m_imports, m_module, m_arena, m_keys);
    MapperRange range;
    range.begin = MapperLocation::makeLoc(MapperFile::fromRaw(0), 5);
    range.end = MapperLocation::makeLoc(MapperFile::fromRaw(0), 9);
    auto block = std::make_shared<ScopeBlock>("{");
    block->setRange(range);
    ImportSite site;
    site.importer = kMain;
    site.imported = kLib;
    site.scopeRange = range;

    EXPECT_FALSE(resolver.isSiteActive(site)) << "скоуп ещё не открыт";
    m_scopes.push(block.get());
    EXPECT_TRUE(resolver.isSiteActive(site));
    m_scopes.pop();
    EXPECT_FALSE(resolver.isSiteActive(site)) << "вне своего скоупа импорт не действует";
}

} // namespace
} // namespace trust
