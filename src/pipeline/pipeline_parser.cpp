#include "pipeline/pipeline_parser.hpp"
#include "pipeline/options.h"
#include "trust/version.h"

#include <CLI/CLI.hpp>

#include <iostream>

namespace trust {

// ── Callbacks ──

static void set_emit_flag(PipelineOpts& opts, EmitFlags flag) {
    opts.emit_flags = opts.emit_flags | flag;
}

// ── parse_args ──

ParseResult parse_args(int argc, char* argv[]) {
    ParseResult result;

    // Set default compiler from CMake config
    result.opts.compiler = TRUST_DEFAULT_COMPILER;

    CLI::App app{"trust — Trust language transpiler pipeline"};
    app.allow_extras();
    app.allow_windows_style_options(false);
    app.set_help_flag("");        // отключаем стандартный --help
    app.set_version_flag("", ""); // отключаем стандартный --version
    app.require_subcommand(0, 0);

    // ── Позиционный аргумент: входной файл ──
    // (без ExistingFile проверки — она выполняется в pipeline.cpp)
    app.add_option("input", result.opts.input_file, "Input source file")->type_name("file");

    // ── Флаги ──
    app.add_flag("-h,--help", result.opts.help_requested, "Show this help message");
    app.add_flag("--version", result.opts.version_requested, "Show version information");
    app.add_flag("-v,--verbose", result.opts.verbose, "Verbose output");
    app.add_flag("-q,--quiet", result.opts.quiet, "Quiet mode (suppress warnings)");
    app.add_flag("--emit-tokens", [&](int64_t) { set_emit_flag(result.opts, EmitFlags::Tokens); }, "Print tokens after lexing");
    app.add_flag("--emit-ast", [&](int64_t) { set_emit_flag(result.opts, EmitFlags::AST); }, "Print AST after parsing");
    app.add_flag("--emit-cpp", [&](int64_t) { set_emit_flag(result.opts, EmitFlags::Cpp); }, "Emit C++ source file");
    app.add_flag("--emit-module", [&](int64_t) { set_emit_flag(result.opts, EmitFlags::Module); }, "Emit C++ module file");
    app.add_flag("-c", result.opts.compile_to_object, "Compile to object file (.o)");
    app.add_flag("-a,--static-lib", result.opts.compile_to_static_lib, "Compile to static library (.a)");
    app.add_flag("-l,--shared-lib", result.opts.compile_to_shared_lib, "Compile to shared library (.so)");

    // ── Опции с аргументом ──
    app.add_option("-o,--output", result.opts.output_file, "Output file (default: stdout)")->type_name("file");

    app.add_option("--temp-dir", result.opts.temp_dir, "Temporary directory for intermediate files")->type_name("dir");

    app.add_option("--compiler", result.opts.compiler, "Compiler path")->type_name("path");

    app.add_option("--options", result.opts.compiler_options, "Additional compiler options (quoted)")->type_name("opts");

    // ── -b: флаг (без значения) ──
    app.add_flag(
        "-b",
        [&](int64_t) {
            result.opts.gen_binding_header = true;
            result.opts.binding_header_explicitly_set = true;
        },
        "Generate binding header file");

    // ── --binding-header[=FILE]: опциональное значение ──
    // (только длинная форма, чтобы избежать захвата позиционных аргументов)
    auto* binding_opt = app.add_option("--binding-header", result.opts.binding_header_file, "Generate binding header file (optional: --binding-header=FILE)");
    binding_opt->expected(0, 1);
    binding_opt->type_name("[FILE]");

    // --no-binding-header
    app.add_flag(
        "--no-binding-header",
        [&](int64_t) {
            result.opts.gen_binding_header = false;
            result.opts.binding_header_explicitly_set = true;
        },
        "Disable binding header generation");

    // ── Парсинг ──
    try {
        app.parse(argc, argv);
    } catch (const CLI::ParseError& e) {
        result.exit_code = static_cast<int>(e.get_exit_code());
        if (result.exit_code != 0) {
            std::cerr << "error: " << e.what() << "\n";
        }
        return result;
    }

    // ── Дополнительная обработка после парсинга ──

    // --help / --version
    if (result.opts.help_requested) {
        std::cout << app.help();
        return result;
    }
    if (result.opts.version_requested) {
        std::cout << "trust " << TRUST_VERSION << "\n";
        return result;
    }

    // Проверка входного файла
    if (result.opts.input_file.empty()) {
        std::cerr << "error: no input file specified\n";
        std::cerr << app.help();
        result.exit_code = 1;
        return result;
    }

    // Оставшиеся аргументы (неизвестные опции)
    result.remaining_args = app.remaining();

    // --binding-header (с опциональным значением)
    if (binding_opt->count() > 0) {
        result.opts.gen_binding_header = true;
        result.opts.binding_header_explicitly_set = true;
    }

    // Auto-enable binding header for library builds (only if not explicitly disabled)
    if (result.opts.compile_to_static_lib || result.opts.compile_to_shared_lib) {
        if (!result.opts.binding_header_explicitly_set || result.opts.gen_binding_header) {
            result.opts.gen_binding_header = true;
        }
    }

    // Warn about compile options being used with emit flags
    if (!result.opts.should_compile()) {
        if (!result.opts.temp_dir.empty())
            std::cerr << "warning: --temp-dir is ignored when using emit flags\n";
        if (!result.opts.compiler_options.empty())
            std::cerr << "warning: --options is ignored when using emit flags\n";
        if (result.opts.compile_to_object)
            std::cerr << "warning: -c is ignored when using emit flags\n";
        if (result.opts.compile_to_static_lib)
            std::cerr << "warning: -a is ignored when using emit flags\n";
        if (result.opts.compile_to_shared_lib)
            std::cerr << "warning: -l is ignored when using emit flags\n";
    }

    return result;
}

ParseResult parse_args(std::span<char*> argv) {
    if (argv.empty())
        return {};
    return parse_args(static_cast<int>(argv.size()), argv.data());
}

} // namespace trust
