#include "semantic/semantic_test_fixture.hpp"

namespace trust {
TEST_F(SymbolTableTest, DeclareResolveGlobal) {
    SymbolTable symtab;
    Symbol sym;
    sym.name = "x";
    auto node = std::make_shared<VarDecl>("x");
    sym.decl = node.get();

    EXPECT_TRUE(symtab.declare(sym));
    EXPECT_EQ(symtab.globalSize(), 1u);
    EXPECT_NE(symtab.resolve("x"), nullptr);
}

TEST_F(SymbolTableTest, DupRejectedInScope) {
    SymbolTable symtab;
    Symbol s1, s2;
    s1.name = "x";
    s2.name = "x";
    auto n1 = std::make_shared<VarDecl>("x");
    auto n2 = std::make_shared<VarDecl>("x");
    s1.decl = n1.get();
    s2.decl = n2.get();

    EXPECT_TRUE(symtab.declare(s1));
    EXPECT_FALSE(symtab.declare(s2)); // дубликат в том же скоупе
}

TEST_F(SymbolTableTest, ResolveNotFound) {
    SymbolTable symtab;
    EXPECT_EQ(symtab.resolve("nonexistent"), nullptr);
}

// -- Function forward declaration tests -------------------

} // namespace trust
