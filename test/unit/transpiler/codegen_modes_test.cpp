#include "transpiler/transpiler_test_fixture.hpp"

namespace trust {

namespace {

/// `x := 0; x += 3;` — контролируемая арифметика (`Binary::m_overflowCheck` ставит семантика).
std::vector<AstNodePtr> compoundAddAst(Context& ctx) {
    MapperFile input_file = ctx.source().add_source("test.src", "x := 0; x += 3;", true);
    MapperRange range(ctx.source().makeLoc(input_file, 9), ctx.source().makeLoc(input_file, 16));
    auto opTerm = Term::Create(TermID::ASSIGN, "+=", range, parser::token_type::END);
    auto b = std::make_shared<Binary>(ParserToken::Kind::AssignOp, std::move(opTerm));
    b->m_left = std::make_shared<IdentName>("x");
    b->m_right = std::make_shared<Literal>(ParserToken::Kind::IntLiteral, "3");

    std::vector<AstNodePtr> seq;
    auto xTerm =
        Term::Create(TermID::NAME, "x", MapperRange(ctx.source().makeLoc(input_file, 1), ctx.source().makeLoc(input_file, 8)), parser::token_type::NAME);
    seq.push_back(std::make_shared<VarDecl>(std::move(xTerm), nullptr, std::make_shared<Literal>(ParserToken::Kind::IntLiteral, "0")));
    seq.push_back(std::move(b));
    return seq;
}

} // namespace

/// Поведенческие режимы кодогена — ДАННЫЕ, разрешённые ОДИН раз (`codegenModes`) в точке сборки
/// прогона (R39). Кодоген не читает `diag::Options`: смена опций после разрешения режимов не
/// меняет вывод.
TEST_F(TranspilerTest, ModesAreResolvedData_NotLiveOptions) {
    std::vector<AstNodePtr> seq = compoundAddAst(m_ctx);
    SemanticPassRunner runner(m_ctx, *m_types);
    ASSERT_TRUE(runner.run(seq));

    const analysis::BehavioralModes resolved = codegenModes({}, m_ctx.opts());
    EXPECT_TRUE(resolved.overflowCheck);
    EXPECT_TRUE(resolved.comments);
    EXPECT_TRUE(resolved.backtrace);
    // Опция выключается ПОСЛЕ разрешения режимов: кодоген обязан остаться с прежним значением.
    m_ctx.opts().set_enabled(transpiler::FlagKind::OverflowCheck, false);

    MapperFile out_idx = m_ctx.source().add_output("test_modes_checked.cpp", true);
    ASSERT_FALSE(out_idx.isInvalid());
    CppTranspiler gen(codegenServices(m_ctx), resolved);
    gen.generateToFile(hirOf(seq), out_idx);

    const std::string text = m_ctx.source().output_result(out_idx);
    EXPECT_TRUE(text.find("__builtin_add_overflow") != std::string::npos) << "result: " << text;
}

/// Режимы, разрешённые ПОСЛЕ выключения флага, дают арифметику без проверки — тот же вход.
TEST_F(TranspilerTest, ModesFollowOptionsAtResolution) {
    std::vector<AstNodePtr> seq = compoundAddAst(m_ctx);
    SemanticPassRunner runner(m_ctx, *m_types);
    ASSERT_TRUE(runner.run(seq));

    m_ctx.opts().set_enabled(transpiler::FlagKind::OverflowCheck, false);
    const analysis::BehavioralModes resolved = codegenModes({}, m_ctx.opts());
    EXPECT_FALSE(resolved.overflowCheck);

    MapperFile out_idx = m_ctx.source().add_output("test_modes_plain.cpp", true);
    ASSERT_FALSE(out_idx.isInvalid());
    CppTranspiler gen(codegenServices(m_ctx), resolved);
    gen.generateToFile(hirOf(seq), out_idx);

    const std::string text = m_ctx.source().output_result(out_idx);
    EXPECT_TRUE(text.find("__builtin_add_overflow") == std::string::npos) << "result: " << text;
}

} // namespace trust