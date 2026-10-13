// test/unit/symbols/symbol_visibility_test.cpp
// Видимость импортированных имён (маски/транзитивность): объявления регистрируются
// полностью, но поиск фильтруется file-local ключом (private/hidden/flat).
#include "symbols/symbol_table.hpp"
#include "ast/ast_nodes.hpp"
#include "ast/ident_name.hpp"
#include "gtest/gtest.h"

#include <memory>
#include <string>
#include <vector>

namespace trust {
namespace {

class SymbolVisibilityTest : public ::testing::Test {
  protected:
    SymbolTable m_table;
    std::shared_ptr<ModuleNode> m_moduleRoot = std::make_shared<ModuleNode>(ModuleIndex{0}, std::string("main"));
    std::vector<std::shared_ptr<IdentName>> m_decls;

    /// Объявление-заглушка: уникальный узел (duplicate-проверка сравнивает decl).
    Symbol makeSymbol(std::string_view name) {
        auto node = std::make_shared<IdentName>(std::string(name));
        m_decls.push_back(node);
        Symbol sym;
        sym.name = node->text();
        sym.decl = node.get();
        return sym;
    }

    void enterModuleScope() { m_table.push(m_moduleRoot.get()); }
    const AstNodeBase* resolvedDecl(std::string_view name) {
        const Symbol* sym = m_table.resolve(name);
        return sym != nullptr ? sym->decl : nullptr;
    }
};

TEST_F(SymbolVisibilityTest, DirectImportVisibleToImporterAndModule) {
    enterModuleScope();
    m_table.setCurrentModule(ModuleIndex{0});
    m_table.pushImportScope(ModuleIndex{1}, utils::ImportMasks{});
    m_table.setCurrentModule(ModuleIndex{1});
    const Symbol f = makeSymbol("f");
    ASSERT_EQ(m_table.declare(f), DeclResult::Inserted);
    m_table.popImportScope();
    m_table.setCurrentModule(ModuleIndex{0});
    EXPECT_EQ(resolvedDecl("f"), f.decl) << "direct import must be visible in the importer";
    m_table.setCurrentModule(ModuleIndex{1});
    EXPECT_EQ(resolvedDecl("f"), f.decl) << "module body must see its own export";
}

TEST_F(SymbolVisibilityTest, MaskedNameHiddenFromImporterButVisibleToModule) {
    enterModuleScope();
    m_table.setCurrentModule(ModuleIndex{0});
    m_table.pushImportScope(ModuleIndex{1}, utils::ImportMasks::parse("f"));
    m_table.setCurrentModule(ModuleIndex{1});
    const Symbol f = makeSymbol("f");
    const Symbol g = makeSymbol("g");
    ASSERT_EQ(m_table.declare(f), DeclResult::Inserted);
    ASSERT_EQ(m_table.declare(g), DeclResult::Inserted);
    EXPECT_EQ(resolvedDecl("g"), g.decl) << "masked name must stay visible inside the module body";
    m_table.popImportScope();
    m_table.setCurrentModule(ModuleIndex{0});
    EXPECT_EQ(resolvedDecl("f"), f.decl);
    EXPECT_EQ(resolvedDecl("g"), nullptr) << "masked name must not leak into the importer";
    m_table.setCurrentModule(ModuleIndex{1});
    EXPECT_EQ(resolvedDecl("g"), g.decl) << "file-local hidden key must be found in module context";
}

TEST_F(SymbolVisibilityTest, TransitiveImportHiddenFromRootImporter) {
    enterModuleScope();
    m_table.setCurrentModule(ModuleIndex{0});
    m_table.pushImportScope(ModuleIndex{1}, utils::ImportMasks{});
    m_table.setCurrentModule(ModuleIndex{1});
    const Symbol a = makeSymbol("a");
    ASSERT_EQ(m_table.declare(a), DeclResult::Inserted);
    m_table.pushImportScope(ModuleIndex{2}, utils::ImportMasks{});
    m_table.setCurrentModule(ModuleIndex{2});
    const Symbol b = makeSymbol("b");
    ASSERT_EQ(m_table.declare(b), DeclResult::Inserted);
    m_table.setCurrentModule(ModuleIndex{1});
    EXPECT_EQ(resolvedDecl("b"), b.decl) << "direct importer of module 2 must see its names";
    m_table.popImportScope();
    m_table.setCurrentModule(ModuleIndex{0});
    EXPECT_EQ(resolvedDecl("b"), nullptr) << "transitive import must not leak into the root importer";
    EXPECT_EQ(resolvedDecl("a"), a.decl);
}

TEST_F(SymbolVisibilityTest, MaskedImportDoesNotConflictWithImporterDeclaration) {
    enterModuleScope();
    m_table.setCurrentModule(ModuleIndex{0});
    const Symbol own = makeSymbol("g");
    ASSERT_EQ(m_table.declare(own), DeclResult::Inserted);
    m_table.pushImportScope(ModuleIndex{1}, utils::ImportMasks::parse("f"));
    m_table.setCurrentModule(ModuleIndex{1});
    ASSERT_EQ(m_table.declare(makeSymbol("f")), DeclResult::Inserted);
    ASSERT_EQ(m_table.declare(makeSymbol("g")), DeclResult::Inserted);
    m_table.popImportScope();
    m_table.setCurrentModule(ModuleIndex{0});
    EXPECT_EQ(resolvedDecl("g"), own.decl) << "masked import must not conflict with the importer's own name";
}

TEST_F(SymbolVisibilityTest, SecondImportSiteIsDuplicateRegistration) {
    enterModuleScope();
    m_table.setCurrentModule(ModuleIndex{0});
    const Symbol f = makeSymbol("f");
    m_table.pushImportScope(ModuleIndex{1}, utils::ImportMasks::parse("f"));
    m_table.setCurrentModule(ModuleIndex{1});
    ASSERT_EQ(m_table.declare(f), DeclResult::Inserted);
    m_table.popImportScope();
    m_table.setCurrentModule(ModuleIndex{0});
    // Повторный сайт импорта того же модуля регистрирует ТО ЖЕ объявление (тот же узел AST):
    // идемпотентно, не дубль (одна AST на модуль ⇒ тот же модуль).
    m_table.pushImportScope(ModuleIndex{1}, utils::ImportMasks::parse("f"));
    m_table.setCurrentModule(ModuleIndex{1});
    EXPECT_EQ(m_table.declare(f), DeclResult::AlreadyRegistered) << "re-import of the same declaration must be idempotent";
}

} // namespace
} // namespace trust
