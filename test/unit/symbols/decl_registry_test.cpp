#include "symbols/decl/decl_registry.hpp"

#include "ast/ast_nodes.hpp"
#include "ast/token.hpp"
#include "ast/token_type.hpp"
#include "gtest/gtest.h"
#include "session/context.hpp"
#include "syntax/term/term.h"
#include "syntax/term/term_types.h"
#include "types/type_system.hpp"
#include "types/names.hpp"

#include <memory>
#include <span>
#include <vector>

namespace trust {
namespace {

// -- DeclarationRegistry: классификация и per-key регистрация -------------------------------

TEST(DeclRegistry, ForwardDeclarationClassification) {
    VarDecl forward("x");
    EXPECT_TRUE(isForwardDeclaration(forward));
    VarDecl defined("x");
    defined.m_initializer = std::make_shared<VarDecl>("y");
    EXPECT_FALSE(isForwardDeclaration(defined));

    auto funcTerm = Term::Create(TermID::NAME, "f", {}, parser::token_type::NAME);
    FuncDecl funcForward(std::move(funcTerm));
    EXPECT_TRUE(isForwardDeclaration(funcForward));
    funcForward.m_definition = Definition::fromBody({});
    EXPECT_FALSE(isForwardDeclaration(funcForward));
}

TEST(DeclRegistry, ForeignDeclarationIsModuleRelative) {
    VarDecl node("x");
    Symbol sym;
    sym.decl = &node;
    node.module = ModuleIndex{2};
    EXPECT_TRUE(isForeignDeclaration(sym, ModuleIndex{1}));
    EXPECT_FALSE(isForeignDeclaration(sym, ModuleIndex{2}));
    node.module = ModuleIndex::invalid();
    EXPECT_FALSE(isForeignDeclaration(sym, ModuleIndex{1}));
}

class DeclRegistryTest : public ::testing::Test {
  protected:
    void SetUp() override { m_types = std::make_unique<TypeSystem>(m_ctx.diag(), m_ctx.opts()); }

    std::shared_ptr<FuncDecl> makeFunc(std::string_view name, bool definition) {
        auto term = Term::Create(TermID::NAME, std::string(name), {}, parser::token_type::NAME);
        auto f = std::make_shared<FuncDecl>(std::move(term));
        if (definition) {
            f->m_definition = Definition::fromBody({});
        }
        return f;
    }

    Symbol symbol(std::string_view name, TypeId signature, FuncDecl* decl) {
        Symbol s;
        s.name = trust::stableSymbolName(name);
        s.type = QualType::fromTypeId(signature);
        s.decl = decl;
        s.overloads.push_back(SymbolOverload{signature, {}, decl});
        return s;
    }

    Context m_ctx;
    std::unique_ptr<TypeSystem> m_types;
    Scope m_scope;
};

TEST_F(DeclRegistryTest, InsertThenIdempotentReRegistration) {
    const TypeId i32 = m_types->read().getType(type::Int32);
    const TypeId signature = m_types->interner().getOrCreateFunctionType(i32, {i32});
    auto func = makeFunc("f", false);
    const StorageKey key = exportedKey("", "f");

    EXPECT_EQ(declareOrCompleteInScope(m_scope, symbol("f", signature, func.get()), key, m_types.get()), DeclResult::Inserted);
    EXPECT_EQ(m_scope.symbols.size(), 1u);
    // Тот же узел — идемпотентно.
    EXPECT_EQ(declareOrCompleteInScope(m_scope, symbol("f", signature, func.get()), key, m_types.get()), DeclResult::AlreadyRegistered);
}

TEST_F(DeclRegistryTest, ForwardThenDefinitionCompletes) {
    const TypeId i32 = m_types->read().getType(type::Int32);
    const TypeId signature = m_types->interner().getOrCreateFunctionType(i32, {i32});
    auto forward = makeFunc("f", false);
    auto definition = makeFunc("f", true);
    const StorageKey key = exportedKey("", "f");

    EXPECT_EQ(declareOrCompleteInScope(m_scope, symbol("f", signature, forward.get()), key, m_types.get()), DeclResult::Inserted);
    EXPECT_EQ(declareOrCompleteInScope(m_scope, symbol("f", signature, definition.get()), key, m_types.get()), DeclResult::Completed);
    ASSERT_EQ(m_scope.symbols.at(key).overloads.size(), 1u);
    EXPECT_EQ(m_scope.symbols.at(key).overloads.front().decl, definition.get());
}

TEST_F(DeclRegistryTest, DifferentSignatureBecomesOverload) {
    const TypeId i32 = m_types->read().getType(type::Int32);
    const TypeId i64 = m_types->read().getType(type::Int64);
    auto f32 = makeFunc("f", true);
    auto f64 = makeFunc("f", true);
    const StorageKey key = exportedKey("", "f");

    EXPECT_EQ(declareOrCompleteInScope(m_scope, symbol("f", m_types->interner().getOrCreateFunctionType(i32, {i32}), f32.get()), key, m_types.get()),
              DeclResult::Inserted);
    EXPECT_EQ(declareOrCompleteInScope(m_scope, symbol("f", m_types->interner().getOrCreateFunctionType(i32, {i64}), f64.get()), key, m_types.get()),
              DeclResult::Overloaded);
    EXPECT_EQ(m_scope.symbols.at(key).overloads.size(), 2u);
}

TEST_F(DeclRegistryTest, TwoDefinitionsOfSameSignatureAreDuplicate) {
    const TypeId i32 = m_types->read().getType(type::Int32);
    const TypeId signature = m_types->interner().getOrCreateFunctionType(i32, {i32});
    auto first = makeFunc("f", true);
    auto second = makeFunc("f", true);
    const StorageKey key = exportedKey("", "f");

    EXPECT_EQ(declareOrCompleteInScope(m_scope, symbol("f", signature, first.get()), key, m_types.get()), DeclResult::Inserted);
    EXPECT_EQ(declareOrCompleteInScope(m_scope, symbol("f", signature, second.get()), key, m_types.get()), DeclResult::Duplicate);
}

TEST_F(DeclRegistryTest, AmbiguityRecordsModulesAndDecls) {
    VarDecl a("x");
    VarDecl b("x");
    a.module = ModuleIndex{1};
    b.module = ModuleIndex{2};
    Symbol first;
    first.decl = &a;
    Symbol second;
    second.decl = &b;
    const StorageKey flat = exportedKey("", "x");

    recordAmbiguousBinding(m_scope, flat, first, second, ModuleIndex{3});
    ASSERT_EQ(m_scope.ambiguous.size(), 1u);
    const AmbiguousBinding& conflict = m_scope.ambiguous.at(flat);
    ASSERT_EQ(conflict.modules.size(), 3u);
    ASSERT_EQ(conflict.decls.size(), 2u);
}

// -- DeclarationRegistry: политика × сценарий ------------------------------------------------

/// Регистрация через единый примитив `store`: вызывающие задают только политику.
class StorePolicyTest : public ::testing::Test {
  protected:
    void SetUp() override {
        m_types = std::make_unique<TypeSystem>(m_ctx.diag(), m_ctx.opts());
        m_module.setCurrentModule(ModuleIndex{1});
    }

    static std::span<const StorageKey> one(const StorageKey& key) { return std::span<const StorageKey>(&key, 1); }

    static Symbol symbolOf(const char* name, AstNodeBase* decl) {
        Symbol sym;
        sym.name = trust::stableSymbolName(name);
        sym.decl = decl;
        return sym;
    }

    Context m_ctx;
    std::unique_ptr<TypeSystem> m_types;
    ScopeStack m_scopes;
    ModuleSurface m_surface;
    SymbolArena m_arena;
    ModuleContext m_module;
    ImportSiteRegistry m_imports;
    StorageKeyFactory m_keys;
};

TEST_F(StorePolicyTest, InsertOnlyRegistersOnceAndRejectsAnotherNode) {
    DeclarationRegistry registry(m_scopes, m_surface, m_arena, m_module, m_imports, m_keys, m_types.get());
    VarDecl firstNode("x");
    VarDecl secondNode("x");
    const Symbol first = symbolOf("x", &firstNode);
    const Symbol second = symbolOf("x", &secondNode);
    const StorageKey key = exportedKey("", "x");
    const StorePolicy insertOnly{.completeForward = false};

    EXPECT_EQ(registry.store(m_scopes.current(), first, one(key), insertOnly), DeclResult::Inserted);
    EXPECT_EQ(registry.store(m_scopes.current(), first, one(key), insertOnly), DeclResult::AlreadyRegistered);
    EXPECT_EQ(registry.store(m_scopes.current(), second, one(key), insertOnly), DeclResult::Duplicate);
    EXPECT_EQ(m_scopes.current().symbols.size(), 1u);
}

TEST_F(StorePolicyTest, ForwardPolicyCompletesAndPublishesSurfaceOnce) {
    DeclarationRegistry registry(m_scopes, m_surface, m_arena, m_module, m_imports, m_keys, m_types.get());
    VarDecl forwardNode("x"); // без инициализатора — forward
    VarDecl definedNode("x");
    definedNode.m_initializer = std::make_shared<VarDecl>("y");
    const StorageKey key = exportedKey("", "x");
    const StorePolicy policy{.surface = SurfacePolicy::DeclarationKey};

    EXPECT_EQ(registry.store(m_scopes.current(), symbolOf("x", &forwardNode), one(key), policy), DeclResult::Inserted);
    EXPECT_EQ(registry.store(m_scopes.current(), symbolOf("x", &definedNode), one(key), policy), DeclResult::Completed);
    EXPECT_EQ(m_scopes.current().symbols.at(key).decl, &definedNode);
    ASSERT_TRUE(m_surface.hasModule(ModuleIndex{1}));
    EXPECT_EQ(m_surface.declarationsOf(ModuleIndex{1}).size(), 1u);
}

TEST_F(StorePolicyTest, ForeignDeclarationUnderFlatKeyBecomesAmbiguous) {
    DeclarationRegistry registry(m_scopes, m_surface, m_arena, m_module, m_imports, m_keys, m_types.get());
    VarDecl foreignNode("x");
    foreignNode.module = ModuleIndex{2};
    VarDecl localNode("x");
    localNode.module = ModuleIndex{1};
    const StorageKey flat = exportedKey("", "x");
    const StorePolicy insertOnly{.completeForward = false};

    EXPECT_EQ(registry.store(m_scopes.current(), symbolOf("x", &foreignNode), one(flat), insertOnly), DeclResult::Inserted);
    // Плоская привязка занята чужим модулем: не дубль, а неоднозначность имени.
    EXPECT_EQ(registry.store(m_scopes.current(), symbolOf("x", &localNode), one(flat), insertOnly), DeclResult::AlreadyRegistered);
    EXPECT_EQ(m_scopes.current().ambiguous.size(), 1u);
}

TEST_F(StorePolicyTest, ModuleRootRejectsConflictWithOwnVisibilityKey) {
    DeclarationRegistry registry(m_scopes, m_surface, m_arena, m_module, m_imports, m_keys, m_types.get());
    VarDecl privateNode("x");
    privateNode.module = ModuleIndex{1};
    VarDecl exportedNode("x");
    exportedNode.module = ModuleIndex{1};
    const StorageKey privateKey = m_keys.privateKey(ModuleIndex{1}, "x");
    const StorageKey flat = exportedKey("", "x");

    EXPECT_EQ(registry.store(m_scopes.current(), symbolOf("x", &privateNode), one(privateKey), StorePolicy{.completeForward = false}),
              DeclResult::Inserted);
    // Тот же spelling уже объявлен под private-ключом своего модуля — конфликт видимости.
    EXPECT_EQ(registry.store(m_scopes.current(), symbolOf("x", &exportedNode), one(flat), StorePolicy{.completeForward = false, .moduleRoot = true}),
              DeclResult::Duplicate);
}

TEST_F(StorePolicyTest, SameModuleReexportIsNotAmbiguity) {
    DeclarationRegistry registry(m_scopes, m_surface, m_arena, m_module, m_imports, m_keys, m_types.get());
    VarDecl facadeNode("x");
    facadeNode.module = ModuleIndex{2};
    VarDecl ownerNode("x");
    ownerNode.module = ModuleIndex{2};
    const StorageKey flat = exportedKey("", "x");
    const StorePolicy sitePolicy{.completeForward = false, .skipSameModuleReexport = true};

    EXPECT_EQ(registry.store(m_scopes.current(), symbolOf("x", &facadeNode), one(flat), sitePolicy), DeclResult::Inserted);
    // Тот же экспорт (модуль владельца совпал, узел другой) — привязка сайта, не неоднозначность.
    EXPECT_EQ(registry.store(m_scopes.current(), symbolOf("x", &ownerNode), one(flat), sitePolicy), DeclResult::AlreadyRegistered);
    EXPECT_TRUE(m_scopes.current().ambiguous.empty());
    EXPECT_EQ(m_scopes.current().symbols.at(flat).decl, &facadeNode);
}

TEST_F(StorePolicyTest, RootDeclarationPublishesRecognizedDeclaration) {
    DeclarationRegistry registry(m_scopes, m_surface, m_arena, m_module, m_imports, m_keys, m_types.get());
    VarDecl node("x");
    node.module = ModuleIndex{1};
    const Symbol sym = symbolOf("x", &node);
    const StorageKey key = m_keys.identityKey(ModuleIndex{1}, "x");
    const StorePolicy rootPolicy{.surface = SurfacePolicy::RootDeclaration, .completeForward = false};

    EXPECT_EQ(registry.store(m_scopes.current(), sym, one(key), rootPolicy), DeclResult::Inserted);
    // Повторная регистрация того же узла: поверхность обязана существовать и не дублироваться.
    EXPECT_EQ(registry.store(m_scopes.current(), sym, one(key), rootPolicy), DeclResult::AlreadyRegistered);
    EXPECT_EQ(m_surface.declarationsOf(ModuleIndex{1}).size(), 1u);
}

TEST_F(StorePolicyTest, AliasKeepsTargetOverloadPathAndPublishesSurface) {
    DeclarationRegistry registry(m_scopes, m_surface, m_arena, m_module, m_imports, m_keys, m_types.get());
    const TypeId i32 = m_types->read().getType(type::Int32);
    VarDecl targetNode("f");
    targetNode.module = ModuleIndex{2};
    Symbol target;
    target.name = trust::stableSymbolName("f");
    target.type = QualType::fromTypeId(i32);
    target.decl = &targetNode;
    target.overloads.push_back(SymbolOverload{i32, {}, &targetNode, "ns"});

    // Имя-алиас: тип/перегрузки/decl — от цели, видимое имя — алиас.
    Symbol alias = target;
    alias.name = trust::stableSymbolName("g");
    const StorageKey aliasKey = exportedKey("", "g");
    const StorePolicy policy{.surface = SurfacePolicy::PositionDerived, .aliasOfThisFile = true, .completeForward = false};

    EXPECT_EQ(registry.store(m_scopes.current(), alias, one(aliasKey), policy), DeclResult::Inserted);
    const Symbol& stored = m_scopes.current().symbols.at(aliasKey);
    EXPECT_EQ(stored.name, "g");
    EXPECT_EQ(stored.decl, &targetNode);
    // Путь объявления ЦЕЛИ не перезаписывается путём ключа алиаса: кодоген печатает C++-имя цели.
    ASSERT_EQ(stored.overloads.size(), 1u);
    EXPECT_EQ(stored.overloads.front().namespacePath, "ns");
    EXPECT_TRUE(m_surface.hasModule(ModuleIndex{1}));
}

TEST_F(StorePolicyTest, AliasOfOccupiedNameIsDuplicate) {
    DeclarationRegistry registry(m_scopes, m_surface, m_arena, m_module, m_imports, m_keys, m_types.get());
    VarDecl existingNode("g");
    VarDecl targetNode("f");
    const StorageKey key = exportedKey("", "g");
    EXPECT_EQ(registry.store(m_scopes.current(), symbolOf("g", &existingNode), one(key), StorePolicy{.completeForward = false}),
              DeclResult::Inserted);

    Symbol alias = symbolOf("g", &targetNode);
    const StorePolicy policy{.surface = SurfacePolicy::PositionDerived, .aliasOfThisFile = true, .completeForward = false};
    EXPECT_EQ(registry.store(m_scopes.current(), alias, one(key), policy), DeclResult::Duplicate);
    EXPECT_EQ(m_scopes.current().symbols.at(key).decl, &existingNode);
}

TEST_F(StorePolicyTest, GlobalStoreIsInsertOrCompleteAndKeepsFirstDeclaration) {
    DeclarationRegistry registry(m_scopes, m_surface, m_arena, m_module, m_imports, m_keys, m_types.get());
    VarDecl forwardNode("gv");
    VarDecl definition("gv");
    definition.m_initializer = std::make_shared<VarDecl>("y");

    registry.storeGlobalIfAbsent(symbolOf("gv", &forwardNode));
    // Повторная регистрация другим узлом имя не перезаписывает.
    registry.storeGlobalIfAbsent(symbolOf("gv", &definition));
    ASSERT_EQ(m_scopes.front().symbols.size(), 1u);
    const StorageKey globalKey = m_keys.localKey("gv");
    EXPECT_EQ(m_scopes.front().symbols.at(globalKey).decl, &forwardNode);

    // С завершением forward: определение заменяет представителя.
    VarDecl forwardNode2("gv2");
    VarDecl definition2("gv2");
    definition2.m_initializer = std::make_shared<VarDecl>("y");
    EXPECT_EQ(registry.storeGlobal(symbolOf("gv2", &forwardNode2)), DeclResult::Inserted);
    EXPECT_EQ(registry.storeGlobal(symbolOf("gv2", &definition2)), DeclResult::Completed);
    EXPECT_EQ(m_scopes.front().symbols.at(m_keys.localKey("gv2")).decl, &definition2);
}

TEST_F(StorePolicyTest, CompleteTypeUpdatesEntriesWithSameTypeId) {
    DeclarationRegistry registry(m_scopes, m_surface, m_arena, m_module, m_imports, m_keys, m_types.get());
    const TypeId i32 = m_types->read().getType(type::Int32);
    VarDecl forwardNode("T");
    Symbol forwardSym;
    forwardSym.name = trust::stableSymbolName("T");
    forwardSym.type = QualType::fromTypeId(i32);
    forwardSym.decl = &forwardNode;
    const StorageKey key = exportedKey("", "T");
    EXPECT_EQ(registry.store(m_scopes.current(), forwardSym, one(key), StorePolicy{.completeForward = false}), DeclResult::Inserted);

    VarDecl definition("T");
    Symbol definitionSym = forwardSym;
    definitionSym.decl = &definition;
    registry.completeType(definitionSym, key);
    EXPECT_EQ(m_scopes.current().symbols.at(key).decl, &definition);
}

} // namespace
} // namespace trust
