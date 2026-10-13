#include "pipeline/pipeline.hpp"
#include "session/context.hpp"
#include "diag/diag.hpp"
#include "types/type_system.hpp"
#include "gtest/gtest.h"

#include "test_data.hpp"

#include <filesystem>
#include <fstream>
#include <format>
#include <optional>
#include <string>

using namespace trust;

// При allow_semantic_on_errors=true анализатор работает на частичном AST даже при наличии
// ошибок лексера/парсера, и PipelineResult.symbols собирается (для LSP).
TEST(PipelineSymbol, AllowSemanticOnErrorsCollectsSymbols) {
    Context ctx;
    TypeSystem reg(ctx.diag(), ctx.opts());
    ctx.setTypes(&reg);

    PipelineOpts opts;
    opts.allow_semantic_on_errors = true;
    Pipeline pipeline(ctx, opts);

    // Валидная переменная + мягкая ошибка парсера (пустая последовательность макроса).
    MapperFile src = ctx.source().add_source("test.src", "x : Int32 := 42;\n@@@@ @@@@@");
    PipelineResult result = pipeline.run(RunRequest::analysis(), src);

    EXPECT_GT(ctx.diag().errorCount(), 0);     // мягкая ошибка парсера зафиксирована
    ASSERT_TRUE(result.semantics.has_value()); // символы собраны несмотря на ошибку
    bool found = false;
    for (const auto& si : result.semantics->symbols()) {
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
    TypeSystem reg(ctx.diag(), ctx.opts());
    ctx.setTypes(&reg);

    PipelineOpts opts; // allow_semantic_on_errors = false (по умолчанию)
    Pipeline pipeline(ctx, opts);

    MapperFile src = ctx.source().add_source("test.src", "x : Int32 := 42;\n@@@@ @@@@@");
    PipelineResult result = pipeline.run(RunRequest::analysis(), src);

    EXPECT_GT(ctx.diag().errorCount(), 0);
    EXPECT_FALSE(result.semantics.has_value()); // семантика не запускалась
}

// Макроопределения записываются в SymbolIndex (isMacro=true) и не теряются после парсинга
// модуля (в отличие от таблицы макросов Macro, очищаемой при PopScope).
TEST(PipelineSymbol, CollectsMacroNames) {
    Context ctx;
    TypeSystem reg(ctx.diag(), ctx.opts());
    ctx.setTypes(&reg);

    PipelineOpts opts;
    Pipeline pipeline(ctx, opts);

    MapperFile src = ctx.source().add_source("test.src", "@@ greet @@ 42 @@@@; x := 1;");
    PipelineResult result = pipeline.run(RunRequest::analysis(), src);

    ASSERT_TRUE(result.semantics.has_value());
    bool foundMacro = false;
    for (const auto& si : result.semantics->symbols()) {
        if (si.isMacro && si.name.find("greet") != std::string::npos) {
            foundMacro = true;
        }
    }
    EXPECT_TRUE(foundMacro) << "macro 'greet' should be recorded in SymbolIndex";
}

// Документирующий комментарий (///) перед объявлением привязывается к символу.
TEST(PipelineSymbol, AttachesDocCommentToSymbol) {
    Context ctx;
    TypeSystem reg(ctx.diag(), ctx.opts());
    ctx.setTypes(&reg);

    PipelineOpts opts;
    Pipeline pipeline(ctx, opts);

    MapperFile src = ctx.source().add_source("test.src", "/// документ для x\nx : Int32 := 42;\n");
    PipelineResult result = pipeline.run(RunRequest::analysis(), src);

    ASSERT_TRUE(result.semantics.has_value());
    bool found = false;
    for (const auto& si : result.semantics->symbols()) {
        if (si.name == "x") {
            found = true;
            EXPECT_NE(si.documentation.find("документ"), std::string::npos) << "x documentation: '" << si.documentation << "'";
        }
    }
    EXPECT_TRUE(found);
}

// Логические имена загруженных модулей проецируются в результат: загрузчик уничтожается вместе
// с Pipeline, а LSP строит по этим именам квалифицированные ссылки (`\lib::f`).
TEST(PipelineSymbol, ProjectsModuleLogicalNames) {
    const std::filesystem::path dir = trust::test::makeTestDataDir("pipeline_module_names");
    const std::string libPath = (dir / "lib.src").string();
    const std::string mainPath = (dir / "main.src").string();
    {
        std::ofstream lib(libPath);
        lib << "\\lib;\nf(a:Int32):Int32 := { @return a; };\n";
        std::ofstream main(mainPath);
        main << "\\lib(f);\ng(a:Int32):Int32 := { @return f(a); };\n";
    }

    Context ctx;
    TypeSystem reg(ctx.diag(), ctx.opts());
    ctx.setTypes(&reg);

    PipelineOpts opts;
    opts.input_file = mainPath;
    Pipeline pipeline(ctx, opts);

    MapperFile src = ctx.source().add_source(mainPath, "\\lib(f);\ng(a:Int32):Int32 := { @return f(a); };\n");
    PipelineResult result = pipeline.run(RunRequest{}, src);

    ASSERT_EQ(ctx.diag().errorCount(), 0) << "unexpected diagnostics: " << ctx.diag().diagnostics().size();
    ASSERT_TRUE(result.semantics.has_value());
    ASSERT_EQ(result.semantics->modules().moduleNames.size(), 2u);                            // главный модуль + \lib
    EXPECT_EQ(result.semantics->modules().moduleNames[result.mainModuleIndex.value], "main"); // headerless root → stem
    bool foundLib = false;
    for (const std::string& name : result.semantics->modules().moduleNames) {
        foundLib = foundLib || name == "\\lib";
    }
    EXPECT_TRUE(foundLib) << "imported module spelling must be projected";
}

// S3: имя-алиас (`f :- \lib::g;`) попадает в SymbolIndex как объявление МОДУЛЯ-АЛИАСА:
// имя/диапазон/документация — узла алиаса, набор перегрузок/модуль C++-имени — цели.
TEST(PipelineSymbol, RecordsFunctionAliasDeclaration) {
    const std::filesystem::path dir = trust::test::makeTestDataDir("pipeline_function_alias");
    {
        std::ofstream(dir / "lib.src") << "\\lib;\ng(x:Int32):Int32 := { @return x; };\n";
        std::ofstream(dir / "facade.src") << "\\facade {\n    \\lib(g);\n    f :- \\lib::g;\n};\n";
        std::ofstream(dir / "main.src") << "\\facade(f);\nh(a:Int32):Int32 := { @return f(a); };\n";
    }
    const std::string mainPath = (dir / "main.src").string();

    Context ctx;
    TypeSystem reg(ctx.diag(), ctx.opts());
    ctx.setTypes(&reg);

    PipelineOpts opts;
    opts.input_file = mainPath;
    Pipeline pipeline(ctx, opts);

    MapperFile src = ctx.source().add_source(mainPath, "\\facade(f);\nh(a:Int32):Int32 := { @return f(a); };\n");
    PipelineResult result = pipeline.run(RunRequest{}, src);

    ASSERT_EQ(ctx.diag().errorCount(), 0);
    ASSERT_TRUE(result.semantics.has_value());
    const SymbolInfo* alias = nullptr;
    for (const auto& si : result.semantics->symbols()) {
        if (!si.isMacro && si.name == "f") {
            alias = &si;
        }
    }
    ASSERT_NE(alias, nullptr) << "function alias name must be projected into SymbolIndex";
    ASSERT_TRUE(alias->module.has_value());
    ASSERT_LT(alias->module->value, result.semantics->modules().moduleNames.size());
    EXPECT_EQ(result.semantics->modules().moduleNames[alias->module->value], "\\facade") << "alias declaration belongs to the facade module";
    EXPECT_FALSE(alias->nameRange.isInvalid()) << "alias name range must point at the facade alias declaration";
}

// Хвостовой документирующий комментарий (`///<` на той же строке) цепляется к объявлению,
// а НЕ к следующему за ним (раньше `moduleDocMap` привязывал trailing-док к следующему decl).
TEST(PipelineSymbol, AttachesTrailingDocCommentToSymbol) {
    Context ctx;
    TypeSystem reg(ctx.diag(), ctx.opts());
    ctx.setTypes(&reg);

    PipelineOpts opts;
    Pipeline pipeline(ctx, opts);

    // `y` идёт ПОСЛЕ trailing-дока `x`: док должен попасть к `x`, а не к `y`.
    MapperFile src = ctx.source().add_source("test.src", "x : Int32 := 42 ///< документ для x\ny : Int32 := 1;\n");
    PipelineResult result = pipeline.run(RunRequest::analysis(), src);

    ASSERT_TRUE(result.semantics.has_value());
    const SymbolInfo* xs = nullptr;
    const SymbolInfo* ys = nullptr;
    for (const auto& si : result.semantics->symbols()) {
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

// S6: проекция ссылок имя→объявление (значение и ТИП) по материализованным фактам семантики.
TEST(PipelineSymbol, ProjectsSymbolReferences) {
    const std::string path = (trust::test::makeTestDataDir("pipeline_references") / "refs.src").string();
    const std::string text = ":T ::= :Int32;\nx : T := 1;\ny := x + 1;\n";
    {
        std::ofstream(path) << text;
    }

    Context ctx;
    TypeSystem reg(ctx.diag(), ctx.opts());
    ctx.setTypes(&reg);

    PipelineOpts opts;
    opts.input_file = path;
    Pipeline pipeline(ctx, opts);

    MapperFile src = ctx.source().add_source(path, text);
    PipelineResult result = pipeline.run(RunRequest::analysis(), src);

    ASSERT_EQ(ctx.diag().errorCount(), 0);
    ASSERT_TRUE(result.semantics.has_value());
    ASSERT_FALSE(result.semantics->references().empty());
    bool valueRef = false;
    bool typeRef = false;
    for (const SymbolReference& ref : result.semantics->references()) {
        const int declLine = static_cast<int>(ctx.source().line_column(ref.decl.begin).line) - 1;
        if (declLine == 1) { // `x : T := 1;` — значение x
            valueRef = true;
        }
        if (declLine == 0) { // `:T ::= :Int32;` — имя типа T
            typeRef = true;
        }
        EXPECT_EQ(ref.ref.begin.fileIdx().as_index(), ref.decl.begin.fileIdx().as_index()) << "single-file reference target";
    }
    EXPECT_TRUE(valueRef) << "value reference must resolve to x";
    EXPECT_TRUE(typeRef) << "type reference must resolve to :T declaration";
}

// S6: ссылка в чужой модуль проецируется с целью в файле модуля.
TEST(PipelineSymbol, ProjectsImportedReference) {
    const std::filesystem::path dir = trust::test::makeTestDataDir("pipeline_imported_reference");
    {
        std::ofstream(dir / "lib.src") << "\\lib;\nlf():Int32 := { @return 1; };\n";
        std::ofstream(dir / "main.src") << "\\lib(lf);\ny := \\lib::lf();\n";
    }
    const std::string mainPath = (dir / "main.src").string();
    const std::string text = "\\lib(lf);\ny := \\lib::lf();\n";

    Context ctx;
    TypeSystem reg(ctx.diag(), ctx.opts());
    ctx.setTypes(&reg);

    PipelineOpts opts;
    opts.input_file = mainPath;
    Pipeline pipeline(ctx, opts);

    MapperFile src = ctx.source().add_source(mainPath, text);
    MapperFile cpp = ctx.source().add_output("out.cppt");
    PipelineResult result = pipeline.run(RunRequest::withCpp(cpp), src);
    ASSERT_EQ(ctx.diag().errorCount(), 0);
    ASSERT_TRUE(result.semantics.has_value());
    ASSERT_FALSE(result.semantics->references().empty());
    bool crossed = false;
    std::string dump;
    for (const SymbolReference& ref : result.semantics->references()) {
        dump += std::format("ref[f{}:{}..{}] -> decl[f{}:{}]\n", ref.ref.begin.fileIdx().as_index(), ref.ref.begin.offset(), ref.ref.end.offset(),
                            ref.decl.begin.fileIdx().as_index(), ref.decl.begin.offset());
        if (ref.ref.begin.fileIdx().as_index() != ref.decl.begin.fileIdx().as_index()) {
            crossed = true;
        }
    }
    const trust::SourceMapReader* reader = ctx.source().toReader();
    ASSERT_NE(reader, nullptr);
    const auto loc = reader->lspToLocation(trust::ReaderFile::from(src), 1, 12);
    std::optional<trust::MapperRange> target;
    for (const SymbolReference& ref : result.semantics->references()) {
        if (ref.ref.begin.fileIdx().as_index() != loc.fileIdx().as_index()) {
            continue;
        }
        if (loc.offset() >= ref.ref.begin.offset() && loc.offset() <= ref.ref.end.offset()) {
            target = ref.decl;
        }
    }
    EXPECT_TRUE(target.has_value()) << "cursor lookup failed; line1 char12 loc=" << loc.offset() << "\n" << dump;
    EXPECT_TRUE(crossed) << dump;
}
