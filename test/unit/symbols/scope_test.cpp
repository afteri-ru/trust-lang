#include "symbols/symbol_table.hpp"
#include "ast/ast_nodes.hpp"
#include "gtest/gtest.h"

#include <string>
#include <vector>

namespace trust {
namespace {

// -- SymbolTable: стек вложенных скоупов (единая таблица символов) --

TEST(SymbolTable, GlobalScopeAlwaysPresent) {
    SymbolTable st;
    EXPECT_EQ(st.depth(), 1u); // глобальный скоуп существует с самого начала

    Symbol s;
    s.name = "x";
    s.type = QualType::fromTypeId(TypeId::fromRaw(1));
    EXPECT_EQ(st.declare(s), DeclResult::Inserted);
    ASSERT_NE(st.resolve("x"), nullptr);
    EXPECT_EQ(st.resolve("x")->type.getTypeId(), TypeId::fromRaw(1));
    EXPECT_EQ(st.resolve("missing"), nullptr);

    // Глобальная таблица (уровень 0) - плоский реестр глобальных имён.
    EXPECT_EQ(st.globalSize(), 1u);
    ASSERT_NE(st.global().lookup(exportedKeyOf("x")), nullptr);
}

TEST(SymbolTable, GlobalScopeCreatorIsNull) {
    SymbolTable st;
    EXPECT_EQ(st.depth(), 1u);
    EXPECT_EQ(st.currentCreator(), nullptr); // глобальный скоуп не имеет создающего узла
    EXPECT_EQ(st.current().creator, nullptr);
}

TEST(SymbolTable, DeclareDuplicateInSameScopeFails) {
    SymbolTable st;
    Symbol a1;
    a1.name = "a";
    EXPECT_EQ(st.declare(a1), DeclResult::Inserted);

    Symbol a2;
    a2.name = "a";
    EXPECT_EQ(st.declare(a2), DeclResult::Duplicate); // дубликат в том же скоупе
}

TEST(SymbolTable, ShadowingResolveUp) {
    SymbolTable st;

    Symbol a1;
    a1.name = "a";
    a1.type = QualType::fromTypeId(TypeId::fromRaw(1));
    st.declare(a1);

    st.push(); // вложенный блок (без узла-создателя)
    Symbol a2;
    a2.name = "a";
    a2.type = QualType::fromTypeId(TypeId::fromRaw(2));
    st.declare(a2);

    // Внутри блока видна внутренняя переменная (shadowing).
    ASSERT_NE(st.resolve("a"), nullptr);
    EXPECT_EQ(st.resolve("a")->type.getTypeId(), TypeId::fromRaw(2));

    st.pop();
    EXPECT_EQ(st.depth(), 1u); // глобальный скоуп не удаляется

    // Вне блока снова видна внешняя переменная.
    ASSERT_NE(st.resolve("a"), nullptr);
    EXPECT_EQ(st.resolve("a")->type.getTypeId(), TypeId::fromRaw(1));
}

TEST(SymbolTable, NestedScopeResolveOuter) {
    SymbolTable st;

    Symbol outer;
    outer.name = "outer";
    st.declare(outer);

    st.push();
    Symbol inner;
    inner.name = "inner";
    st.declare(inner);

    // Из вложенного скоупа видны имена из внешних (поиск вверх).
    EXPECT_NE(st.resolve("outer"), nullptr);
    EXPECT_NE(st.resolve("inner"), nullptr);

    st.pop();
    // После выхода внутреннее имя недоступно.
    EXPECT_EQ(st.resolve("inner"), nullptr);
    EXPECT_NE(st.resolve("outer"), nullptr);
}

TEST(SymbolTable, ModuleLevelKindTracksModuleRoot) {
    SymbolTable st;
    EXPECT_EQ(st.scopes().node(0).kind, ScopeKind::Global);
    EXPECT_FALSE(st.atModuleLevel());

    auto module = std::make_shared<ModuleNode>(ModuleIndex{0}, std::string("main"));
    st.push(module.get());
    EXPECT_TRUE(st.atModuleLevel());
    EXPECT_EQ(st.scopes().current().kind, ScopeKind::ModuleRoot);

    st.push();
    EXPECT_FALSE(st.atModuleLevel());
    st.pop();
    EXPECT_TRUE(st.atModuleLevel());
    st.pop();
    EXPECT_FALSE(st.atModuleLevel());
}

TEST(SymbolTable, ImplicitModuleRootWithoutModuleNode) {
    SymbolTable st;
    // Прелюдия/тесты без ModuleNode: первый вложенный скоуп — модульный уровень.
    st.push();
    EXPECT_TRUE(st.atModuleLevel());
    st.push();
    EXPECT_FALSE(st.atModuleLevel());
}

TEST(SymbolTable, CreatorStoredPerScope) {
    SymbolTable st;

    // Имитация блока: узел-владелец скоупа. Контракт - тождество указателя.
    auto block = std::make_shared<ScopeBlock>();
    st.push(block.get());

    EXPECT_EQ(st.currentCreator(), block.get());
    EXPECT_NE(st.current().creator, nullptr);
    EXPECT_EQ(st.current().creator, block.get());

    // Вложенный скоуп без узла.
    st.push();
    EXPECT_EQ(st.currentCreator(), nullptr);

    st.pop();
    EXPECT_EQ(st.currentCreator(), block.get());
    st.pop();
    EXPECT_EQ(st.depth(), 1u);
    EXPECT_EQ(st.currentCreator(), nullptr);
}

TEST(SymbolTable, ScopeLookupIsSingleLevel) {
    SymbolTable st;

    Symbol a;
    a.name = "a";
    a.type = QualType::fromTypeId(TypeId::fromRaw(1));
    st.declare(a);

    st.push();
    Symbol a_inner;
    a_inner.name = "a";
    a_inner.type = QualType::fromTypeId(TypeId::fromRaw(2));
    st.declare(a_inner);

    // lookup на текущем (внутреннем) уровне видит только локальное имя. Модульный скоуп
    // регистрирует объявления под ключом модульной ИДЕНТИЧНОСТИ (не плоским), поэтому
    // используется вариант поиска скоупа, учитывающий видимость.
    const auto* inner = st.lookupCurrent("a");
    ASSERT_NE(inner, nullptr);
    EXPECT_EQ(inner->type.getTypeId(), TypeId::fromRaw(2));

    // lookup на глобальном уровне видит только глобальное имя.
    const auto* outer = st.global().lookup(exportedKeyOf("a"));
    ASSERT_NE(outer, nullptr);
    EXPECT_EQ(outer->type.getTypeId(), TypeId::fromRaw(1));
}

TEST(SymbolTable, ScopeIterationOrderIsDeterministic) {
    // Обход скоупа задаётся порядком StorageKey (visibility → module → namespacePath → name).
    // От него зависит порядок диагностик переноса namespace-блока: он не должен зависеть от
    // порядка вставки объявлений.
    const auto order = [](const std::vector<StorageKey>& insertion) {
        Scope scope;
        for (const StorageKey& key : insertion) {
            scope.symbols.emplace(key, Symbol{});
        }
        std::vector<std::string> result;
        for (const auto& [key, sym] : scope.symbols) {
            (void)sym;
            result.push_back(storageKeyText(key));
        }
        return result;
    };

    const std::vector<StorageKey> keys = {
        exportedKey("", "a"),
        exportedKey("ns", "b"),
        moduleKey(ModuleIndex{1}, {}, "a", /*importHidden=*/false),
        moduleKey(ModuleIndex{1}, {}, "a", /*importHidden=*/true),
        moduleKey(ModuleIndex{2}, {}, "a", /*importHidden=*/false),
    };
    const std::vector<StorageKey> reversed(keys.rbegin(), keys.rend());

    const std::vector<std::string> forwardOrder = order(keys);
    EXPECT_EQ(forwardOrder, order(reversed)) << "iteration order must not depend on insertion order";

    // Экспорт идёт первым, затем import-hidden, затем file-private (порядок VisibilityClass).
    ASSERT_EQ(forwardOrder.size(), keys.size());
    EXPECT_EQ(forwardOrder.front(), "a");
    EXPECT_EQ(forwardOrder.at(1), "ns::b");
}

} // namespace
} // namespace trust
