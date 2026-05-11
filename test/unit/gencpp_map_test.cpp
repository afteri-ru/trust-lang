#include "gencpp/ast.hpp"
#include "gencpp/cpp_generator.hpp"
#include "diag/context.hpp"
#include "diag/location.hpp"

#include <gtest/gtest.h>

#include <memory>
#include <string>
#include <string_view>
#include <vector>

using namespace trust;

// Helper: make a vector of decls from move args
// Can't use initializer_list with unique_ptr
template <typename... Args>
static std::vector<std::unique_ptr<Decl>> make_decls(Args&&... args) {
    std::vector<std::unique_ptr<Decl>> v;
    (v.push_back(std::unique_ptr<Decl>(std::forward<Args>(args).release())), ...);
    return v;
}

// Helper: make a vector of block items from move args
template <typename... Args>
static BlockBody make_block_body(Args&&... args) {
    BlockBody v;
    (v.push_back(std::unique_ptr<Stmt>(std::forward<Args>(args).release())), ...);
    return v;
}

// Helper: make a vector of Expr from move args
static std::vector<std::unique_ptr<Expr>> make_exprs() {
    return {};
}
template <typename... Args>
static std::vector<std::unique_ptr<Expr>> make_exprs(std::unique_ptr<Expr>&& first, Args&&... rest) {
    std::vector<std::unique_ptr<Expr>> v;
    v.reserve(1 + sizeof...(rest));
    v.push_back(std::move(first));
    (v.push_back(std::unique_ptr<Expr>(std::forward<Args>(rest).release())), ...);
    return v;
}

// ============================================================================
// Tests for gencpp mapping
// ============================================================================

TEST(GenCppMappingTest, VarDeclSimple) {
    auto init = std::make_unique<IntLiteral>(42);
    auto varDecl = std::make_unique<VarDecl>("x", std::move(init));
    varDecl->var_type = TypeInfo(TypeKind::Int32, "int");
    varDecl->source = TokenInfo::make(ParserToken::Kind::VarDecl, "create x = 42");
    varDecl->source->text = "create x = 42";

    auto program = std::make_unique<Program>(make_decls(std::move(varDecl)));

    Context ctx(".");
    MapperFile trustIdx = ctx.add_source("test.trust", "create x = 42;");
    MapperFile cppIdx = ctx.add_output("test.cpp");

    // trust source: "create x = 42;" — 14 символов
    // getText(1,14) = substr(0,13) = "create x = 42"
    program->items[0]->source->range = MapperRange{MapperLocation::makeLoc(trustIdx, 1), MapperLocation::makeLoc(trustIdx, 14)};

    CppGenerator gen;
    gen.set_context(&ctx);
    gen.set_output_file_idx(cppIdx);

    (void)gen.generate(program.get());
    gen.finalize_output(ctx, cppIdx);

    auto mappings = ctx.toReader()->getTrustFileMappings(ReaderFile::from(trustIdx));
    ASSERT_EQ(mappings.size(), 1);

    EXPECT_EQ(
        ctx.getText(MapperRange{MapperLocation::fromPacked(mappings[0].from.begin.asPacked()), MapperLocation::fromPacked(mappings[0].from.end.asPacked())}),
        "create x = 42");
    // StmtBegin/StmtEnd дают "int x = 42;\n" (весь стейтмент)
    EXPECT_EQ(ctx.getText(MapperRange{MapperLocation::fromPacked(mappings[0].to.begin.asPacked()), MapperLocation::fromPacked(mappings[0].to.end.asPacked())}),
              "int x = 42;\n");
}

TEST(GenCppMappingTest, RoundtripTrustToCpp) {
    auto init = std::make_unique<IntLiteral>(42);
    auto varDecl = std::make_unique<VarDecl>("x", std::move(init));
    varDecl->var_type = TypeInfo(TypeKind::Int32, "int");
    varDecl->source = TokenInfo::make(ParserToken::Kind::VarDecl, "create x = 42");
    varDecl->source->text = "create x = 42";

    auto program = std::make_unique<Program>(make_decls(std::move(varDecl)));

    Context ctx(".");
    MapperFile trustIdx = ctx.add_source("test.trust", "create x = 42;");
    MapperFile cppIdx = ctx.add_output("test.cpp");

    program->items[0]->source->range = MapperRange{MapperLocation::makeLoc(trustIdx, 1), MapperLocation::makeLoc(trustIdx, 14)};

    CppGenerator gen;
    gen.set_context(&ctx);
    gen.set_output_file_idx(cppIdx);

    (void)gen.generate(program.get());
    gen.finalize_output(ctx, cppIdx);

    auto mappings = ctx.toReader()->getTrustFileMappings(ReaderFile::from(trustIdx));
    ASSERT_EQ(mappings.size(), 1);

    auto roundtrip = ctx.toReader()->getMapTrustToCpp(ReaderLocation::fromPacked(mappings[0].from.begin.asPacked()));
    ASSERT_TRUE(roundtrip.has_value());
    EXPECT_EQ(roundtrip->begin, mappings[0].to.begin);
}

TEST(GenCppMappingTest, RoundtripCppToTrust) {
    auto init = std::make_unique<IntLiteral>(42);
    auto varDecl = std::make_unique<VarDecl>("x", std::move(init));
    varDecl->var_type = TypeInfo(TypeKind::Int32, "int");
    varDecl->source = TokenInfo::make(ParserToken::Kind::VarDecl, "create x = 42");
    varDecl->source->text = "create x = 42";

    auto program = std::make_unique<Program>(make_decls(std::move(varDecl)));

    Context ctx(".");
    MapperFile trustIdx = ctx.add_source("test.trust", "create x = 42;");
    MapperFile cppIdx = ctx.add_output("test.cpp");

    program->items[0]->source->range = MapperRange{MapperLocation::makeLoc(trustIdx, 1), MapperLocation::makeLoc(trustIdx, 14)};

    CppGenerator gen;
    gen.set_context(&ctx);
    gen.set_output_file_idx(cppIdx);

    (void)gen.generate(program.get());
    gen.finalize_output(ctx, cppIdx);

    auto mappings = ctx.toReader()->getTrustFileMappings(ReaderFile::from(trustIdx));
    ASSERT_EQ(mappings.size(), 1);

    auto roundtripBack = ctx.toReader()->getMapCppToTrust(ReaderLocation::fromPacked(mappings[0].to.begin.asPacked()));
    ASSERT_TRUE(roundtripBack.has_value());
    EXPECT_EQ(roundtripBack->begin, mappings[0].from.begin);
}

TEST(GenCppMappingTest, MultipleVarDecls) {
    auto init1 = std::make_unique<IntLiteral>(5);
    auto v1 = std::make_unique<VarDecl>("a", std::move(init1));
    v1->var_type = TypeInfo(TypeKind::Int32, "int");
    v1->source = TokenInfo::make(ParserToken::Kind::VarDecl, "create a = 5");
    v1->source->text = "create a = 5";

    auto init2 = std::make_unique<IntLiteral>(10);
    auto v2 = std::make_unique<VarDecl>("b", std::move(init2));
    v2->var_type = TypeInfo(TypeKind::Int32, "int");
    v2->source = TokenInfo::make(ParserToken::Kind::VarDecl, "create b = 10");
    v2->source->text = "create b = 10";

    auto program = std::make_unique<Program>(make_decls(std::move(v1), std::move(v2)));

    Context ctx(".");
    MapperFile trustIdx = ctx.add_source("test.trust", "create a = 5;\ncreate b = 10;\n");
    MapperFile cppIdx = ctx.add_output("test.cpp");

    program->items[0]->source->range = MapperRange{MapperLocation::makeLoc(trustIdx, 1), MapperLocation::makeLoc(trustIdx, 13)};
    program->items[1]->source->range = MapperRange{MapperLocation::makeLoc(trustIdx, 15), MapperLocation::makeLoc(trustIdx, 28)};

    CppGenerator gen;
    gen.set_context(&ctx);
    gen.set_output_file_idx(cppIdx);

    (void)gen.generate(program.get());
    gen.finalize_output(ctx, cppIdx);

    auto mappings = ctx.toReader()->getTrustFileMappings(ReaderFile::from(trustIdx));
    ASSERT_EQ(mappings.size(), 2);

    EXPECT_EQ(
        ctx.getText(MapperRange{MapperLocation::fromPacked(mappings[0].from.begin.asPacked()), MapperLocation::fromPacked(mappings[0].from.end.asPacked())}),
        "create a = 5");
    EXPECT_EQ(ctx.getText(MapperRange{MapperLocation::fromPacked(mappings[0].to.begin.asPacked()), MapperLocation::fromPacked(mappings[0].to.end.asPacked())}),
              "int a = 5;\n");

    EXPECT_EQ(
        ctx.getText(MapperRange{MapperLocation::fromPacked(mappings[1].from.begin.asPacked()), MapperLocation::fromPacked(mappings[1].from.end.asPacked())}),
        "create b = 10");
    EXPECT_EQ(ctx.getText(MapperRange{MapperLocation::fromPacked(mappings[1].to.begin.asPacked()), MapperLocation::fromPacked(mappings[1].to.end.asPacked())}),
              "int b = 10;\n");

    EXPECT_LT(mappings[0].from.begin, mappings[1].from.begin);
    EXPECT_LT(mappings[0].to.begin, mappings[1].to.begin);
}

TEST(GenCppMappingTest, MappingEntryCount) {
    auto init1 = std::make_unique<IntLiteral>(1);
    auto v1 = std::make_unique<VarDecl>("a", std::move(init1));
    v1->var_type = TypeInfo(TypeKind::Int32, "int");
    v1->source = TokenInfo::make(ParserToken::Kind::VarDecl, "create a = 1");
    v1->source->text = "create a = 1";

    auto init2 = std::make_unique<IntLiteral>(2);
    auto v2 = std::make_unique<VarDecl>("b", std::move(init2));
    v2->var_type = TypeInfo(TypeKind::Int32, "int");
    v2->source = TokenInfo::make(ParserToken::Kind::VarDecl, "create b = 2");
    v2->source->text = "create b = 2";

    auto init3 = std::make_unique<IntLiteral>(3);
    auto v3 = std::make_unique<VarDecl>("c", std::move(init3));
    v3->var_type = TypeInfo(TypeKind::Int32, "int");
    v3->source = TokenInfo::make(ParserToken::Kind::VarDecl, "create c = 3");
    v3->source->text = "create c = 3";

    auto program = std::make_unique<Program>(make_decls(std::move(v1), std::move(v2), std::move(v3)));

    Context ctx(".");
    MapperFile trustIdx = ctx.add_source("test.trust", "create a = 1; create b = 2; create c = 3;\n");
    MapperFile cppIdx = ctx.add_output("test.cpp");

    program->items[0]->source->range = MapperRange{MapperLocation::makeLoc(trustIdx, 1), MapperLocation::makeLoc(trustIdx, 12)};
    program->items[1]->source->range = MapperRange{MapperLocation::makeLoc(trustIdx, 14), MapperLocation::makeLoc(trustIdx, 25)};
    program->items[2]->source->range = MapperRange{MapperLocation::makeLoc(trustIdx, 27), MapperLocation::makeLoc(trustIdx, 38)};

    CppGenerator gen;
    gen.set_context(&ctx);
    gen.set_output_file_idx(cppIdx);

    (void)gen.generate(program.get());
    gen.finalize_output(ctx, cppIdx);

    auto mappings = ctx.toReader()->getTrustFileMappings(ReaderFile::from(trustIdx));
    ASSERT_EQ(mappings.size(), 3);

    EXPECT_NE(mappings[0].from.begin, mappings[1].from.begin);
    EXPECT_NE(mappings[1].from.begin, mappings[2].from.begin);
    EXPECT_NE(mappings[0].from.end, mappings[1].from.end);

    EXPECT_LT(mappings[0].from.begin, mappings[1].from.begin);
    EXPECT_LT(mappings[1].from.begin, mappings[2].from.begin);

    EXPECT_LT(mappings[0].to.begin, mappings[1].to.begin);
    EXPECT_LT(mappings[1].to.begin, mappings[2].to.begin);
}

TEST(GenCppMappingTest, FuncDeclReturnStmt) {
    auto returnVal = std::make_unique<IntLiteral>(42);
    auto retStmt = std::make_unique<ReturnStmt>(std::move(returnVal));
    retStmt->source = TokenInfo::make(ParserToken::Kind::ReturnStmt, "return 42");
    retStmt->source->text = "return 42";

    BlockBody body = make_block_body(std::move(retStmt));
    auto block = std::make_unique<BlockStmt>(std::move(body));

    auto func = std::make_unique<FuncDecl>("foo", TypeKind::Int32, std::vector<std::unique_ptr<ParamDecl>>{}, std::move(block));
    func->return_type = TypeInfo(TypeKind::Int32, "int");

    auto program = std::make_unique<Program>(make_decls(std::move(func)));

    Context ctx(".");
    MapperFile trustIdx = ctx.add_source("test.trust", "func foo() -> int { return 42; }");
    MapperFile cppIdx = ctx.add_output("test.cpp");

    auto* funcDecl = static_cast<FuncDecl*>(program->items[0].get());
    if (funcDecl->body && !funcDecl->body->body.empty()) {
        funcDecl->body->body[0]->source->range = MapperRange{MapperLocation::makeLoc(trustIdx, 21), MapperLocation::makeLoc(trustIdx, 30)};
    }

    CppGenerator gen;
    gen.set_context(&ctx);
    gen.set_output_file_idx(cppIdx);

    (void)gen.generate(program.get());
    gen.finalize_output(ctx, cppIdx);

    auto mappings = ctx.toReader()->getTrustFileMappings(ReaderFile::from(trustIdx));
    ASSERT_EQ(mappings.size(), 1);

    EXPECT_EQ(
        ctx.getText(MapperRange{MapperLocation::fromPacked(mappings[0].from.begin.asPacked()), MapperLocation::fromPacked(mappings[0].from.end.asPacked())}),
        "return 42");
    // ReturnStmt генерирует "    return 42;\n" — весь стейтмент с отступом
    EXPECT_EQ(ctx.getText(MapperRange{MapperLocation::fromPacked(mappings[0].to.begin.asPacked()), MapperLocation::fromPacked(mappings[0].to.end.asPacked())}),
              "return 42;\n");
}

TEST(GenCppMappingTest, AssignStmtMapping) {
    auto target = std::make_unique<VarRef>("x");
    auto val = std::make_unique<IntLiteral>(7);
    auto assign = std::make_unique<AssignmentStmt>(std::move(target), std::move(val));
    assign->source = TokenInfo::make(ParserToken::Kind::AssignmentStmt, "x = 7");
    assign->source->text = "x = 7";

    BlockBody body = make_block_body(std::move(assign));
    auto block = std::make_unique<BlockStmt>(std::move(body));

    auto func = std::make_unique<FuncDecl>("test", TypeKind::Void, std::vector<std::unique_ptr<ParamDecl>>{}, std::move(block));
    func->return_type = TypeInfo(TypeKind::Void, "void");

    auto program = std::make_unique<Program>(make_decls(std::move(func)));

    Context ctx(".");
    MapperFile trustIdx = ctx.add_source("test.trust", "func test() { x = 7; }");
    MapperFile cppIdx = ctx.add_output("test.cpp");

    auto* funcDecl = static_cast<FuncDecl*>(program->items[0].get());
    if (funcDecl->body && !funcDecl->body->body.empty()) {
        funcDecl->body->body[0]->source->range = MapperRange{MapperLocation::makeLoc(trustIdx, 15), MapperLocation::makeLoc(trustIdx, 20)};
    }

    CppGenerator gen;
    gen.set_context(&ctx);
    gen.set_output_file_idx(cppIdx);

    (void)gen.generate(program.get());
    gen.finalize_output(ctx, cppIdx);

    auto mappings = ctx.toReader()->getTrustFileMappings(ReaderFile::from(trustIdx));
    ASSERT_GE(mappings.size(), 1);

    EXPECT_EQ(
        ctx.getText(MapperRange{MapperLocation::fromPacked(mappings[0].from.begin.asPacked()), MapperLocation::fromPacked(mappings[0].from.end.asPacked())}),
        "x = 7");
    EXPECT_EQ(ctx.getText(MapperRange{MapperLocation::fromPacked(mappings[0].to.begin.asPacked()), MapperLocation::fromPacked(mappings[0].to.end.asPacked())}),
              "x = 7;\n");
}

TEST(GenCppMappingTest, ExprStmtMapping) {
    auto arg = std::make_unique<VarRef>("x");
    auto call = std::make_unique<CallExpr>("print", make_exprs(std::move(arg)));
    call->source = TokenInfo::make(ParserToken::Kind::CallExpr, "print x");
    call->source->text = "print x";

    auto exprStmt = std::make_unique<ExprStmt>(std::move(call));
    exprStmt->source = TokenInfo::make(ParserToken::Kind::ExprStmt, "print x;");
    exprStmt->source->text = "print x;";

    BlockBody body = make_block_body(std::move(exprStmt));
    auto block = std::make_unique<BlockStmt>(std::move(body));

    auto func = std::make_unique<FuncDecl>("foo", TypeKind::Void, std::vector<std::unique_ptr<ParamDecl>>{}, std::move(block));
    func->return_type = TypeInfo(TypeKind::Void, "void");

    auto program = std::make_unique<Program>(make_decls(std::move(func)));

    Context ctx(".");
    MapperFile trustIdx = ctx.add_source("test.trust", "func foo() { print x; }");
    MapperFile cppIdx = ctx.add_output("test.cpp");

    auto* funcDecl = static_cast<FuncDecl*>(program->items[0].get());
    if (funcDecl->body && !funcDecl->body->body.empty()) {
        funcDecl->body->body[0]->source->range = MapperRange{MapperLocation::makeLoc(trustIdx, 14), MapperLocation::makeLoc(trustIdx, 22)};
    }

    CppGenerator gen;
    gen.set_context(&ctx);
    gen.set_output_file_idx(cppIdx);

    (void)gen.generate(program.get());
    gen.finalize_output(ctx, cppIdx);

    auto mappings = ctx.toReader()->getTrustFileMappings(ReaderFile::from(trustIdx));
    ASSERT_GE(mappings.size(), 1);

    EXPECT_EQ(
        ctx.getText(MapperRange{MapperLocation::fromPacked(mappings[0].from.begin.asPacked()), MapperLocation::fromPacked(mappings[0].from.end.asPacked())}),
        "print x;");
    EXPECT_EQ(ctx.getText(MapperRange{MapperLocation::fromPacked(mappings[0].to.begin.asPacked()), MapperLocation::fromPacked(mappings[0].to.end.asPacked())}),
              "print(x);\n");
}

TEST(GenCppMappingTest, NestedStatementsMapping) {
    // Строим дерево:
    //   FuncDecl foo() -> int {
    //       IfStmt(1) { ReturnStmt(2); }
    //       ReturnStmt(0);
    //   }
    // Это проверяет, что вложенные вызовы map_node_begin/map_node_end
    // не дублируют маппинг и не теряют синхронизацию.

    auto retInner = std::make_unique<ReturnStmt>(std::make_unique<IntLiteral>(2));
    retInner->source = TokenInfo::make(ParserToken::Kind::ReturnStmt, "return 2");
    retInner->source->text = "return 2";

    auto cond = std::make_unique<IntLiteral>(1);
    BlockBody thenBody = make_block_body(std::move(retInner));
    auto ifStmt = std::make_unique<IfStmt>(std::move(cond), std::move(thenBody), nullptr, nullptr);
    ifStmt->source = TokenInfo::make(ParserToken::Kind::IfStmt, "if (true) { return 2; }");
    ifStmt->source->text = "if (true) { return 2; }";

    auto retOuter = std::make_unique<ReturnStmt>(std::make_unique<IntLiteral>(0));
    retOuter->source = TokenInfo::make(ParserToken::Kind::ReturnStmt, "return 0");
    retOuter->source->text = "return 0";

    BlockBody blockBody = make_block_body(std::move(ifStmt), std::move(retOuter));
    auto block = std::make_unique<BlockStmt>(std::move(blockBody));

    auto func = std::make_unique<FuncDecl>("foo", TypeKind::Int32, std::vector<std::unique_ptr<ParamDecl>>{}, std::move(block));
    func->return_type = TypeInfo(TypeKind::Int32, "int");

    auto program = std::make_unique<Program>(make_decls(std::move(func)));

    Context ctx(".");
    MapperFile trustIdx = ctx.add_source("test.trust", "func foo() -> int { if (true) { return 2; } return 0; }");
    MapperFile cppIdx = ctx.add_output("test.cpp");

    // trust source (0-based):
    //  "func foo() -> int { if (true) { return 2; } return 0; }"
    //   0         1         2         3         4         5
    //   01234567890123456789012345678901234567890123456789012345678
    //
    //   if (true) { return 2; }   -> substr(20, 24)  -> 1-based [21, 45)
    //   return 2;                 -> substr(32, 9)   -> 1-based [33, 42)
    //   return 0;                 -> substr(44, 9)   -> 1-based [45, 54)

    auto* funcDecl = static_cast<FuncDecl*>(program->items[0].get());
    ASSERT_TRUE(funcDecl->body != nullptr);
    ASSERT_EQ(funcDecl->body->body.size(), 2);

    // IfStmt
    funcDecl->body->body[0]->source->range = MapperRange{MapperLocation::makeLoc(trustIdx, 21), MapperLocation::makeLoc(trustIdx, 45)};

    // ReturnStmt внутри IfStmt
    auto* ifStmtPtr = static_cast<IfStmt*>(funcDecl->body->body[0].get());
    if (!ifStmtPtr->then_body.empty())
        ifStmtPtr->then_body[0]->source->range = MapperRange{MapperLocation::makeLoc(trustIdx, 33), MapperLocation::makeLoc(trustIdx, 42)};

    // ReturnStmt после if
    funcDecl->body->body[1]->source->range = MapperRange{MapperLocation::makeLoc(trustIdx, 45), MapperLocation::makeLoc(trustIdx, 54)};

    CppGenerator gen;
    gen.set_context(&ctx);
    gen.set_output_file_idx(cppIdx);

    (void)gen.generate(program.get());
    gen.finalize_output(ctx, cppIdx);

    auto mappings = ctx.toReader()->getTrustFileMappings(ReaderFile::from(trustIdx));
    // IfStmt не маппится в CppGenerator (visit_if_stmt не вызывает map_node_begin/end),
    // только дочерние ReturnStmt: retInner и retOuter — итого 2 маппинга
    ASSERT_EQ(mappings.size(), 2);

    // Проверяем, что trust-диапазоны корректны
    EXPECT_EQ(
        ctx.getText(MapperRange{MapperLocation::fromPacked(mappings[0].from.begin.asPacked()), MapperLocation::fromPacked(mappings[0].from.end.asPacked())}),
        "return 2;");
    EXPECT_EQ(
        ctx.getText(MapperRange{MapperLocation::fromPacked(mappings[1].from.begin.asPacked()), MapperLocation::fromPacked(mappings[1].from.end.asPacked())}),
        "return 0;");

    // Проверяем, что cpp-диапазоны корректны и идут по порядку
    auto text1 =
        ctx.getText(MapperRange{MapperLocation::fromPacked(mappings[0].to.begin.asPacked()), MapperLocation::fromPacked(mappings[0].to.end.asPacked())});
    auto text2 =
        ctx.getText(MapperRange{MapperLocation::fromPacked(mappings[1].to.begin.asPacked()), MapperLocation::fromPacked(mappings[1].to.end.asPacked())});

    // retInner — "return 2;\n" (внутри IfStmt отступ сбрасывается)
    EXPECT_EQ(text1, "return 2;\n");
    // retOuter — "return 0;\n" (после IfStmt отступ тоже не сохраняется)
    EXPECT_EQ(text2, "return 0;\n");
}
