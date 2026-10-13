// test/unit/hir/hir_verify_test.cpp
// Структурный верификатор HIR: recovery-узлы, полнота материализованных типов,
// согласованность goto/меток внутри функции.
#include "../../semantic/semantic_test_fixture.hpp"
#include "ir/hir_verify.hpp"
#include "ast/binary_op.hpp"

namespace trust {
namespace {

using trust::ParserToken;

TEST_F(SemanticTest, VerifyHirAcceptsMaterializedTypes) {
    auto name = Term::Create(TermID::NAME, "x", {}, parser::token_type::NAME);
    auto var = std::make_shared<VarDecl>(std::move(name), std::make_shared<IdentType>("Int32"), std::make_shared<Literal>(ParserToken::Kind::IntLiteral, "5"));
    std::vector<AstNodePtr> seq{std::move(var)};
    ASSERT_TRUE(SemanticPassRunner(m_ctx).run(seq));
    const HirModule hir = buildHir(seq, m_ctx);
    EXPECT_FALSE(verifyHir(hir).has_value());
}

TEST_F(SemanticTest, VerifyHirRejectsUnmaterializedAnnotation) {
    // Без семантики аннотация не разрешена: это дефект материализации, а не пользовательская ошибка.
    auto name = Term::Create(TermID::NAME, "x", {}, parser::token_type::NAME);
    auto var = std::make_shared<VarDecl>(std::move(name), std::make_shared<IdentType>("Int32"), std::make_shared<Literal>(ParserToken::Kind::IntLiteral, "5"));
    std::vector<AstNodePtr> seq{std::move(var)};
    const HirModule hir = buildHir(seq, m_ctx);
    const auto violation = verifyHir(hir);
    ASSERT_TRUE(violation.has_value());
    EXPECT_NE(violation->find("not materialized"), std::string::npos) << *violation;
}

TEST_F(SemanticTest, VerifyHirRejectsRecoveryNode) {
    auto fn = std::make_shared<FuncDecl>("f");
    std::vector<AstNodePtr> body;
    body.push_back(std::make_shared<ErrorExpr>(std::make_shared<Literal>(ParserToken::Kind::IntLiteral, "1")));
    fn->m_body = std::move(body);
    std::vector<AstNodePtr> seq{std::move(fn)};
    const HirModule hir = buildHir(seq, m_ctx);
    const auto violation = verifyHir(hir);
    ASSERT_TRUE(violation.has_value());
    EXPECT_NE(violation->find("ErrorExpr"), std::string::npos) << *violation;
}

TEST_F(SemanticTest, VerifyHirRejectsUnresolvedMemberAccessObject) {
    auto left = std::make_shared<IdentName>("obj");
    auto right = std::make_shared<IdentName>("field");
    auto access = std::make_shared<Binary>(ParserToken::Kind::MemberAccess, left, right);
    auto fn = std::make_shared<FuncDecl>("f");
    std::vector<AstNodePtr> body;
    body.push_back(access);
    fn->m_body = std::move(body);
    std::vector<AstNodePtr> seq{std::move(fn)};

    const HirModule hir = buildHir(seq, m_ctx);
    const auto violation = verifyHir(hir);
    ASSERT_TRUE(violation.has_value());
    EXPECT_NE(violation->find("member access object 'obj'"), std::string::npos) << *violation;
}

TEST_F(SemanticTest, VerifyHirAcceptsResolvedMemberAccessObject) {
    auto decl = std::make_shared<VarDecl>("obj", nullptr, std::make_shared<Literal>(ParserToken::Kind::IntLiteral, "1"));
    const auto int32 = m_ctx.readTypes().findType("Int32");
    ASSERT_TRUE(int32.has_value());
    decl->inferredType = QualType::fromTypeId(*int32);
    auto left = std::make_shared<IdentName>("obj");
    left->resolvedDecl = decl.get();
    auto right = std::make_shared<IdentName>("field");
    auto access = std::make_shared<Binary>(ParserToken::Kind::MemberAccess, left, right);
    auto fn = std::make_shared<FuncDecl>("f");
    std::vector<AstNodePtr> body;
    body.push_back(decl);
    body.push_back(access);
    fn->m_body = std::move(body);
    std::vector<AstNodePtr> seq{std::move(fn)};

    const HirModule hir = buildHir(seq, m_ctx);
    EXPECT_FALSE(verifyHir(hir).has_value());
}

TEST_F(SemanticTest, VerifyHirRejectsNamedBreak) {
    auto brk = std::make_shared<JumpStmt>(ParserToken::Kind::BreakStmt);
    brk->m_label = std::make_shared<IdentName>("L");
    auto fn = std::make_shared<FuncDecl>("f");
    std::vector<AstNodePtr> body;
    body.push_back(brk);
    fn->m_body = std::move(body);

    std::vector<AstNodePtr> roots{fn};
    const HirModule hir(std::move(roots));
    const auto violation = verifyHir(hir);
    ASSERT_TRUE(violation.has_value());
    EXPECT_NE(violation->find("named break/continue"), std::string::npos) << *violation;
}

TEST_F(SemanticTest, VerifyHirRejectsNestedSemicolon) {
    auto inner = std::make_shared<SemicolonStmt>(std::make_shared<Literal>(ParserToken::Kind::IntLiteral, "1"));
    auto outer = std::make_shared<SemicolonStmt>(inner);
    auto fn = std::make_shared<FuncDecl>("f");
    std::vector<AstNodePtr> body;
    body.push_back(outer);
    fn->m_body = std::move(body);

    std::vector<AstNodePtr> roots{fn};
    const HirModule hir(std::move(roots));
    const auto violation = verifyHir(hir);
    ASSERT_TRUE(violation.has_value());
    EXPECT_NE(violation->find("SemicolonStmt"), std::string::npos) << *violation;
}

TEST_F(SemanticTest, VerifyHirRequiresInferredTypeForUntypedVariable) {
    auto var = std::make_shared<VarDecl>("x", nullptr, std::make_shared<Literal>(ParserToken::Kind::IntLiteral, "1"));
    std::vector<AstNodePtr> roots{var};
    const HirModule hir(std::move(roots));
    const auto violation = verifyHir(hir);
    ASSERT_TRUE(violation.has_value());
    EXPECT_NE(violation->find("inferred type of variable 'x'"), std::string::npos) << *violation;
}

TEST_F(SemanticTest, VerifyHirRejectsCallCastArityMismatch) {
    const auto int32 = m_ctx.readTypes().findType("Int32");
    ASSERT_TRUE(int32.has_value());
    auto call = std::make_shared<CallExpr>(ParserToken::Kind::CallExpr, std::make_shared<IdentName>("f"));
    call->m_args = std::vector<AstNodePtr>{std::make_shared<Literal>(ParserToken::Kind::IntLiteral, "1")};
    call->argCasts = {*int32, *int32};

    std::vector<AstNodePtr> roots{call};
    const HirModule hir(std::move(roots));
    const auto violation = verifyHir(hir);
    ASSERT_TRUE(violation.has_value());
    EXPECT_NE(violation->find("argCasts"), std::string::npos) << *violation;
}

TEST_F(SemanticTest, VerifyHirRequiresOverflowClassification) {
    auto add = std::make_shared<Binary>(ParserToken::Kind::MathOp, std::make_shared<Literal>(ParserToken::Kind::IntLiteral, "1"),
                                        std::make_shared<Literal>(ParserToken::Kind::IntLiteral, "2"));
    add->m_op = BinaryOp::Add;

    std::vector<AstNodePtr> roots{add};
    const HirModule hir(std::move(roots));
    const auto violation = verifyHir(hir);
    ASSERT_TRUE(violation.has_value());
    EXPECT_NE(violation->find("overflow-check"), std::string::npos) << *violation;
}

TEST_F(SemanticTest, VerifyHirRejectsDuplicateSyntheticTemp) {
    const auto int32 = m_ctx.readTypes().findType("Int32");
    ASSERT_TRUE(int32.has_value());
    auto first = std::make_shared<VarDecl>("_w0", nullptr, std::make_shared<Literal>(ParserToken::Kind::IntLiteral, "0"));
    auto second = std::make_shared<VarDecl>("_w0", nullptr, std::make_shared<Literal>(ParserToken::Kind::IntLiteral, "0"));
    first->inferredType = QualType::fromTypeId(*int32);
    second->inferredType = QualType::fromTypeId(*int32);
    auto fn = std::make_shared<FuncDecl>("f");
    std::vector<AstNodePtr> body;
    body.push_back(first);
    body.push_back(second);
    fn->m_body = std::move(body);

    std::vector<AstNodePtr> roots{fn};
    const HirModule hir(std::move(roots));
    const auto violation = verifyHir(hir);
    ASSERT_TRUE(violation.has_value());
    EXPECT_NE(violation->find("synthetic temporary '_w0'"), std::string::npos) << *violation;
}

TEST_F(SemanticTest, VerifyHirRejectsDuplicateLabel) {
    auto fn = std::make_shared<FuncDecl>("f");
    std::vector<AstNodePtr> body;
    body.push_back(std::make_shared<LabelRef>(ParserToken::Kind::LabelStmt, "L"));
    body.push_back(std::make_shared<LabelRef>(ParserToken::Kind::LabelStmt, "L"));
    fn->m_body = std::move(body);

    std::vector<AstNodePtr> roots{fn};
    const HirModule hir(std::move(roots));
    const auto violation = verifyHir(hir);
    ASSERT_TRUE(violation.has_value());
    EXPECT_NE(violation->find("label 'L' is defined 2 times"), std::string::npos) << *violation;
}

TEST_F(SemanticTest, VerifyHirReportsOriginForSyntheticNode) {
    auto originNode = std::make_shared<JumpStmt>(ParserToken::Kind::BreakStmt);
    auto gotoStmt = std::make_shared<LabelRef>(ParserToken::Kind::GotoStmt, "missing");
    gotoStmt->origin = originNode.get();
    auto fn = std::make_shared<FuncDecl>("f");
    std::vector<AstNodePtr> body;
    body.push_back(gotoStmt);
    fn->m_body = std::move(body);

    std::vector<AstNodePtr> roots{fn};
    const HirModule hir(std::move(roots));
    const auto violation = verifyHir(hir);
    ASSERT_TRUE(violation.has_value());
    EXPECT_NE(violation->find("origin BreakStmt"), std::string::npos) << *violation;
}

TEST_F(SemanticTest, VerifyHirChecksGotoLabelsPerFunction) {
    auto fn = std::make_shared<FuncDecl>("f");
    std::vector<AstNodePtr> body;
    body.push_back(std::make_shared<LabelRef>(ParserToken::Kind::GotoStmt, "missing"));
    fn->m_body = std::move(body);
    std::vector<AstNodePtr> seq{std::move(fn)};
    const HirModule hir = buildHir(seq, m_ctx);
    const auto violation = verifyHir(hir);
    ASSERT_TRUE(violation.has_value());
    EXPECT_NE(violation->find("goto 'missing'"), std::string::npos) << *violation;
}

TEST_F(SemanticTest, VerifyHirAcceptsMatchedGotoLabel) {
    auto fn = std::make_shared<FuncDecl>("f");
    std::vector<AstNodePtr> body;
    body.push_back(std::make_shared<LabelRef>(ParserToken::Kind::GotoStmt, "L"));
    body.push_back(std::make_shared<LabelRef>(ParserToken::Kind::LabelStmt, "L"));
    fn->m_body = std::move(body);
    std::vector<AstNodePtr> seq{std::move(fn)};
    const HirModule hir = buildHir(seq, m_ctx);
    EXPECT_FALSE(verifyHir(hir).has_value());
}

} // namespace
} // namespace trust
