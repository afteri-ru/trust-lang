// Тесты встроенных системных макросов отладочного вывода (УРОВЕНЬ 1):
//   - @__DEBUG__(...) / @__DEBUG_SCOPE__(...) захватываются PragmaEvaluator::evalDebug
//     (аргументы -> терм-маркер MACRO_CONTEXT);
//   - term_to_ast строит узел DebugStmt (mode/args);
//   - невалидная арность/отсутствие скобок -> диагностика, маркер не создаётся.
// В release-сборке компилятора (TRUST_TRACE_ENABLED=0) отладочный вывод недоступен: вызовы
// стираются и выдаётся информационное предупреждение - ветка проверки стоит прямо в теле каждого
// теста под `#if TRUST_TRACE_ENABLED`.

#include "syntax/pragma_evaluator.hpp"

#include "ast/ast_nodes.hpp"
#include "ast/term_to_ast.hpp"
#include "session/context.hpp"
#include "syntax/predef_macro.hpp"
#include "syntax/term.h"
#include "syntax/term_types.h"
#include "utils/trace.hpp"

#include <gtest/gtest.h>

#include <memory>
#include <string>

namespace {

using token_type = trust::parser::token_type;
using trust::TermID;

trust::MapperRange makeRange(trust::Context& ctx, trust::MapperFile f) {
    return trust::MapperRange(ctx.source().makeLoc(f, 1), ctx.source().makeLoc(f, 2));
}

// «Сырой» буфер вызова макроса: <имя> ( <tok> , <tok> ... ).
struct RawCall {
    trust::SequenceType buf;

    void add(const TermID id, const std::string& text, const trust::MapperRange& rng, const token_type tt) {
        buf.push_back(trust::Term::Create(id, text, rng, tt));
    }
};

bool hasDiag(const trust::Context& ctx, const std::string& needle) {
    for (const auto& d : ctx.diag().diagnostics()) {
        if (d.message.find(needle) != std::string::npos) {
            return true;
        }
    }
    return false;
}

} // namespace

TEST(DebugMacroEval, CapturesFilterMasks) {
    trust::Context ctx;
    const trust::MapperFile f = ctx.source().add_source("t.src", "@__DEBUG__(\"scope*\");");
    trust::syntax::PredefMacroResolver resolver(ctx);
    trust::syntax::PragmaEvaluator evaluator(ctx, resolver);
    const trust::MapperRange rng = makeRange(ctx, f);

    RawCall call;
    call.add(TermID::MACRO, "@__DEBUG__", rng, token_type::MACRO);
    call.add(TermID::NAME, "(", rng, token_type::NAME);
    call.add(TermID::STRCHAR, "\"scope*\"", rng, token_type::STRCHAR);
    call.add(TermID::NAME, ")", rng, token_type::NAME);

    EXPECT_TRUE(evaluator.evalDebug(call.buf));
#if TRUST_TRACE_ENABLED
    // Dev: макрос захватывается в терм-маркер с аргументами.
    ASSERT_EQ(call.buf.size(), 1u);
    EXPECT_EQ(call.buf[0]->getText(), "@__DEBUG__");
    ASSERT_EQ(call.buf[0]->m_sequence.size(), 1u);
    EXPECT_EQ(call.buf[0]->m_sequence[0]->getText(), "scope*");
#else
    // Release (TRUST_TRACE_ENABLED=0): вызов стирается, вместо вывода - информационное предупреждение.
    EXPECT_TRUE(call.buf.empty()) << "release build: the debug call must be erased";
    EXPECT_TRUE(hasDiag(ctx, "debug output is not available in a release build"));
#endif
}

TEST(DebugMacroEval, EmptyArgsDisable) {
    trust::Context ctx;
    const trust::MapperFile f = ctx.source().add_source("t.src", "@__DEBUG__();");
    trust::syntax::PredefMacroResolver resolver(ctx);
    trust::syntax::PragmaEvaluator evaluator(ctx, resolver);
    const trust::MapperRange rng = makeRange(ctx, f);

    RawCall call;
    call.add(TermID::MACRO, "@__DEBUG__", rng, token_type::MACRO);
    call.add(TermID::NAME, "(", rng, token_type::NAME);
    call.add(TermID::NAME, ")", rng, token_type::NAME);

    EXPECT_TRUE(evaluator.evalDebug(call.buf));
#if TRUST_TRACE_ENABLED
    // Dev: пустой вызов = выключение фильтра сообщений (маркер без аргументов).
    ASSERT_EQ(call.buf.size(), 1u);
    EXPECT_TRUE(call.buf[0]->m_sequence.empty());
#else
    // Release (TRUST_TRACE_ENABLED=0): вызов стирается, вместо вывода - информационное предупреждение.
    EXPECT_TRUE(call.buf.empty()) << "release build: the debug call must be erased";
    EXPECT_TRUE(hasDiag(ctx, "debug output is not available in a release build"));
#endif
}

TEST(DebugMacroEval, IgnoresOtherMacros) {
    trust::Context ctx;
    trust::syntax::PredefMacroResolver resolver(ctx);
    trust::syntax::PragmaEvaluator evaluator(ctx, resolver);

    RawCall call;
    call.add(TermID::NAME, "@other", {}, token_type::NAME);
    EXPECT_FALSE(evaluator.evalDebug(call.buf));
}

TEST(DebugMacroEval, ScopeCapturesNameMasksAndNamedOptions) {
    trust::Context ctx;
    const trust::MapperFile f = ctx.source().add_source("t.src", "@__DEBUG_SCOPE__(\"x*\", level=all);");
    trust::syntax::PredefMacroResolver resolver(ctx);
    trust::syntax::PragmaEvaluator evaluator(ctx, resolver);
    const trust::MapperRange rng = makeRange(ctx, f);

    // Реальный поток токенов: позиционные МАСКИ ИМЁН, затем именованные опции «key = value»
    // (значения без кавычек).
    RawCall call;
    call.add(TermID::MACRO, "@__DEBUG_SCOPE__", rng, token_type::MACRO);
    call.add(TermID::NAME, "(", rng, token_type::NAME);
    call.add(TermID::STRCHAR, "\"x*\"", rng, token_type::STRCHAR);
    call.add(TermID::NAME, ",", rng, token_type::NAME);
    call.add(TermID::NAME, "level", rng, token_type::NAME);
    call.add(TermID::NAME, "=", rng, token_type::NAME);
    call.add(TermID::NAME, "all", rng, token_type::NAME);
    call.add(TermID::NAME, ")", rng, token_type::NAME);

    EXPECT_TRUE(evaluator.evalDebug(call.buf));
#if TRUST_TRACE_ENABLED
    // Dev: маски имён и именованные опции захватываются в маркер.
    ASSERT_EQ(call.buf.size(), 1u);
    EXPECT_EQ(call.buf[0]->getText(), "@__DEBUG_SCOPE__");
    ASSERT_EQ(call.buf[0]->m_sequence.size(), 2u);
    EXPECT_EQ(call.buf[0]->m_sequence[0]->getText(), "x*");
    EXPECT_EQ(call.buf[0]->m_sequence[1]->getText(), "level=all");
#else
    // Release (TRUST_TRACE_ENABLED=0): вызов стирается, вместо вывода - информационное предупреждение.
    EXPECT_TRUE(call.buf.empty()) << "release build: the debug call must be erased";
    EXPECT_TRUE(hasDiag(ctx, "debug output is not available in a release build"));
#endif
}

TEST(DebugMacroEval, ScopeEmptyArgsMeansFullDump) {
    trust::Context ctx;
    const trust::MapperFile f = ctx.source().add_source("t.src", "@__DEBUG_SCOPE__();");
    trust::syntax::PredefMacroResolver resolver(ctx);
    trust::syntax::PragmaEvaluator evaluator(ctx, resolver);
    const trust::MapperRange rng = makeRange(ctx, f);

    RawCall call;
    call.add(TermID::MACRO, "@__DEBUG_SCOPE__", rng, token_type::MACRO);
    call.add(TermID::NAME, "(", rng, token_type::NAME);
    call.add(TermID::NAME, ")", rng, token_type::NAME);

    EXPECT_TRUE(evaluator.evalDebug(call.buf));
#if TRUST_TRACE_ENABLED
    // Dev: пустой вызов = полный дамп (маркер без аргументов).
    ASSERT_EQ(call.buf.size(), 1u);
    EXPECT_TRUE(call.buf[0]->m_sequence.empty()) << "empty call = full dump (no name filter)";
#else
    // Release (TRUST_TRACE_ENABLED=0): вызов стирается, вместо вывода - информационное предупреждение.
    EXPECT_TRUE(call.buf.empty()) << "release build: the debug call must be erased";
    EXPECT_TRUE(hasDiag(ctx, "debug output is not available in a release build"));
#endif
}

TEST(DebugMacroEval, ScopeOptionsWithoutMasksAreValid) {
    trust::Context ctx;
    const trust::MapperFile f = ctx.source().add_source("t.src", "@__DEBUG_SCOPE__(level=all);");
    trust::syntax::PredefMacroResolver resolver(ctx);
    trust::syntax::PragmaEvaluator evaluator(ctx, resolver);
    const trust::MapperRange rng = makeRange(ctx, f);

    RawCall call;
    call.add(TermID::MACRO, "@__DEBUG_SCOPE__", rng, token_type::MACRO);
    call.add(TermID::NAME, "(", rng, token_type::NAME);
    call.add(TermID::NAME, "level", rng, token_type::NAME);
    call.add(TermID::NAME, "=", rng, token_type::NAME);
    call.add(TermID::NAME, "all", rng, token_type::NAME);
    call.add(TermID::NAME, ")", rng, token_type::NAME);

    ctx.diag().clear();
    EXPECT_TRUE(evaluator.evalDebug(call.buf));
#if TRUST_TRACE_ENABLED
    // Dev: маски имён необязательны - только именованные опции, без диагностик.
    EXPECT_TRUE(ctx.diag().diagnostics().empty()) << "маски имён необязательны";
    ASSERT_EQ(call.buf.size(), 1u);
    ASSERT_EQ(call.buf[0]->m_sequence.size(), 1u);
    EXPECT_EQ(call.buf[0]->m_sequence[0]->getText(), "level=all");
#else
    // Release (TRUST_TRACE_ENABLED=0): вызов стирается, вместо вывода - информационное предупреждение.
    EXPECT_TRUE(call.buf.empty()) << "release build: the debug call must be erased";
    EXPECT_TRUE(hasDiag(ctx, "debug output is not available in a release build"));
#endif
}

TEST(DebugMacroEval, ScopeRejectsMaskAfterNamedOption) {
    trust::Context ctx;
    const trust::MapperFile f = ctx.source().add_source("t.src", "@__DEBUG_SCOPE__(level=all, \"x*\");");
    trust::syntax::PredefMacroResolver resolver(ctx);
    trust::syntax::PragmaEvaluator evaluator(ctx, resolver);
    const trust::MapperRange rng = makeRange(ctx, f);

    RawCall call;
    call.add(TermID::MACRO, "@__DEBUG_SCOPE__", rng, token_type::MACRO);
    call.add(TermID::NAME, "(", rng, token_type::NAME);
    call.add(TermID::NAME, "level", rng, token_type::NAME);
    call.add(TermID::NAME, "=", rng, token_type::NAME);
    call.add(TermID::NAME, "all", rng, token_type::NAME);
    call.add(TermID::NAME, ",", rng, token_type::NAME);
    call.add(TermID::STRCHAR, "\"x*\"", rng, token_type::STRCHAR);
    call.add(TermID::NAME, ")", rng, token_type::NAME);

    ctx.diag().clear();
    EXPECT_TRUE(evaluator.evalDebug(call.buf));
#if TRUST_TRACE_ENABLED
    // Dev: маска после именованной опции - ошибка, вызов стёрт.
    EXPECT_TRUE(call.buf.empty());
    EXPECT_TRUE(hasDiag(ctx, "must precede named options"));
#else
    // Release (TRUST_TRACE_ENABLED=0): вызов стирается, вместо вывода - информационное предупреждение.
    EXPECT_TRUE(call.buf.empty()) << "release build: the debug call must be erased";
    EXPECT_TRUE(hasDiag(ctx, "debug output is not available in a release build"));
#endif
}

TEST(DebugMacroEval, ScopeRejectsFilterAfterNamedOption) {
    trust::Context ctx;
    const trust::MapperFile f = ctx.source().add_source("t.src", "@__DEBUG_SCOPE__(level=all, \"scope\");");
    trust::syntax::PredefMacroResolver resolver(ctx);
    trust::syntax::PragmaEvaluator evaluator(ctx, resolver);
    const trust::MapperRange rng = makeRange(ctx, f);

    RawCall call;
    call.add(TermID::MACRO, "@__DEBUG_SCOPE__", rng, token_type::MACRO);
    call.add(TermID::NAME, "(", rng, token_type::NAME);
    call.add(TermID::NAME, "level", rng, token_type::NAME);
    call.add(TermID::NAME, "=", rng, token_type::NAME);
    call.add(TermID::NAME, "all", rng, token_type::NAME);
    call.add(TermID::NAME, ",", rng, token_type::NAME);
    call.add(TermID::STRCHAR, "\"scope\"", rng, token_type::STRCHAR);
    call.add(TermID::NAME, ")", rng, token_type::NAME);

    ctx.diag().clear();
    EXPECT_TRUE(evaluator.evalDebug(call.buf));
#if TRUST_TRACE_ENABLED
    // Dev: фильтр после именованной опции - ошибка, вызов стёрт.
    EXPECT_TRUE(call.buf.empty());
    EXPECT_TRUE(hasDiag(ctx, "must precede named options"));
#else
    // Release (TRUST_TRACE_ENABLED=0): вызов стирается, вместо вывода - информационное предупреждение.
    EXPECT_TRUE(call.buf.empty()) << "release build: the debug call must be erased";
    EXPECT_TRUE(hasDiag(ctx, "debug output is not available in a release build"));
#endif
}

TEST(DebugMacroEval, RejectsBadArity) {
    trust::Context ctx;
    const trust::MapperFile f = ctx.source().add_source("t.src", "@__DEBUG__(\"a\", \"b\");");
    trust::syntax::PredefMacroResolver resolver(ctx);
    trust::syntax::PragmaEvaluator evaluator(ctx, resolver);
    const trust::MapperRange rng = makeRange(ctx, f);

    RawCall call;
    call.add(TermID::MACRO, "@__DEBUG__", rng, token_type::MACRO);
    call.add(TermID::NAME, "(", rng, token_type::NAME);
    call.add(TermID::STRCHAR, "\"a\"", rng, token_type::STRCHAR);
    call.add(TermID::NAME, ",", rng, token_type::NAME);
    call.add(TermID::STRCHAR, "\"b\"", rng, token_type::STRCHAR);
    call.add(TermID::NAME, ")", rng, token_type::NAME);

    ctx.diag().clear();
    EXPECT_TRUE(evaluator.evalDebug(call.buf));
#if TRUST_TRACE_ENABLED
    // Dev: неверная арность - ошибка, вызов стёрт.
    EXPECT_TRUE(call.buf.empty()) << "call must be erased on error";
    EXPECT_TRUE(hasDiag(ctx, "expects arguments"));
#else
    // Release (TRUST_TRACE_ENABLED=0): вызов стирается, вместо вывода - информационное предупреждение.
    EXPECT_TRUE(call.buf.empty()) << "release build: the debug call must be erased";
    EXPECT_TRUE(hasDiag(ctx, "debug output is not available in a release build"));
#endif
}

TEST(DebugMacroEval, RejectsMissingParens) {
    trust::Context ctx;
    const trust::MapperFile f = ctx.source().add_source("t.src", "@__DEBUG_SCOPE__;");
    trust::syntax::PredefMacroResolver resolver(ctx);
    trust::syntax::PragmaEvaluator evaluator(ctx, resolver);
    const trust::MapperRange rng = makeRange(ctx, f);

    RawCall call;
    call.add(TermID::MACRO, "@__DEBUG_SCOPE__", rng, token_type::MACRO);

    ctx.diag().clear();
    EXPECT_TRUE(evaluator.evalDebug(call.buf));
    EXPECT_TRUE(call.buf.empty());
    EXPECT_TRUE(hasDiag(ctx, "expects arguments"));
}

TEST(DebugMacroEval, ScopeRejectsUnknownOptionWithUsageList) {
    trust::Context ctx;
    const trust::MapperFile f = ctx.source().add_source("t.src", "@__DEBUG_SCOPE__(\"scope\", \"bogus=1\");");
    trust::syntax::PredefMacroResolver resolver(ctx);
    trust::syntax::PragmaEvaluator evaluator(ctx, resolver);
    const trust::MapperRange rng = makeRange(ctx, f);

    RawCall call;
    call.add(TermID::MACRO, "@__DEBUG_SCOPE__", rng, token_type::MACRO);
    call.add(TermID::NAME, "(", rng, token_type::NAME);
    call.add(TermID::STRCHAR, "\"scope\"", rng, token_type::STRCHAR);
    call.add(TermID::NAME, ",", rng, token_type::NAME);
    call.add(TermID::NAME, "bogus", rng, token_type::NAME);
    call.add(TermID::NAME, "=", rng, token_type::NAME);
    call.add(TermID::NAME, "1", rng, token_type::NAME);
    call.add(TermID::NAME, ")", rng, token_type::NAME);

    ctx.diag().clear();
    EXPECT_TRUE(evaluator.evalDebug(call.buf));
#if TRUST_TRACE_ENABLED
    // Dev: неизвестная опция - ошибка с полным списком поддерживаемых опций, вызов стёрт.
    EXPECT_TRUE(call.buf.empty());
    EXPECT_TRUE(hasDiag(ctx, "unknown option"));
    EXPECT_TRUE(hasDiag(ctx, "level=current|all")) << "must list all supported options";
#else
    // Release (TRUST_TRACE_ENABLED=0): вызов стирается, вместо вывода - информационное предупреждение.
    EXPECT_TRUE(call.buf.empty()) << "release build: the debug call must be erased";
    EXPECT_TRUE(hasDiag(ctx, "debug output is not available in a release build"));
#endif
}

TEST(DebugMacroEval, ScopeRejectsBadOptionValue) {
    trust::Context ctx;
    const trust::MapperFile f = ctx.source().add_source("t.src", "@__DEBUG_SCOPE__(\"scope\", \"level=deep\");");
    trust::syntax::PredefMacroResolver resolver(ctx);
    trust::syntax::PragmaEvaluator evaluator(ctx, resolver);
    const trust::MapperRange rng = makeRange(ctx, f);

    RawCall call;
    call.add(TermID::MACRO, "@__DEBUG_SCOPE__", rng, token_type::MACRO);
    call.add(TermID::NAME, "(", rng, token_type::NAME);
    call.add(TermID::STRCHAR, "\"scope\"", rng, token_type::STRCHAR);
    call.add(TermID::NAME, ",", rng, token_type::NAME);
    call.add(TermID::NAME, "level", rng, token_type::NAME);
    call.add(TermID::NAME, "=", rng, token_type::NAME);
    call.add(TermID::NAME, "deep", rng, token_type::NAME);
    call.add(TermID::NAME, ")", rng, token_type::NAME);

    ctx.diag().clear();
    EXPECT_TRUE(evaluator.evalDebug(call.buf));
#if TRUST_TRACE_ENABLED
    // Dev: неверное значение опции - ошибка, вызов стёрт.
    EXPECT_TRUE(call.buf.empty());
    EXPECT_TRUE(hasDiag(ctx, "current|all"));
    EXPECT_TRUE(hasDiag(ctx, "supported options"));
#else
    // Release (TRUST_TRACE_ENABLED=0): вызов стирается, вместо вывода - информационное предупреждение.
    EXPECT_TRUE(call.buf.empty()) << "release build: the debug call must be erased";
    EXPECT_TRUE(hasDiag(ctx, "debug output is not available in a release build"));
#endif
}

TEST(DebugMacroAst, MarkerBecomesDebugStmt) {
    trust::Context ctx;
    const trust::MapperFile f = ctx.source().add_source("t.src", "x := 1;");
    const trust::MapperRange rng = makeRange(ctx, f);

    trust::TermPtr marker = trust::Term::Create(TermID::MACRO_CONTEXT, "@__DEBUG_SCOPE__", rng, token_type::MACRO_CONTEXT);
    marker->m_sequence.push_back(trust::Term::Create(TermID::NAME, "scope", rng, token_type::NAME));
    marker->m_sequence.push_back(trust::Term::Create(TermID::NAME, "level=all", rng, token_type::NAME));

    trust::TermToAstConverter conv(ctx);
    trust::AstNodePtr node = conv.convert(marker);
    ASSERT_NE(node, nullptr);
    ASSERT_EQ(node->kind(), trust::ParserToken::Kind::DebugStmt);
    auto dbg = std::static_pointer_cast<trust::DebugStmt>(node);
    EXPECT_EQ(dbg->mode, trust::DebugMode::Scope);
    ASSERT_EQ(dbg->args.size(), 2u);
    EXPECT_EQ(dbg->args[0], "scope");
    EXPECT_EQ(dbg->args[1], "level=all");
}
