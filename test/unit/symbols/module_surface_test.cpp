#include "symbols/decl/module_surface.hpp"

#include "ast/ast_nodes.hpp"
#include "gtest/gtest.h"

#include <memory>
#include <stdexcept>
#include <vector>

namespace trust {
namespace {

// -- ModuleSurface: единственное хранилище экспорт-поверхности модуля -----------------------

TEST(ModuleSurface, DeclarationIsKeyedByIdentity) {
    ModuleSurface surface;
    Symbol symbol;
    symbol.name = "f";
    symbol.decl = nullptr;
    const ExportRecord record{moduleExportKey(ModuleIndex{1}, "", "f"), &symbol};
    surface.recordDeclaration(ModuleIndex{1}, record);
    surface.recordDeclaration(ModuleIndex{1}, record);

    ASSERT_EQ(surface.declarationsOf(ModuleIndex{1}).size(), 1u);
    EXPECT_TRUE(surface.hasModule(ModuleIndex{1}));
    EXPECT_EQ(surface.findDeclaration(ModuleIndex{1}, "", "f"), &surface.declarationsOf(ModuleIndex{1}).front());
    EXPECT_EQ(surface.findDeclaration(ModuleIndex{1}, "ns", "f"), nullptr);
}

TEST(ModuleSurface, RicherOverloadSetWinsOverLiveSymbol) {
    ModuleSurface surface;
    Symbol poor;
    poor.name = "f";
    Symbol rich;
    rich.name = "f";
    rich.overloads.resize(2);
    const StorageKey key = moduleExportKey(ModuleIndex{1}, "", "f");

    surface.recordDeclaration(ModuleIndex{1}, ExportRecord{key, &rich});
    // Менее полный набор НЕ затирает более полный: указатели обеих записей — на живые символы.
    surface.recordDeclaration(ModuleIndex{1}, ExportRecord{key, &poor});
    ASSERT_EQ(surface.declarationsOf(ModuleIndex{1}).size(), 1u);
    EXPECT_EQ(surface.declarationsOf(ModuleIndex{1}).front().symbol, &rich);

    // Равный набор — входящая запись становится представителем (завершение forward).
    Symbol next;
    next.name = "f";
    next.overloads.resize(2);
    surface.recordDeclaration(ModuleIndex{1}, ExportRecord{key, &next});
    EXPECT_EQ(surface.declarationsOf(ModuleIndex{1}).front().symbol, &next);
}

TEST(ModuleSurface, ReExportUsesOwnerRecordsAndIsTransitive) {
    ModuleSurface surface;
    Symbol owner_decl;
    auto ownerNode = std::make_shared<VarDecl>("f");
    owner_decl.decl = ownerNode.get();
    const StorageKey ownerKey = moduleExportKey(ModuleIndex{1}, "", "f");
    surface.recordDeclaration(ModuleIndex{1}, ExportRecord{ownerKey, &owner_decl});

    // Фасад A публикует f владельца 1.
    const std::vector<const AstNodeBase*> exported{ownerNode.get()};
    surface.recordReExport(ModuleIndex{2}, ModuleIndex{1}, exported);
    ASSERT_EQ(surface.reExportsOf(ModuleIndex{2}).size(), 1u);
    EXPECT_EQ(surface.reExportsOf(ModuleIndex{2}).front().symbol, &owner_decl);

    // Фасад B реэкспортирует через A: имя наследуется (транзитивность).
    surface.recordReExport(ModuleIndex{3}, ModuleIndex{2}, exported);
    ASSERT_EQ(surface.reExportsOf(ModuleIndex{3}).size(), 1u);
    EXPECT_EQ(surface.reExportsOf(ModuleIndex{3}).front().symbol, &owner_decl);
}

TEST(ModuleSurface, ReExportProjectionDerivesOwnerFromKey) {
    ModuleSurface surface;
    Symbol symbol;
    auto node = std::make_shared<VarDecl>("f");
    symbol.decl = node.get();
    node->module = ModuleIndex{1};
    surface.recordDeclaration(ModuleIndex{1}, ExportRecord{moduleExportKey(ModuleIndex{1}, "ns", "f"), &symbol});
    surface.recordReExport(ModuleIndex{2}, ModuleIndex{1}, {node.get()});

    const std::vector<ReExportedNameInfo> projection = surface.reExportProjection();
    ASSERT_EQ(projection.size(), 1u);
    EXPECT_EQ(projection.front().facade.value, 2u);
    EXPECT_EQ(projection.front().owner.value, 1u);
    EXPECT_EQ(projection.front().namespacePath, "ns");
    EXPECT_EQ(projection.front().name, "f");
}

TEST(ModuleSurface, NamespacePresenceIsReadFromKeys) {
    ModuleSurface surface;
    Symbol symbol;
    surface.recordDeclaration(ModuleIndex{1}, ExportRecord{moduleExportKey(ModuleIndex{1}, "ns::sub", "x"), &symbol});
    EXPECT_TRUE(surface.hasNamespace(ModuleIndex{1}, "ns::sub"));
    EXPECT_FALSE(surface.hasNamespace(ModuleIndex{1}, "ns"));
    EXPECT_FALSE(surface.hasNamespace(ModuleIndex{2}, "ns::sub"));
    EXPECT_FALSE(surface.hasNamespace(ModuleIndex::invalid(), "ns::sub"));
}

TEST(ModuleSurface, HasNamespaceIncludesReExportedNames) {
    ModuleSurface surface;
    Symbol owner_decl;
    auto ownerNode = std::make_shared<VarDecl>("x");
    owner_decl.decl = ownerNode.get();
    ownerNode->module = ModuleIndex{1};
    surface.recordDeclaration(ModuleIndex{1}, ExportRecord{moduleExportKey(ModuleIndex{1}, "ns::sub", "x"), &owner_decl});
    surface.recordReExport(ModuleIndex{2}, ModuleIndex{1}, {ownerNode.get()});

    // Фасад публикует область только реэкспортом: собственных объявлений у него нет.
    EXPECT_TRUE(surface.declarationsOf(ModuleIndex{2}).empty());
    EXPECT_TRUE(surface.hasNamespace(ModuleIndex{2}, "ns::sub"));
}

TEST(ModuleSurface, ReExportWithoutMatchesDoesNotCreateSurfaceEntry) {
    ModuleSurface surface;
    Symbol owner_decl;
    auto ownerNode = std::make_shared<VarDecl>("x");
    auto otherNode = std::make_shared<VarDecl>("y");
    owner_decl.decl = ownerNode.get();
    surface.recordDeclaration(ModuleIndex{1}, ExportRecord{moduleExportKey(ModuleIndex{1}, "", "x"), &owner_decl});

    // Ни один узел набора не принадлежит поверхности владельца: публикации нет, записи модуля нет.
    surface.recordReExport(ModuleIndex{2}, ModuleIndex{1}, {otherNode.get()});
    EXPECT_TRUE(surface.reExportsOf(ModuleIndex{2}).empty());
    EXPECT_FALSE(surface.hasModule(ModuleIndex{2}));
}

TEST(ModuleSurface, ReExportSeesOwnerUpdates) {
    ModuleSurface surface;
    Symbol forwardSymbol;
    Symbol definedSymbol;
    auto node = std::make_shared<VarDecl>("f");
    node->module = ModuleIndex{1};
    forwardSymbol.decl = node.get();
    definedSymbol.decl = node.get();
    const StorageKey key = moduleExportKey(ModuleIndex{1}, "", "f");

    surface.recordDeclaration(ModuleIndex{1}, ExportRecord{key, &forwardSymbol});
    surface.recordReExport(ModuleIndex{2}, ModuleIndex{1}, {node.get()});
    // Владелец публикует определение за forward'ом: реэкспорт обязан видеть НОВЫЙ символ
    // (в поверхности хранится ссылка владельца, а не копия указателя).
    surface.recordDeclaration(ModuleIndex{1}, ExportRecord{key, &definedSymbol});

    ASSERT_EQ(surface.reExportsOf(ModuleIndex{2}).size(), 1u);
    EXPECT_EQ(surface.reExportsOf(ModuleIndex{2}).front().symbol, &definedSymbol);
}

TEST(ModuleSurface, RecordDeclarationRequiresSymbol) {
    ModuleSurface surface;
    EXPECT_THROW(surface.recordDeclaration(ModuleIndex{1}, ExportRecord{moduleExportKey(ModuleIndex{1}, "", "f"), nullptr}), std::runtime_error);
}

} // namespace
} // namespace trust
