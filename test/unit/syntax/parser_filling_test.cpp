// test/unit/syntax/parser_filling_test.cpp - unit-тесты FILLING `... expr ...` (Term -> AST).
// Единый узел (kind Filling, класс Sequence, операнд - в m_body[0]) строится ОДИНАКОВО в
// литерале массива и в аргументах вызова (общее правило грамматики `arg`); единый источник
// детей collectChildren отдаёт операнд.

#include "syntax/parser_test_fixture.hpp"

#include <vector>

namespace {

// Рекурсивный сбор всех узлов kind=Filling через единый источник детей collectChildren.
void collectFilling(AstNodePtr& n, std::vector<Sequence*>& out) {
    if (!n) {
        return;
    }
    if (n->kind() == ParserToken::Kind::Filling) {
        out.push_back(static_cast<Sequence*>(n.get()));
    }
    std::vector<AstNodePtr*> slots;
    n->collectChildren(slots);
    for (auto* slot : slots) {
        if (slot) {
            collectFilling(*slot, out);
        }
    }
}

} // namespace

// `... expr ...` в литерале массива: единый узел Filling с операндом-литералом.
TEST_F(ParserTest, FillingInArrayLiteralParsesToAstNode) {
    ASSERT_TRUE(Parse("@f() := { v:Int32[3] := [1, 2, ... 7 ...,]; };"));
    std::vector<AstNodePtr> nodes = TermToAstConverter::termToAst(ast, m_ctx);
    std::vector<Sequence*> fillings;
    for (auto& n : nodes) {
        collectFilling(n, fillings);
    }
    ASSERT_EQ(fillings.size(), 1u);
    Sequence& f = *fillings.front();
    // Операнд `expr` - единственный ребёнок (collectChildren/children).
    std::vector<AstNodePtr*> slots;
    f.collectChildren(slots);
    ASSERT_EQ(slots.size(), 1u);
    ASSERT_NE(slots.front()->get(), nullptr);
    EXPECT_EQ(slots.front()->get()->kind(), ParserToken::Kind::IntLiteral);
    EXPECT_EQ(slots.front()->get()->text(), "7");
}

// `... expr ...` в аргументах вызова: тот же узел (правило `arg` - общее для всех `args`).
TEST_F(ParserTest, FillingInCallArgsParsesToAstNode) {
    ASSERT_TRUE(Parse("@f() := { g(1, ... 9 ...); };"));
    std::vector<AstNodePtr> nodes = TermToAstConverter::termToAst(ast, m_ctx);
    std::vector<Sequence*> fillings;
    for (auto& n : nodes) {
        collectFilling(n, fillings);
    }
    ASSERT_EQ(fillings.size(), 1u);
    Sequence& f = *fillings.front();
    ASSERT_EQ(f.m_body.size(), 1u);
    ASSERT_NE(f.m_body[0].get(), nullptr);
    EXPECT_EQ(f.m_body[0]->kind(), ParserToken::Kind::IntLiteral);
    EXPECT_EQ(f.m_body[0]->text(), "9");
}
