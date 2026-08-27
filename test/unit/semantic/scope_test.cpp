#include "semantic/symbol_table.hpp"
#include "ast/ast_nodes.hpp"
#include "gtest/gtest.h"

namespace trust {
namespace {

// -- SymbolTable: стек вложенных скоупов (единая таблица символов) --

TEST(SymbolTable, GlobalScopeAlwaysPresent) {
    SymbolTable st;
    EXPECT_EQ(st.depth(), 1u); // глобальный скоуп существует с самого начала

    Symbol s;
    s.name = "x";
    s.type = 1;
    EXPECT_TRUE(st.declare(s));
    ASSERT_NE(st.resolve("x"), nullptr);
    EXPECT_EQ(st.resolve("x")->type, 1);
    EXPECT_EQ(st.resolve("missing"), nullptr);

    // Глобальная таблица (уровень 0) - плоский реестр глобальных имён.
    EXPECT_EQ(st.globalSize(), 1u);
    ASSERT_NE(st.global().lookup("x"), nullptr);
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
    EXPECT_TRUE(st.declare(a1));

    Symbol a2;
    a2.name = "a";
    EXPECT_FALSE(st.declare(a2)); // дубликат в том же скоупе
}

TEST(SymbolTable, ShadowingResolveUp) {
    SymbolTable st;

    Symbol a1;
    a1.name = "a";
    a1.type = 1;
    st.declare(a1);

    st.push(); // вложенный блок (без узла-создателя)
    Symbol a2;
    a2.name = "a";
    a2.type = 2;
    st.declare(a2);

    // Внутри блока видна внутренняя переменная (shadowing).
    ASSERT_NE(st.resolve("a"), nullptr);
    EXPECT_EQ(st.resolve("a")->type, 2);

    st.pop();
    EXPECT_EQ(st.depth(), 1u); // глобальный скоуп не удаляется

    // Вне блока снова видна внешняя переменная.
    ASSERT_NE(st.resolve("a"), nullptr);
    EXPECT_EQ(st.resolve("a")->type, 1);
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
    a.type = 1;
    st.declare(a);

    st.push();
    Symbol a_inner;
    a_inner.name = "a";
    a_inner.type = 2;
    st.declare(a_inner);

    // lookup на текущем (внутреннем) уровне видит только локальное имя.
    const auto* inner = st.current().lookup("a");
    ASSERT_NE(inner, nullptr);
    EXPECT_EQ(inner->type, 2);

    // lookup на глобальном уровне видит только глобальное имя.
    const auto* outer = st.global().lookup("a");
    ASSERT_NE(outer, nullptr);
    EXPECT_EQ(outer->type, 1);
}

} // namespace
} // namespace trust
