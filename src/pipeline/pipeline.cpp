#include "pipeline/pipeline.hpp"
#include "pipeline/options.h"
#include "gencpp/ast_builder.hpp"
#include "gencpp/cpp_generator.hpp"
#include "parser/lexer.hpp"
#include "parser/mmproc.hpp"
#include "parser.tab.hh"
#include "ast/token_info.hpp"

#include <cstdlib>
#include <filesystem>
#include <fstream>
#include <iostream>
#include <string>
#include <variant>

namespace trust {

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

    // Lex → MMProcess → Parse → Build Program → Generate C++
    auto lexemes = Lexer::tokenize(ctx, src_idx);
    auto tokens = MMProcessor::process(ctx, lexemes);

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

    // Lex → MMProcess → Parse → Build Program → Generate C++
    auto lexemes = Lexer::tokenize(ctx, src_idx);
    auto tokens = MMProcessor::process(ctx, lexemes);

    std::size_t pos = 0;
    TokenSequence ast_nodes;
    trust::ParserContext pc(tokens, pos, ast_nodes, ctx);
    trust::ParserAST parser(pc);
    parser.parse();

    return 0;
}

} // namespace trust