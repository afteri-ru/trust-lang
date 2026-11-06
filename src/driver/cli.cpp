#include "driver/cli.hpp"
#include "diag/flag_values.hpp"

#include "diag/options.hpp"
#include "driver/options.hpp"
#include "utils/utils.hpp"

#include <cstdlib>

namespace trust {

// id передаются в общий parseDriverArgs как int; таблица - buildTrustTable().
enum class DriverOptId {
    Help,
    Version,
    Verbose,
    Quiet,
    Input,
    Output,
    TempDir,
    ModuleRoot,
    ModulePath,
    PackagePath,
    EmitTokens,
    EmitAst,
    EmitCpp,
    EmitLexemes,
    EmitMacros,
    EmitModuleDeps,
    Comments,
    OverflowCheck,
    ErrorLimit,
    DiagnosticsFormat,
    SourceMap,
    SingleFile,
    ObjectFile,
    StaticLib,
    SharedLib,
    Run,
    LinkLib,
    LinkDir,
    LinkRuntime,
    Compiler,
    CompilerOptions,
    NoStdlib,
    Dsl,
    NoDsl,
    SemanticOnErrors,
    SolverMode,
    SolverLoopUnroll,
    StackCheck,
    StackCheckReserve,
    StackCheckFunctions,
    Verify,
    NoopBackend,
    SyncDeadlock,
    Format,
    FormatCheck,
    FormatDumpConfig,
    Keywords,
    KeywordSigil,
    FormatConfig,
    FormatStyle,
    CompleteOptions,
    CompleteFiles,
    Package,
    Extract,
};

namespace {

// Единое связывание DriverOptId -> PipelineOpts; false при невалидном значении флага
// (parseDriverArgs -> «invalid value»). Новая опция = строка в buildTrustTable + ветка здесь.
bool applyOption(DriverOptId id, const std::string& value, PipelineOpts& opts) {
    // Каноничная форма ФАКТИЧЕСКИ заданной codegen-опции (кеш --run); порядок нормализуется
    // сортировкой.
    auto note = [&opts](std::string s) { opts.codegen_args.push_back(std::move(s)); };
    switch (id) {
    case DriverOptId::Help:
        opts.help_requested = true;
        break;
    case DriverOptId::Version:
        opts.version_requested = true;
        break;
    case DriverOptId::Verbose:
        opts.verbose = true;
        break;
    case DriverOptId::Quiet:
        opts.quiet = true;
        break;
    case DriverOptId::Output:
        opts.output_file = value;
        break;
    case DriverOptId::TempDir:
        opts.temp_dir = value;
        break;
    case DriverOptId::ModuleRoot:
        opts.module_root = value;
        note("--module-root=" + value);
        break;
    case DriverOptId::ModulePath:
        opts.module_paths.push_back(value);
        note("-I" + value);
        break;
    case DriverOptId::PackagePath:
        opts.package_paths.push_back(value);
        note("--package-path=" + value);
        break;
    case DriverOptId::EmitTokens:
        opts.emit_flags = opts.emit_flags | EmitFlags::Tokens;
        break;
    case DriverOptId::EmitAst:
        opts.emit_flags = opts.emit_flags | EmitFlags::AST;
        break;
    case DriverOptId::EmitCpp:
        opts.emit_flags = opts.emit_flags | EmitFlags::Cpp;
        break;
    case DriverOptId::EmitLexemes:
        opts.emit_flags = opts.emit_flags | EmitFlags::LexemesOnly;
        break;
    case DriverOptId::EmitMacros:
        opts.emit_flags = opts.emit_flags | EmitFlags::Macros;
        break;
    case DriverOptId::EmitModuleDeps:
        opts.emit_module_deps = true;
        break;
    case DriverOptId::Comments:
        if (const auto v = parseBoolFlagValue(value)) {
            opts.comments = *v;
            note(*v ? "-fcomments" : "-fno-comments");
        } else {
            return false; // no silent fallback: невалидное значение - ошибка
        }
        break;
    case DriverOptId::OverflowCheck:
        if (const auto v = parseBoolFlagValue(value)) {
            opts.overflow_check = *v;
            note(*v ? "-foverflow-check" : "-fno-overflow-check");
        } else {
            return false;
        }
        break;
    case DriverOptId::ErrorLimit:
        if (std::size_t limit = 0; parseSizeValue(value, limit)) {
            opts.error_limit = limit;
        } else {
            return false; // no silent fallback: невалидное значение - ошибка
        }
        break;
    case DriverOptId::DiagnosticsFormat:
        // Сырое значение; валидация - в parseCommandLine (нужен exit_code).
        opts.diagnostics_format = value;
        break;
    case DriverOptId::SourceMap:
        if (const auto v = parseBoolFlagValue(value)) {
            opts.embed_source_map = *v;
            note(*v ? "-fsourcemap" : "-fno-sourcemap");
        } else {
            return false;
        }
        break;
    case DriverOptId::SingleFile:
        if (const auto v = parseBoolFlagValue(value)) {
            opts.single_file = *v;
            note(*v ? "-fsingle-file" : "-fno-single-file");
        } else {
            return false;
        }
        opts.single_file_set = true;
        break;
    case DriverOptId::ObjectFile:
        opts.compile_mode = CompileMode::ObjectFile;
        break;
    case DriverOptId::StaticLib:
        opts.compile_mode = CompileMode::StaticLib;
        break;
    case DriverOptId::SharedLib:
        opts.compile_mode = CompileMode::SharedLib;
        break;
    case DriverOptId::Run:
        opts.compile_mode = CompileMode::Executable;
        opts.run = true;
        break;
    case DriverOptId::LinkLib:
        opts.link_libs_cli.push_back(value);
        note("-l" + value);
        break;
    case DriverOptId::LinkDir:
        opts.link_dirs.push_back(value);
        note("-L" + value);
        break;
    case DriverOptId::LinkRuntime:
        opts.runtime_link = (value == "shared") ? RuntimeLink::Shared : RuntimeLink::Static;
        if (opts.runtime_link == RuntimeLink::Shared) {
            note("--link-runtime=shared");
        }
        break;
    case DriverOptId::NoopBackend:
        opts.noop_backend = true;
        note("--noop-backend");
        break;
    case DriverOptId::Compiler:
        opts.compiler = value;
        note("--compiler=" + value);
        break;
    case DriverOptId::CompilerOptions:
        opts.compiler_options = value;
        note("--options=" + value);
        break;
    case DriverOptId::NoStdlib:
        opts.use_stdlib = false;
        note("--no-stdlib");
        break;
    case DriverOptId::Dsl:
        opts.dsl_file = value;
        note("--dsl=" + value);
        break;
    case DriverOptId::NoDsl:
        opts.no_dsl = true;
        note("-fno-dsl");
        break;
    case DriverOptId::SemanticOnErrors:
        opts.allow_semantic_on_errors = true;
        break;
    case DriverOptId::SolverMode:
        // Сырое значение; валидация - в parseCommandLine (нужен exit_code).
        opts.solver_mode = value;
        note("--solver-mode=" + value);
        break;
    case DriverOptId::SolverLoopUnroll:
        if (const auto v = parseBoolFlagValue(value)) {
            opts.solver_loop_unroll = *v;
            note(*v ? "-fsolver-loop-unroll" : "-fno-solver-loop-unroll");
        } else {
            return false;
        }
        break;
    case DriverOptId::StackCheck:
        // Сырое значение; валидация - в applyBehavioralFlags.
        opts.stack_check_mode = value;
        note("--stack-check=" + value);
        break;
    case DriverOptId::StackCheckReserve:
        // Сырое значение; валидация в applyBehavioralFlags.
        opts.stack_check_reserve = value;
        note("--stack-check-reserve=" + value);
        break;
    case DriverOptId::StackCheckFunctions:
        opts.stack_check_functions = value;
        note("--stack-check-functions=" + value);
        break;
    case DriverOptId::Verify:
        // Сырое значение; валидация - в applyBehavioralFlags (отклоняет всё, кроме all).
        opts.verify = value;
        note("--verify=" + value);
        break;
    case DriverOptId::SyncDeadlock:
        // Сырое значение; встраивается в начало main (trust::runtime::setSyncDeadlockFromString).
        opts.sync_deadlock_timeout = value;
        note("-fsync-deadlock=" + value);
        break;
    case DriverOptId::Format:
        opts.format_requested = true;
        break;
    case DriverOptId::FormatCheck:
        opts.format_requested = true;
        opts.format_check = true;
        break;
    case DriverOptId::Package:
        opts.package_requested = true;
        break;
    case DriverOptId::Extract:
        opts.extract_requested = true;
        break;
    case DriverOptId::FormatDumpConfig:
        opts.format_dump_config = true;
        break;
    case DriverOptId::Keywords:
        opts.keywords = value;
        break;
    case DriverOptId::KeywordSigil:
        opts.keyword_sigil = value;
        break;
    case DriverOptId::FormatConfig:
        opts.format_config = value;
        break;
    case DriverOptId::FormatStyle:
        // none -> не искать .trust-format; иных стилей нет.
        opts.format_no_config = (value == "none");
        break;
    case DriverOptId::CompleteOptions:
        opts.complete_options = true;
        break;
    case DriverOptId::CompleteFiles:
        opts.complete_files = true;
        break;
    default:
        break;
    }
    return true;
}

} // namespace

namespace {
// Таблица опций trust: единый источник для parseDriverArgs и driverHelp.
std::vector<DriverOption> buildTrustTable() {
    std::vector<DriverOption> out = {
        {int(DriverOptId::Help), "help", "h", CliOpt::Flag, "", "Show this help message", CliCategory::General},
        {int(DriverOptId::Version), "version", "", CliOpt::Flag, "", "Show version information", CliCategory::General},
        {int(DriverOptId::Verbose), "verbose", "v", CliOpt::Flag, "", "Verbose output", CliCategory::General},
        {int(DriverOptId::Quiet), "quiet", "q", CliOpt::Flag, "", "Quiet mode (suppress warnings)", CliCategory::General},
        {int(DriverOptId::DiagnosticsFormat), "diagnostics-format", "", CliOpt::Value, "format",
         "Diagnostic output format: text | json | sarif (default: text); json/sarif — machine-readable output for CI/LSP", CliCategory::General},
        {int(DriverOptId::Input), "input", "", CliOpt::Value, "file", "Input source file", CliCategory::InputOutput},
        {int(DriverOptId::Output), "output", "o", CliOpt::Value, "file", "Output file (default: stdout)", CliCategory::InputOutput},
        {int(DriverOptId::TempDir), "temp-dir", "", CliOpt::Value, "dir", "Temporary directory for intermediate files", CliCategory::InputOutput},
        {int(DriverOptId::ModuleRoot), "module-root", "", CliOpt::Value, "dir", "Project root for module paths (default: input file directory)",
         CliCategory::InputOutput},
        {int(DriverOptId::ModulePath), "I", "", CliOpt::ValueList, "dir", "Add directory to the module search path (repeatable)", CliCategory::InputOutput},
        {int(DriverOptId::PackagePath), "package-path", "", CliOpt::ValueList, "dir", "Add directory to the package search path (repeatable)",
         CliCategory::InputOutput},
        {int(DriverOptId::EmitTokens), "emit-tokens", "", CliOpt::Flag, "", "Print tokens after lexing", CliCategory::CompileModel},
        {int(DriverOptId::EmitAst), "emit-ast", "", CliOpt::Flag, "", "Print AST after parsing", CliCategory::CompileModel},
        {int(DriverOptId::EmitCpp), "emit-cpp", "", CliOpt::Flag, "", "Emit C++ source file", CliCategory::CompileModel},
        {int(DriverOptId::EmitLexemes), "emit-lexemes", "", CliOpt::Flag, "", "Stop after lexing, output lexemes only", CliCategory::CompileModel},
        {int(DriverOptId::EmitMacros), "emit-macros", "", CliOpt::Flag, "", "Print macro definitions after parsing", CliCategory::CompileModel},
        {int(DriverOptId::EmitModuleDeps), "emit-module-deps", "", CliOpt::Flag, "", "Print the file dependency graph (make rules: <target>: <deps>)",
         CliCategory::CompileModel},
        {int(DriverOptId::Comments), "comments", "", CliOpt::Flag, "",
         "Emit documentation comments in the generated C++ (default: on). Behavioral flag, not a diagnostic: "
         "-fcomments/-fno-comments",
         CliCategory::CompileModel},
        {int(DriverOptId::OverflowCheck), "overflow-check", "", CliOpt::Flag, "",
         "Detect signed integer overflow in arithmetic (+,-,* and +=,-=,*=) and throw trust::IntMinus at "
         "runtime (default: on). Behavioral flag, not a diagnostic: -foverflow-check/-fno-overflow-check. "
         "Unsigned integers, BigInteger/Rational, '/'/'//' and bitwise ops are not checked.",
         CliCategory::CompileModel},
        {int(DriverOptId::ErrorLimit), "error-limit", "", CliOpt::Value, "N",
         "Maximum number of errors before compilation stops (0 = no limit; default: 20). Behavioral option, "
         "not a diagnostic: -ferror-limit=<N> or --error-limit=<N>",
         CliCategory::CompileModel},
        {int(DriverOptId::SingleFile), "single-file", "", CliOpt::Flag, "",
         "Single-file mode: inline the int main wrapper into the module .cppt (no separate _main.cppt). "
         "Program must have a main entry (@main). Enabled by default for --run. Behavioral flag, not a "
         "diagnostic: -fsingle-file/-fno-single-file",
         CliCategory::CompileModel},

        {int(DriverOptId::SourceMap), "sourcemap", "", CliOpt::Flag, "",
         "Embed source map in the generated C++ (default: on). The .src_map file is always "
         "written; this flag only controls the embedded source-map byte array. Behavioral flag, not a "
         "diagnostic: -fsourcemap/-fno-sourcemap",
         CliCategory::CompileModel},
        {int(DriverOptId::ObjectFile), "c", "", CliOpt::Flag, "", "Compile to object file (.o)", CliCategory::CompileModel},
        {int(DriverOptId::StaticLib), "static-lib", "a", CliOpt::Flag, "", "Compile to static library (.a)", CliCategory::CompileModel},
        {int(DriverOptId::SharedLib), "shared-lib", "", CliOpt::Flag, "", "Compile to shared library (.so)", CliCategory::CompileModel},
        {int(DriverOptId::Run), "run", "", CliOpt::Flag, "", "Build and run the program", CliCategory::CompileModel},
        {int(DriverOptId::Package), "package", "", CliOpt::Flag, "", "Create a .trust source package (tar.gz) from a directory containing trust.toml",
         CliCategory::CompileModel},
        {int(DriverOptId::Extract), "extract", "", CliOpt::Flag, "",
         "Unpack a .trust source package (tar.gz) into a directory (default: <name>/ in cwd; use -o <dir> to override)", CliCategory::CompileModel},
        {int(DriverOptId::LinkLib), "l", "", CliOpt::ValueList, "lib", "Additional library to link (repeatable; merged with @[link(...)])",
         CliCategory::Linking},
        {int(DriverOptId::LinkDir), "L", "", CliOpt::ValueList, "dir", "Add directory to the library search path (repeatable)", CliCategory::Linking},
        {int(DriverOptId::LinkRuntime), "link-runtime", "", CliOpt::Value, "mode", "How to link the trust runtime: static (default) or shared",
         CliCategory::Linking},
        {int(DriverOptId::Compiler), "compiler", "", CliOpt::Value, "path", "Compiler path", CliCategory::Toolchain},
        {int(DriverOptId::NoopBackend), "noop-backend", "", CliOpt::Flag, "", "Internal: run the stub backend stage (R37 pluggability proof)",
         CliCategory::ProjectSpecific},
        {int(DriverOptId::CompilerOptions), "options", "", CliOpt::Value, "opts", "Additional compiler options (quoted)", CliCategory::Toolchain},
        {int(DriverOptId::NoStdlib), "no-stdlib", "", CliOpt::Flag, "", "Disable standard library types", CliCategory::ProjectSpecific},
        {int(DriverOptId::Dsl), "dsl", "", CliOpt::Value, "file", "Load DSL macros from file instead of stdlib/dsl.src from the stdlib package",
         CliCategory::ProjectSpecific},
        {int(DriverOptId::NoDsl), "no-dsl", "", CliOpt::Flag, "", "Disable loading DSL macros", CliCategory::ProjectSpecific},
        {int(DriverOptId::SemanticOnErrors), "semantic-on-errors", "", CliOpt::Flag, "", "Run the semantic analyzer even when the lexer/parser produced errors",
         CliCategory::ProjectSpecific},
        {int(DriverOptId::Format), "format", "", CliOpt::Flag, "", "Format the input Trust source file and print the result to stdout",
         CliCategory::Formatting},
        {int(DriverOptId::FormatCheck), "format-check", "", CliOpt::Flag, "",
         "Check whether the input file is already formatted; exit 0 if so, 1 if it would change", CliCategory::Formatting},
        {int(DriverOptId::FormatDumpConfig), "format-dump-config", "", CliOpt::Flag, "",
         "Print all formatting settings with default values and comments (like clang-format -dump-config)", CliCategory::Formatting},
        {int(DriverOptId::FormatConfig), "format-config", "", CliOpt::Value, "file", "Path to a .trust-format configuration file", CliCategory::Formatting},
        {int(DriverOptId::FormatStyle), "format-style", "", CliOpt::Value, "style",
         "Formatting style: 'file' (default, use .trust-format) or 'none' (defaults only)", CliCategory::Formatting},
        {int(DriverOptId::KeywordSigil), "keyword-sigil", "", CliOpt::Value, "mode",
         "Formatter handling of the '@' sigil on all keywords (macro names): remove | add | none "
         "(default: remove). remove strips '@', add prepends it, none leaves as written",
         CliCategory::Formatting},
        {int(DriverOptId::CompleteOptions), "complete-options", "", CliOpt::Flag, "",
         "Internal: print all option names (--...) and -W diagnostics for shell completion", CliCategory::Formatting},
        {int(DriverOptId::CompleteFiles), "complete-files", "", CliOpt::Flag, "",
         "Internal: print driver options whose value is a file/path (--...) for shell completion", CliCategory::Formatting},
        {int(DriverOptId::SyncDeadlock), "sync-deadlock", "", CliOpt::Value, "time",
         "Compile-time default deadlock-detector timeout for synchronized shared references "
         "(-fsync-deadlock=<ms|s|us|ns|nano>; no suffix = seconds; runtime default 5s). Overridable at "
         "program start by the system option --trust:fsync-deadlock=<ms|s|nano>.",
         CliCategory::ProjectSpecific},
    };
    // Общие опции анализа - ЕДИНЫЙ источник commonAnalysisOptions (cli.hpp); здесь задаются
    // только id из DriverOptId.
    auto common =
        commonAnalysisOptions(int(DriverOptId::SolverMode), int(DriverOptId::Keywords), int(DriverOptId::SolverLoopUnroll), int(DriverOptId::StackCheck),
                              int(DriverOptId::StackCheckReserve), int(DriverOptId::StackCheckFunctions), int(DriverOptId::Verify));
    out.insert(out.end(), common.begin(), common.end());
    return out;
}
} // namespace

// Обёртка trust: возвращает ошибку (пусто = успех); ParseResult заполняется частично даже при
// ошибке. Печать - обязанность вызывающего (trust.cpp/applyAnalysisArgs; LSP сообщает диагностикой).
std::string parseDriverArgs(ParseResult& result, std::span<std::string> args) {
    static const std::vector<DriverOption> table = buildTrustTable();
    auto apply = [&](int id, const std::string& value) -> bool { return applyOption(static_cast<DriverOptId>(id), value, result.opts); };
    std::string err = parseDriverArgs(args, table, apply, result.diag_args, result.opts.input_file, result.diag_help_requested, nullptr, &result.opts.run_args);
    // Лишние позиционные допустимы только с --run.
    if (err.empty() && !result.opts.run && !result.opts.run_args.empty()) {
        return "unexpected extra argument '" + result.opts.run_args.front() + "'";
    }
    return err;
}

std::string driverHelp() {
    static const std::vector<DriverOption> table = buildTrustTable();
    return driverHelp(table);
}

// Имена driver-опций (--<long>, -<short>) для shell-completion (`--complete-options`).
std::vector<std::string> driverOptionTokens() {
    static const std::vector<DriverOption> table = buildTrustTable();
    std::vector<std::string> out;
    out.reserve(table.size() * 2);
    for (const DriverOption& o : table) {
        out.push_back("--" + o.name);
        if (!o.short_name.empty()) {
            out.push_back("-" + o.short_name);
        }
    }
    return out;
}

// Имена driver-опций со значением-файлом/путём (type ∈ {file,dir,path}) для `--complete-files`.
std::vector<std::string> driverFileValueTokens() {
    static const std::vector<DriverOption> table = buildTrustTable();
    std::vector<std::string> out;
    out.reserve(table.size());
    for (const DriverOption& o : table) {
        if (o.type != "file" && o.type != "dir" && o.type != "path") {
            continue;
        }
        out.push_back("--" + o.name);
        if (!o.short_name.empty()) {
            out.push_back("-" + o.short_name);
        }
    }
    return out;
}

std::span<char*> applyDiagnostics(Options& opts, std::span<char*> args) {
    // args уже содержит только `-W...`-токены (собраны parseDriverArgs в ParseResult::diag_args):
    // единая точка применения -W-диагностик. При `-Whelp` parse_argv ставит opts.helpRequested().
    if (!args.empty()) {
        opts.parse_argv(args);
    }
    return args;
}

} // namespace trust
