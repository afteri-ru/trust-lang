#include "pipeline/pipeline.hpp"
#include "diag/context.hpp"
#include "diag/diag.hpp"
#include "types/registry.hpp"
#include "gtest/gtest.h"

#include <string>

using namespace trust;

// При allow_semantic_on_errors=true анализатор работает на частичном AST даже при наличии
// ошибок лексера/парсера, и PipelineResult.symbols собирается (для LSP).
TEST(PipelineSymbol, AllowSemanticOnErrorsCollectsSymbols) {
    Context ctx;
    TypeRegistry reg(ctx.diag(), ctx.opts());
    ctx.setTypes(&reg);
    ctx.opts().set_enabled(FlagKind::Symbols, true);

    PipelineOpts opts;
    opts.allow_semantic_on_errors = true;
    Pipeline pipeline(ctx, opts);

    // Валидная переменная + мягкая ошибка парсера (пустая последовательность макроса).
    MapperFile src = ctx.source().add_source("test.src", "x : Int32 := 42;\n@@@@ @@@@");
    PipelineResult result = pipeline.runPipeline(PipelineSteps::ParseAST | PipelineSteps::Semantic, src);

    EXPECT_GT(ctx.diag().errorCount(), 0);   // мягкая ошибка парсера зафиксирована
    ASSERT_TRUE(result.symbols.has_value()); // символы собраны несмотря на ошибку
    bool found = false;
    for (const auto& si : *result.symbols) {
        if (si.name == "x") {
            found = true;
            EXPECT_EQ(si.typeName, "Int32");
            EXPECT_FALSE(si.nameRange.isInvalid()); // диапазон имени в исходнике
        }
    }
    EXPECT_TRUE(found);
}

// Без флага при ошибке парсера семантика не запускается - символов нет.
TEST(PipelineSymbol, DefaultSkipsSemanticOnErrors) {
    Context ctx;
    TypeRegistry reg(ctx.diag(), ctx.opts());
    ctx.setTypes(&reg);
    ctx.opts().set_enabled(FlagKind::Symbols, true);

    PipelineOpts opts; // allow_semantic_on_errors = false (по умолчанию)
    Pipeline pipeline(ctx, opts);

    MapperFile src = ctx.source().add_source("test.src", "x : Int32 := 42;\n@@@@ @@@@");
    PipelineResult result = pipeline.runPipeline(PipelineSteps::ParseAST | PipelineSteps::Semantic, src);

    EXPECT_GT(ctx.diag().errorCount(), 0);
    EXPECT_FALSE(result.symbols.has_value()); // семантика не запускалась
}

// Макроопределения записываются в SymbolIndex (isMacro=true) и не теряются после парсинга
// модуля (в отличие от таблицы макросов Macro, очищаемой при PopScope).
TEST(PipelineSymbol, CollectsMacroNames) {
    Context ctx;
    TypeRegistry reg(ctx.diag(), ctx.opts());
    ctx.setTypes(&reg);
    ctx.opts().set_enabled(FlagKind::Symbols, true);

    PipelineOpts opts;
    Pipeline pipeline(ctx, opts);

    MapperFile src = ctx.source().add_source("test.src", "@@ greet @@ ::= 42; x := 1;");
    PipelineResult result = pipeline.runPipeline(PipelineSteps::ParseAST | PipelineSteps::Semantic, src);

    ASSERT_TRUE(result.symbols.has_value());
    bool foundMacro = false;
    for (const auto& si : *result.symbols) {
        if (si.isMacro && si.name.find("greet") != std::string::npos) {
            foundMacro = true;
        }
    }
    EXPECT_TRUE(foundMacro) << "macro 'greet' should be recorded in SymbolIndex";
}

// Документирующий комментарий (///) перед объявлением привязывается к символу.
TEST(PipelineSymbol, AttachesDocCommentToSymbol) {
    Context ctx;
    TypeRegistry reg(ctx.diag(), ctx.opts());
    ctx.setTypes(&reg);
    ctx.opts().set_enabled(FlagKind::Symbols, true);

    PipelineOpts opts;
    Pipeline pipeline(ctx, opts);

    MapperFile src = ctx.source().add_source("test.src", "/// документ для x\nx : Int32 := 42;\n");
    PipelineResult result = pipeline.runPipeline(PipelineSteps::ParseAST | PipelineSteps::Semantic, src);

    ASSERT_TRUE(result.symbols.has_value());
    bool found = false;
    for (const auto& si : *result.symbols) {
        if (si.name == "x") {
            found = true;
            EXPECT_NE(si.documentation.find("документ"), std::string::npos) << "x documentation: '" << si.documentation << "'";
        }
    }
    EXPECT_TRUE(found);
}

// Хвостовой документирующий комментарий (`///<` на той же строке) цепляется к объявлению,
// а НЕ к следующему за ним (раньше `moduleDocMap` привязывал trailing-док к следующему decl).
TEST(PipelineSymbol, AttachesTrailingDocCommentToSymbol) {
    Context ctx;
    TypeRegistry reg(ctx.diag(), ctx.opts());
    ctx.setTypes(&reg);
    ctx.opts().set_enabled(FlagKind::Symbols, true);

    PipelineOpts opts;
    Pipeline pipeline(ctx, opts);

    // `y` идёт ПОСЛЕ trailing-дока `x`: док должен попасть к `x`, а не к `y`.
    MapperFile src = ctx.source().add_source("test.src", "x : Int32 := 42 ///< документ для x\ny : Int32 := 1;\n");
    PipelineResult result = pipeline.runPipeline(PipelineSteps::ParseAST | PipelineSteps::Semantic, src);

    ASSERT_TRUE(result.symbols.has_value());
    const SymbolInfo* xs = nullptr;
    const SymbolInfo* ys = nullptr;
    for (const auto& si : *result.symbols) {
        if (si.name == "x") {
            xs = &si;
        }
        if (si.name == "y") {
            ys = &si;
        }
    }
    ASSERT_NE(xs, nullptr) << "symbol 'x' should be present";
    ASSERT_NE(ys, nullptr) << "symbol 'y' should be present";
    EXPECT_NE(xs->documentation.find("документ"), std::string::npos) << "x documentation: '" << xs->documentation << "'";
    EXPECT_TRUE(ys->documentation.empty()) << "y must NOT inherit x's trailing doc, got: '" << ys->documentation << "'";
}
