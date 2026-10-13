// test/unit/symbols/site_visibility_behavior_test.cpp
// Замок плоскости ПРИВЯЗКИ и РЕЗОЛВА сайта импорта
// (bindImportedModule / findVisibleFromModule / реэкспорт / namespace-алиасы / using).
// Плоскость РЕГИСТРАЦИИ покрыта symbol_visibility_test.cpp.
// Тесты фиксируют наблюдаемое поведение, включая неочевидные правила видимости.
#include "symbols/symbol_table.hpp"
#include "ast/ast_nodes.hpp"
#include "ast/ident_name.hpp"
#include "gtest/gtest.h"

#include <algorithm>
#include <memory>
#include <optional>
#include <string>
#include <string_view>
#include <vector>

namespace trust {
namespace {

class SiteVisibilityBehaviorTest : public ::testing::Test {
  protected:
    static constexpr ModuleIndex kImporter{0};
    static constexpr ModuleIndex kOwner{1};
    static constexpr ModuleIndex kFacade{2};
    static constexpr ModuleIndex kOther{3};

    SymbolTable m_table;
    std::shared_ptr<ModuleNode> m_moduleRoot = std::make_shared<ModuleNode>(kImporter, std::string("main"));
    std::vector<std::shared_ptr<IdentName>> m_decls;

    /// Объявление-заглушка в модуле `owner` (идентичность объявления — по узлу).
    Symbol makeSymbol(std::string_view name, ModuleIndex owner,
                      AstNodeBase::Visibility visibility = AstNodeBase::Visibility::Public) {
        auto node = std::make_shared<IdentName>(std::string(name));
        node->module = owner;
        node->visibility = visibility;
        m_decls.push_back(node);
        Symbol sym;
        sym.name = stableSymbolName(node->text());
        sym.decl = node.get();
        return sym;
    }

    void enterFileScope(ModuleIndex file) {
        m_table.push(m_moduleRoot.get());
        m_table.setCurrentModule(file);
    }

    /// Тело модуля `owner`, импортированного сайтом с масками `masks`:
    /// регистрация объявлений идёт под ключами этого сайта (`includeProtected` — сайт видит `_::`).
    Symbol declareInBody(std::string_view name, ModuleIndex owner, const utils::ImportMasks& masks,
                         AstNodeBase::Visibility visibility = AstNodeBase::Visibility::Public, bool includeProtected = false) {
        const ModuleIndex importer = m_table.currentModule();
        m_table.pushImportScope(owner, masks, includeProtected);
        m_table.setCurrentModule(owner);
        Symbol sym = makeSymbol(name, owner, visibility);
        const DeclResult result = m_table.declare(sym);
        EXPECT_TRUE(result == DeclResult::Inserted || result == DeclResult::AlreadyRegistered) << "declare failed";
        m_table.popImportScope();
        m_table.setCurrentModule(importer);
        return sym;
    }

    const AstNodeBase* resolvedDecl(std::string_view name) {
        const Symbol* sym = m_table.resolve(name);
        return sym != nullptr ? sym->decl : nullptr;
    }
};

// -- Плоскость привязки: bindImportedModule ------------------------------------------------

TEST_F(SiteVisibilityBehaviorTest, DirectSiteBindsFlatName) {
    enterFileScope(kImporter);
    const Symbol f = declareInBody("f", kOwner, utils::ImportMasks{});
    m_table.bindImportedModule(kOwner, utils::ImportMasks{}, /*includeProtected=*/false);
    EXPECT_EQ(resolvedDecl("f"), f.decl) << "прямой сайт привязывает плоское имя импортёру";
}

TEST_F(SiteVisibilityBehaviorTest, MaskedNameIsNotBoundToImporter) {
    enterFileScope(kImporter);
    (void)declareInBody("f", kOwner, utils::ImportMasks::parse("g"));
    const Symbol g = declareInBody("g", kOwner, utils::ImportMasks::parse("g"));
    m_table.bindImportedModule(kOwner, utils::ImportMasks::parse("g"), /*includeProtected=*/false);
    EXPECT_EQ(resolvedDecl("f"), nullptr) << "имя вне маски не привязывается к импортёру";
    EXPECT_EQ(resolvedDecl("g"), g.decl);
}

TEST_F(SiteVisibilityBehaviorTest, ProtectedNameDeniedAtSecondSite) {
    enterFileScope(kImporter);
    // Тело проанализировано сайтом, чья маска скрыла имя: плоского ключа нет, поэтому
    // решение принимает bindImportedModule (второй сайт).
    (void)declareInBody("_::h", kOwner, utils::ImportMasks::parse("other"), AstNodeBase::Visibility::Protected);
    m_table.bindImportedModule(kOwner, utils::ImportMasks{}, /*includeProtected=*/false);
    EXPECT_EQ(resolvedDecl("_::h"), nullptr) << "protected-имя без доступа группы не привязывается";
}

TEST_F(SiteVisibilityBehaviorTest, ProtectedNameHiddenWithoutGroupAccess) {
    // Сайт, первым запустивший анализ тела модуля, тоже НЕ привязывает protected-имя без доступа
    // группы: плоский ключ не создаётся ни на одном сайте.
    enterFileScope(kImporter);
    (void)declareInBody("_::h", kOwner, utils::ImportMasks{}, AstNodeBase::Visibility::Protected);
    m_table.bindImportedModule(kOwner, utils::ImportMasks{}, /*includeProtected=*/false);
    EXPECT_EQ(resolvedDecl("_::h"), nullptr) << "protected-имя без доступа группы не видно ни на одном сайте";
}

TEST_F(SiteVisibilityBehaviorTest, ProtectedNameVisibleWithGroupAccess) {
    enterFileScope(kImporter);
    const Symbol h = declareInBody("_::h", kOwner, utils::ImportMasks{}, AstNodeBase::Visibility::Protected,
                                  /*includeProtected=*/true);
    m_table.bindImportedModule(kOwner, utils::ImportMasks{}, /*includeProtected=*/true);
    EXPECT_EQ(resolvedDecl("_::h"), h.decl) << "protected-имя видно импортёру той же группы/пакета";
}

TEST_F(SiteVisibilityBehaviorTest, ProtectedQualifiedReferenceRequiresGroupAccess) {
    enterFileScope(kImporter);
    (void)declareInBody("_::h", kOwner, utils::ImportMasks{}, AstNodeBase::Visibility::Protected);
    m_table.bindImportedModule(kOwner, utils::ImportMasks{}, /*includeProtected=*/false);
    EXPECT_EQ(m_table.findVisibleFromModule(kOwner, "_::h"), nullptr)
        << "квалифицированная ссылка не обходит защиту protected";
}

TEST_F(SiteVisibilityBehaviorTest, PrivateNamesAreNeverBound) {
    enterFileScope(kImporter);
    (void)declareInBody("secret", kOwner, utils::ImportMasks{}, AstNodeBase::Visibility::Private);
    m_table.bindImportedModule(kOwner, utils::ImportMasks{}, /*includeProtected=*/true);
    EXPECT_EQ(resolvedDecl("secret"), nullptr) << "private-имя чужого модуля не видно никогда";
}

// -- Плоскость резолва: findVisibleFromModule ---------------------------------------------

TEST_F(SiteVisibilityBehaviorTest, QualifiedReferenceRequiresExportAndSite) {
    enterFileScope(kImporter);
    (void)declareInBody("f", kOwner, utils::ImportMasks{});
    m_table.bindImportedModule(kOwner, utils::ImportMasks{}, /*includeProtected=*/false);
    EXPECT_NE(m_table.findVisibleFromModule(kOwner, "f"), nullptr);
    EXPECT_EQ(m_table.findVisibleFromModule(kOwner, "missing"), nullptr) << "не-экспорт модуля не виден";
    EXPECT_EQ(m_table.findVisibleFromModule(kOther, "f"), nullptr) << "модуль без активного сайта не виден";
}

TEST_F(SiteVisibilityBehaviorTest, MaskBypassFindsNameHiddenBySiteMask) {
    enterFileScope(kImporter);
    (void)declareInBody("f", kOwner, utils::ImportMasks::parse("g"));
    (void)declareInBody("g", kOwner, utils::ImportMasks::parse("g"));
    m_table.bindImportedModule(kOwner, utils::ImportMasks::parse("g"), /*includeProtected=*/false);
    EXPECT_EQ(m_table.findVisibleFromModule(kOwner, "f"), nullptr) << "резолв по скрытому имени запрещён";
    EXPECT_NE(m_table.findVisibleFromModule(kOwner, "f", /*maskBypass=*/true), nullptr)
        << "maskBypass — режим диагностики: имя находится, но резолвом не является";
}

TEST_F(SiteVisibilityBehaviorTest, SameNameFromTwoModulesIsAmbiguous) {
    enterFileScope(kImporter);
    (void)declareInBody("f", kOwner, utils::ImportMasks{});
    m_table.bindImportedModule(kOwner, utils::ImportMasks{}, /*includeProtected=*/false);
    (void)declareInBody("f", kOther, utils::ImportMasks{});
    m_table.bindImportedModule(kOther, utils::ImportMasks{}, /*includeProtected=*/false);

    EXPECT_EQ(resolvedDecl("f"), nullptr) << "не-callable одноимённые объявления не резолвятся";
    const std::vector<ModuleIndex>* modules = m_table.ambiguousModules("f");
    ASSERT_NE(modules, nullptr) << "конфликт плоского имени записан как неоднозначность";
    EXPECT_NE(std::find(modules->begin(), modules->end(), kOwner), modules->end());
    EXPECT_NE(std::find(modules->begin(), modules->end(), kOther), modules->end());
}

TEST_F(SiteVisibilityBehaviorTest, FacadeRepeatExportsVisibleNames) {
    enterFileScope(kFacade);
    const Symbol f = declareInBody("f", kOwner, utils::ImportMasks{});
    (void)declareInBody("g", kOwner, utils::ImportMasks{});
    m_table.surface().recordReExport(m_table.currentModule(), kOwner, {f.decl});

    m_table.setCurrentModule(kImporter);
    m_table.bindImportedModule(kFacade, utils::ImportMasks{}, /*includeProtected=*/false);
    EXPECT_EQ(resolvedDecl("f"), f.decl) << "клиент фасада видит реэкспортированное имя";
    EXPECT_EQ(m_table.findVisibleFromModule(kFacade, "g"), nullptr) << "имя вне набора реэкспорта не публикуется";
    EXPECT_NE(m_table.findVisibleFromModule(kFacade, "f"), nullptr) << "квалифицированная ссылка через фасад разрешена";
}

// -- Namespace-алиасы и using ------------------------------------------------------------

TEST_F(SiteVisibilityBehaviorTest, NamespaceAliasResolvesQualifiedName) {
    enterFileScope(kImporter);
    const Symbol f = declareInBody("inner::f", kOwner, utils::ImportMasks{});
    m_table.bindImportedModule(kOwner, utils::ImportMasks{}, /*includeProtected=*/false);

    const NamespaceAliasRecord alias{kImporter, "ns", kOwner, "inner", MapperRange{}, AliasOrigin::Local};
    ASSERT_TRUE(m_table.aliases().recordAlias(m_table.current(), alias));
    EXPECT_EQ(resolvedDecl("ns::f"), f.decl) << "алиас — привязка префикса: имя резолвится через владельца";
}

TEST_F(SiteVisibilityBehaviorTest, UsingScopeResolvesBareNameAndNormalizesText) {
    enterFileScope(kImporter);
    const Symbol f = declareInBody("inner::f", kImporter, utils::ImportMasks{});
    m_table.aliases().recordUsing(m_table.current(), UsingScopeRecord{kImporter, "inner", std::nullopt, MapperRange{}, MapperRange{}});

    const UsingResolveResult resolved = m_table.names().resolveUsing("f");
    ASSERT_NE(resolved.symbol, nullptr);
    EXPECT_EQ(resolved.symbol->decl, f.decl);
    EXPECT_EQ(resolved.qualifiedText, "inner::f") << "текст ссылки НОРМАЛИЗУЕТСЯ на квалифицированное имя";
    EXPECT_FALSE(resolved.ambiguous);
}

TEST_F(SiteVisibilityBehaviorTest, UsingScopeWithModuleTargetUsesSiteVisibility) {
    enterFileScope(kImporter);
    const Symbol f = declareInBody("inner::f", kOwner, utils::ImportMasks{});
    m_table.bindImportedModule(kOwner, utils::ImportMasks{}, /*includeProtected=*/false);
    m_table.aliases().recordUsing(m_table.current(), UsingScopeRecord{kImporter, "inner", kOwner, MapperRange{}, MapperRange{}});

    const UsingResolveResult resolved = m_table.names().resolveUsing("f");
    ASSERT_NE(resolved.symbol, nullptr) << "`... = \\owner::inner` ищет в экспорт-наборе указанного модуля";
    EXPECT_EQ(resolved.symbol->decl, f.decl);
    EXPECT_EQ(resolved.qualifiedText, "inner::f") << "кодоген печатает путь ВЫБРАННОЙ перегрузки: текст квалифицирован";
    EXPECT_FALSE(resolved.ambiguous);
}

} // namespace
} // namespace trust
