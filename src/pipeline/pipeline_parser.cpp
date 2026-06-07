#include "pipeline/pipeline.hpp"
#include "trust/version.h"
#include "utils/io.hpp"

#include <CLI/CLI.hpp>
#include "utils/io.hpp"

#include <filesystem>
#include <iostream>
#include "utils/io.hpp"

namespace trust {

// ── Callbacks ──

static void set_emit_flag(PipelineOpts& opts, EmitFlags flag) {
    opts.emit_flags = opts.emit_flags | flag;
}

// ── Pipeline::parseArgs ──

ParseResult Pipeline::parseArgs(std::span<char*> argv) {
    ParseResult result;

    if (argv.empty())
        return result;

    // Set default compiler from CMake config
    result.opts.compiler = TRUST_DEFAULT_COMPILER;

    CLI::App app{"trust — Trust language transpiler pipeline"};
    app.allow_extras();
    app.allow_windows_style_options(false);
    app.set_help_flag("");        // отключаем стандартный --help
    app.set_version_flag("", ""); // отключаем стандартный --version
    app.require_subcommand(0, 0);

    // ── Позиционный аргумент: входной файл ──
    app.add_option("input", result.opts.input_file, "Input source file")->type_name("file");

    // ── Флаги ──
    app.add_flag("-h,--help", result.opts.help_requested, "Show this help message");
    app.add_flag("--version", result.opts.version_requested, "Show version information");
    app.add_flag("-v,--verbose", result.opts.verbose, "Verbose output");
    app.add_flag("-q,--quiet", result.opts.quiet, "Quiet mode (suppress warnings)");
    app.add_flag("--emit-tokens", [&](int64_t) { set_emit_flag(result.opts, EmitFlags::Tokens); }, "Print tokens after lexing");
    app.add_flag("--emit-ast", [&](int64_t) { set_emit_flag(result.opts, EmitFlags::AST); }, "Print AST after parsing");
    app.add_flag("--emit-cpp", [&](int64_t) { set_emit_flag(result.opts, EmitFlags::Cpp); }, "Emit C++ source file");
    app.add_flag("--emit-lexemes", [&](int64_t) { set_emit_flag(result.opts, EmitFlags::LexemesOnly); }, "Stop after lexing, output lexemes only");
    app.add_flag("-c", [&](int64_t) { result.opts.compile_mode = CompileMode::ObjectFile; }, "Compile to object file (.o)");
    app.add_flag("-a,--static-lib", [&](int64_t) { result.opts.compile_mode = CompileMode::StaticLib; }, "Compile to static library (.a)");
    app.add_flag("-l,--shared-lib", [&](int64_t) { result.opts.compile_mode = CompileMode::SharedLib; }, "Compile to shared library (.so)");
    app.add_flag("-m,--module", [&](int64_t) { result.opts.compile_mode = CompileMode::TrustModule; }, "Compile to trust module (.trust)");
    app.add_flag("--module-info", result.opts.module_info_requested, "Show exported symbols and version of a .trust module");

    // ── Опции с аргументом ──
    app.add_option("-o,--output", result.opts.output_file, "Output file (default: stdout)")->type_name("file");
    app.add_option("--temp-dir", result.opts.temp_dir, "Temporary directory for intermediate files")->type_name("dir");
    app.add_option("--compiler", result.opts.compiler, "Compiler path")->type_name("path");
    app.add_option("--options", result.opts.compiler_options, "Additional compiler options (quoted)")->type_name("opts");

    // ── Standard library ──
    app.add_flag("--no-stdlib", [&](int64_t) { result.opts.use_stdlib = false; }, "Disable standard library types");

    // ── DSL macros ──
    app.add_option("--dsl", result.opts.dsl_file, "Load DSL macros from file instead of embedded std/dsl.src")->type_name("file");
    app.add_flag("--no-dsl", result.opts.no_dsl, "Disable loading DSL macros");

    // ── Парсинг ──
    try {
        app.parse(static_cast<int>(argv.size()), argv.data());
    } catch (const CLI::ParseError& e) {
        result.exit_code = static_cast<int>(e.get_exit_code());
        if (result.exit_code != 0) {
            trust::errs() << "error: " << e.what() << "\n";
        }
        return result;
    }

    // ── Дополнительная обработка после парсинга ──

    // --help / --version
    if (result.opts.help_requested) {
        trust::outs() << app.help();
        return result;
    }
    if (result.opts.version_requested) {
        trust::outs() << "trust " << TRUST_VERSION_FULL << "\n";
        return result;
    }

    // --module-info: нужен только входной файл, всё остальное игнорируется
    if (result.opts.module_info_requested) {
        if (result.opts.input_file.empty()) {
            trust::errs() << "error: --module-info requires a .trust module file\n";
            result.exit_code = 1;
            return result;
        }
        // Возвращаем результат без дополнительной обработки
        return result;
    }

    // Проверка входного файла (кроме --module-info)
    if (result.opts.input_file.empty()) {
        trust::errs() << "error: no input file specified\n";
        trust::errs() << app.help();
        result.exit_code = 1;
        return result;
    }

    // Валидация взаимоисключающих флагов dsl
    if (result.opts.no_dsl && !result.opts.dsl_file.empty()) {
        trust::errs() << "error: --dsl and --no-dsl are mutually exclusive\n";
        result.exit_code = 1;
        return result;
    }

    // Оставшиеся аргументы (неизвестные опции)
    result.remaining_args = app.remaining();

    // Warn about compile options being used with emit flags
    if (!result.opts.should_compile()) {
        if (!result.opts.temp_dir.empty())
            trust::errs() << "warning: --temp-dir is ignored when using emit flags\n";
        if (!result.opts.compiler_options.empty())
            trust::errs() << "warning: --options is ignored when using emit flags\n";
        if (result.opts.compile_mode != CompileMode::Executable)
            trust::errs() << "warning: -c/-a/-l is ignored when using emit flags\n";
    }

    return result;
}

} // namespace trust