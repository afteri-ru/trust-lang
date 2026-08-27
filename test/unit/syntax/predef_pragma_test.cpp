// Тест полноты реализации предопределённых макросов и прагм.
//
// Единственный источник списков - x-macro TRUST_VALUE_MACROS / TRUST_CONTEXT_MACROS
// (predef_macro_x.hpp) и TRUST_PRAGMA_MACROS (pragma_macro_x.hpp). Из них генерируются enum,
// таблицы имён/описаний и раскрытие. Тест проверяет, что:
//   1) каждый значение-макрос действительно РАСКРЫВАЕТСЯ (не падает в «not implemented»);
//   2) каждый контекст-макрос штампуется транзитным маркером (не падает и не остаётся MACRO);
//   3) каждый предопределённый макрос присутствует в реестре (predefMacroNames);
//   4) каждая прагма присутствует в списке прагм (pragmaMacroNames) и распознаётся pragmaEval
//      (не выводит «Uknown pragma»).

#include "syntax/warning_push.h"
#include <gtest/gtest.h>
#include "syntax/warning_pop.h"

#include "syntax/predef_macro.hpp"
#include "syntax/pragma_evaluator.hpp"
#include "syntax/term.h"
#include "diag/context.hpp"

#include <algorithm>
#include <string>

using namespace trust;

namespace {

// Валидный диапазон в зарегистрированном in-memory исходнике (для макросов, требующих location).
MapperRange makeValidRange(trust::Context& ctx, MapperFile f) {
    return MapperRange(ctx.source().makeLoc(f, 1), ctx.source().makeLoc(f, 2));
}

} // namespace

TEST(PredefPragmaCompleteness, AllPredefMacrosImplemented) {
    trust::Context ctx;
    MapperFile f = ctx.source().add_source("test.src", "x := 1;");
    trust::syntax::PredefMacroResolver resolver(ctx);

    for (int i = 0; i < static_cast<int>(trust::syntax::PredefMacroId::Count); ++i) {
        const auto id = static_cast<trust::syntax::PredefMacroId>(i);
        const std::string name = trust::syntax::predefMacroName(id);
        auto term = Term::Create(TermID::MACRO, name, makeValidRange(ctx, f), parser::token_type::MACRO);
        ctx.diag().clear();
        resolver.expandPredefMacro(term);
        EXPECT_NE(term->m_id, TermID::MACRO) << "predef macro not implemented: " << name;
    }
}

TEST(PredefPragmaCompleteness, AllContextMacrosStamped) {
    // Каждый контекст-макрос (информация анализатора) штампуется транзитным маркером
    // (MACRO_CONTEXT/NAMESPACE/NAME/MODULE) и не остаётся «сырым» MACRO и не даёт «not implemented».
    trust::Context ctx;
    MapperFile f = ctx.source().add_source("test.src", "x := 1;");
    trust::syntax::PredefMacroResolver resolver(ctx);

    for (int i = 0; i < static_cast<int>(trust::syntax::ContextMacroId::Count); ++i) {
        const auto id = static_cast<trust::syntax::ContextMacroId>(i);
        const std::string name = trust::syntax::contextMacroName(id);
        auto term = Term::Create(TermID::MACRO, name, makeValidRange(ctx, f), parser::token_type::MACRO);
        ctx.diag().clear();
        resolver.expandPredefMacro(term);
        EXPECT_NE(term->m_id, TermID::MACRO) << "context macro not stamped: " << name;
    }
}

TEST(PredefPragmaCompleteness, AllContextMacrosInRegistry) {
    const auto& names = trust::syntax::PredefMacroResolver::predefMacroNames();
    for (int i = 0; i < static_cast<int>(trust::syntax::ContextMacroId::Count); ++i) {
        const auto id = static_cast<trust::syntax::ContextMacroId>(i);
        const std::string name = trust::syntax::contextMacroName(id);
        EXPECT_NE(std::find(names.begin(), names.end(), name), names.end()) << "context macro missing from registry: " << name;
    }
}

TEST(PredefPragmaCompleteness, AllPredefMacrosInRegistry) {
    const auto& names = trust::syntax::PredefMacroResolver::predefMacroNames();
    for (int i = 0; i < static_cast<int>(trust::syntax::PredefMacroId::Count); ++i) {
        const auto id = static_cast<trust::syntax::PredefMacroId>(i);
        const std::string name = trust::syntax::predefMacroName(id);
        EXPECT_NE(std::find(names.begin(), names.end(), name), names.end()) << "predef macro missing from registry: " << name;
    }
}

TEST(PredefPragmaCompleteness, AllPragmasInRegistry) {
    const auto& names = trust::syntax::PredefMacroResolver::pragmaMacroNames();
    for (int i = 0; i < static_cast<int>(trust::syntax::PragmaMacroId::Count); ++i) {
        const auto id = static_cast<trust::syntax::PragmaMacroId>(i);
        const std::string name = trust::syntax::pragmaMacroName(id);
        EXPECT_NE(std::find(names.begin(), names.end(), name), names.end()) << "pragma missing from pragmaMacroNames: " << name;
    }
}

TEST(PredefPragmaCompleteness, UnknownPredefMacroReportsDiagnostic) {
    trust::Context ctx;
    MapperFile f = ctx.source().add_source("test.src", "x := 1;");
    trust::syntax::PredefMacroResolver resolver(ctx);

    auto term = Term::Create(TermID::MACRO, "@__UNKNOWN_MACRO__", makeValidRange(ctx, f), parser::token_type::MACRO);
    ctx.diag().clear();
    resolver.expandPredefMacro(term);

    // Токен вида @__...__, но не определён - остаётся макросом и даёт диагностику «not implemented».
    EXPECT_EQ(term->m_id, TermID::MACRO);
    bool found = false;
    for (const auto& d : ctx.diag().diagnostics()) {
        if (d.message.find("not implemented") != std::string::npos) {
            found = true;
            break;
        }
    }
    EXPECT_TRUE(found) << "expected 'not implemented' diagnostic for unknown predef macro";
}

TEST(PredefPragmaCompleteness, PragmaTokenInExpandPredefNoDiagnostic) {
    // Прагма (@__OPTION__ и т.п.) не является преdef-макросом: expandPredefMacro должен
    // оставить её без изменений и БЕЗ диагностики (её обрабатывает PragmaEvaluator).
    trust::Context ctx;
    MapperFile f = ctx.source().add_source("test.src", "x := 1;");
    trust::syntax::PredefMacroResolver resolver(ctx);

    auto term = Term::Create(TermID::MACRO, "@__OPTION__", makeValidRange(ctx, f), parser::token_type::MACRO);
    ctx.diag().clear();
    resolver.expandPredefMacro(term);

    EXPECT_EQ(term->m_id, TermID::MACRO);
    bool hasDiag = !ctx.diag().diagnostics().empty();
    EXPECT_FALSE(hasDiag) << "pragma token must not be flagged as unknown predef macro";
}

TEST(PredefPragmaCompleteness, UnknownPragmaReportsDiagnostic) {
    trust::Context ctx;
    trust::syntax::PredefMacroResolver resolver(ctx);
    trust::syntax::PragmaEvaluator evaluator(ctx, resolver);

    auto term = Term::Create(TermID::NAME, "@__UNKNOWN_PRAGMA__", {}, parser::token_type::NAME);
    SequenceType buffer;
    ctx.diag().clear();
    evaluator.pragmaEval(term, buffer);

    bool found = false;
    for (const auto& d : ctx.diag().diagnostics()) {
        if (d.message.find("Uknown pragma") != std::string::npos) {
            found = true;
            break;
        }
    }
    EXPECT_TRUE(found) << "expected 'Uknown pragma' diagnostic for unknown pragma";
}

TEST(PredefPragmaCompleteness, UnsupportedPragmaInEmbedReportsDiagnostic) {
    // Прагмы, не транслируемые в C++ (@__PRAGMA_MESSAGE__, @__OPTION__), внутри {% %} - ошибка.
    trust::Context ctx;
    MapperFile f = ctx.source().add_source("test.src", "x := 1;");
    trust::syntax::PredefMacroResolver resolver(ctx);

    std::string text = "@__PRAGMA_MESSAGE__";
    ctx.diag().clear();
    resolver.expandEmbedPredefMacros(text, makeValidRange(ctx, f));

    bool found = false;
    for (const auto& d : ctx.diag().diagnostics()) {
        if (d.message.find("cannot be used inside") != std::string::npos) {
            found = true;
            break;
        }
    }
    EXPECT_TRUE(found) << "expected diagnostic for unsupported pragma inside {% %}";
}

TEST(PredefPragmaCompleteness, AllPragmasHandled) {
    trust::Context ctx;
    MapperFile f = ctx.source().add_source("test.src", "x := 1;");
    trust::syntax::PredefMacroResolver resolver(ctx);
    trust::syntax::PragmaEvaluator evaluator(ctx, resolver);

    for (int i = 0; i < static_cast<int>(trust::syntax::PragmaMacroId::Count); ++i) {
        const auto id = static_cast<trust::syntax::PragmaMacroId>(i);
        const std::string name = trust::syntax::pragmaMacroName(id);
        ctx.diag().clear();

        // Контекст-макросу @__MODULE_NAME__ нужен валидный диапазон (moduleName) - даём его всем.
        auto term = Term::Create(TermID::NAME, name, makeValidRange(ctx, f), parser::token_type::NAME);
        SequenceType buffer;
        evaluator.pragmaEval(term, buffer);

        bool unknown = false;
        for (const auto& d : ctx.diag().diagnostics()) {
            if (d.message.find("Uknown pragma") != std::string::npos) {
                unknown = true;
                break;
            }
        }
        EXPECT_FALSE(unknown) << "pragma not recognized by pragmaEval: " << name;
    }
}
