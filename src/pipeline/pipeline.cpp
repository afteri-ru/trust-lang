#include "pipeline/pipeline.hpp"
#include "pipeline/options.h"
#include "parser/lexer.hpp"
#include "parser/mmproc.hpp"
#include "parser.tab.hh"
#include "ast/token_info.hpp"

#include <cstdlib>
#include <filesystem>
#include <fstream>
#include <iostream>
#include <memory>
#include <string>
#include <variant>

namespace trust {

// ── Helper: setup DSL macros ──

static std::shared_ptr<MacroTable> setupDsl(Context& ctx, const PipelineOpts& opts) {
    auto macros = std::make_shared<MacroTable>();

    if (opts.dsl_disabled) {
        std::cerr << "info: default DSL macros disabled\n";
        return macros;
    }

    if (!opts.dsl_file.empty()) {
        std::ifstream ifs(opts.dsl_file);
        if (!ifs) {
            std::cerr << "error: cannot open DSL file: " << opts.dsl_file << "\n";
            return nullptr;
        }
        std::string dsl_content((std::istreambuf_iterator<char>(ifs)), std::istreambuf_iterator<char>());
        MMProcessor::compileFromSource(ctx, *macros, dsl_content);
        if (opts.verbose)
            std::cerr << "info: loaded DSL from " << opts.dsl_file << "\n";
    } else {
        MMProcessor::compileFromSource(ctx, *macros, getDefaultDslSrc());
        if (opts.verbose)
            std::cerr << "info: using embedded default DSL\n";
    }

    return macros;
}

// ── Pipeline methods ──

int Pipeline::main(int argc, char* argv[], char* envp[]) {
    (void)envp;

    auto result = parse_args(argc, argv);

    // Help/version/errors — выходим сразу
    if (is_special_exit(result)) {
        return result.exit_code;
    }

    // Validate input file
    if (!std::filesystem::exists(result.opts.input_file)) {
        std::cerr << "error: input file not found: " << result.opts.input_file << "\n";
        return 1;
    }

    Context ctx;
    ctx.diag().setOutput(&std::cerr);
    ctx.diag().setMinSeverity(result.opts.quiet ? Severity::Error : Severity::Remark);

    if (result.opts.should_compile()) {
        return run_compile(result, ctx);
    }

    return run_emit(result, ctx);
}

int Pipeline::run_emit(const ParseResult& result, Context& ctx) {
    auto src_idx = ctx.load_file(result.opts.input_file);

    if (result.opts.verbose) {
        std::cerr << "info: loaded " << result.opts.input_file << "\n";
    }

    // Lex
    auto lexemes = Lexer::tokenize(ctx, src_idx);

    if ((result.opts.emit_flags & EmitFlags::LexemesOnly) != EmitFlags::None) {
        for (const auto& lex : lexemes) {
            std::cout << static_cast<std::string_view>(lex) << "\t" << ParserToken::name(lex.kind) << "\n";
        }
        return 0;
    }

    // Compile default macros
    auto macros = setupDsl(ctx, result.opts);
    if (!macros)
        return 1;

    // Lex → MMProcess → Parse → Build Program → Generate C++
    auto tokens = MMProcessor::process(ctx, lexemes, macros);

    if ((result.opts.emit_flags & EmitFlags::Macros) != EmitFlags::None) {
        for (const auto& tok : tokens) {
            std::cout << tok->text << "\t" << ParserToken::name(tok->kind) << "\n";
        }
        return 0;
    }

    std::size_t pos = 0;

    (void)pos;

    return 0;
}

enum class CompileMode {
    Executable,
    ObjectFile,
    StaticLib,
    SharedLib,
};

int Pipeline::run_compile(const ParseResult& result, Context& ctx) {
    auto src_idx = ctx.load_file(result.opts.input_file);

    if (result.opts.verbose) {
        std::cerr << "info: loaded " << result.opts.input_file << "\n";
    }

    // Compile default macros
    auto macros = setupDsl(ctx, result.opts);
    if (!macros)
        return 1;

    // Lex → MMProcess → Parse → Build Program → Generate C++
    auto lexemes = Lexer::tokenize(ctx, src_idx);
    auto tokens = MMProcessor::process(ctx, lexemes, macros);

    std::size_t pos = 0;
    TokenSequence ast_nodes;
    trust::ParserContext pc(tokens, pos, ast_nodes, ctx);
    trust::ParserAST parser(pc);
    parser.parse();

    return 0;
}

} // namespace trust
