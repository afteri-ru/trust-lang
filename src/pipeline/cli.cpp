#include "pipeline/cli.hpp"

#include "diag/options.hpp"
#include "pipeline/pipeline.hpp"
#include "utils/io.hpp"

#include <cstdlib>

namespace trust {

// Enum id опций драйвера trust. Каждый драйверный бинарник объявляет СВОЙ enum; значения
// int передаются в общий parseDriverArgs. Таблица DriverOption - ниже в buildTrustTable().
enum class DriverOptId {
    Help,
    Version,
    Verbose,
    Quiet,
    Input,
    Output,
    TempDir,
    EmitTokens,
    EmitAst,
    EmitCpp,
    EmitLexemes,
    EmitMacros,
    ObjectFile,
    StaticLib,
    SharedLib,
    TrustModule,
    Run,
    ModuleInfo,
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
    Format,
    FormatCheck,
    FormatDumpConfig,
    Keywords,
    FormatConfig,
    FormatStyle,
    CompleteOptions,
    CompleteFiles,
};

namespace {

// -- Применение опции драйвера к PipelineOpts (единое связывание; для Flag value пуст). --
// Одна таблица-switch: каждый DriverOptId привязан к полям PipelineOpts здесь. «Добавить
// опцию» = строка в buildTrustTable + строка-связывание в этом switch.
void applyOption(DriverOptId id, const std::string& value, PipelineOpts& opts) {
    switch (id) {
    // -- General --
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
    // -- Input & Output --
    case DriverOptId::Output:
        opts.output_file = value;
        break;
    case DriverOptId::TempDir:
        opts.temp_dir = value;
        break;
    // -- Compilation model --
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
    case DriverOptId::ObjectFile:
        opts.compile_mode = CompileMode::ObjectFile;
        break;
    case DriverOptId::StaticLib:
        opts.compile_mode = CompileMode::StaticLib;
        break;
    case DriverOptId::SharedLib:
        opts.compile_mode = CompileMode::SharedLib;
        break;
    case DriverOptId::TrustModule:
        opts.compile_mode = CompileMode::TrustModule;
        break;
    case DriverOptId::Run:
        opts.compile_mode = CompileMode::Executable;
        opts.run = true;
        break;
    case DriverOptId::ModuleInfo:
        opts.module_info_requested = true;
        break;
    // -- Linking --
    case DriverOptId::LinkLib:
        opts.link_libs_cli.push_back(value);
        break;
    case DriverOptId::LinkDir:
        opts.link_dirs.push_back(value);
        break;
    case DriverOptId::LinkRuntime:
        opts.runtime_link = (value == "shared") ? RuntimeLink::Shared : RuntimeLink::Static;
        break;
    // -- Toolchain --
    case DriverOptId::Compiler:
        opts.compiler = value;
        break;
    case DriverOptId::CompilerOptions:
        opts.compiler_options = value;
        break;
    // -- Project-specific --
    case DriverOptId::NoStdlib:
        opts.use_stdlib = false;
        break;
    case DriverOptId::Dsl:
        opts.dsl_file = value;
        break;
    case DriverOptId::NoDsl:
        opts.no_dsl = true;
        break;
    case DriverOptId::SemanticOnErrors:
        opts.allow_semantic_on_errors = true;
        break;
    case DriverOptId::SolverMode:
        // Поведенческий режим trust-условий (assert/export/calculate). Валидация значения
        // выполняется в Pipeline::parseArgs (нужен exit_code); здесь - только сохранение сырого.
        opts.solver_mode = value;
        break;
    case DriverOptId::SolverLoopUnroll:
        // Поведенческий флаг (не severity): `-fsolver-loop-unroll` / `--solver-loop-unroll` включает,
        // `-fno-solver-loop-unroll` выключает (глобальное разворачивание циклов без инварианта).
        opts.solver_loop_unroll = (value != "off");
        break;
    case DriverOptId::Format:
        opts.format_requested = true;
        break;
    case DriverOptId::FormatCheck:
        opts.format_requested = true;
        opts.format_check = true;
        break;
    case DriverOptId::FormatDumpConfig:
        opts.format_dump_config = true;
        break;
    case DriverOptId::Keywords:
        opts.keywords = value;
        break;
    case DriverOptId::FormatConfig:
        opts.format_config = value;
        break;
    case DriverOptId::FormatStyle:
        // --format-style=none отключает поиск .trust-format; иных стилей нет.
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
}

} // namespace

namespace {
// -- Таблица опций драйвера trust (enum + vector<DriverOption> + switch-связывание выше).
//    Единый источник данных для общего парсера (parseDriverArgs) и справки (driverHelp). --
std::vector<DriverOption> buildTrustTable() {
    std::vector<DriverOption> out = {
        // General
        {int(DriverOptId::Help), "help", "h", CliOpt::Flag, "", "Show this help message", CliCategory::General},
        {int(DriverOptId::Version), "version", "", CliOpt::Flag, "", "Show version information", CliCategory::General},
        {int(DriverOptId::Verbose), "verbose", "v", CliOpt::Flag, "", "Verbose output", CliCategory::General},
        {int(DriverOptId::Quiet), "quiet", "q", CliOpt::Flag, "", "Quiet mode (suppress warnings)", CliCategory::General},
        // Input & Output
        {int(DriverOptId::Input), "input", "", CliOpt::Value, "file", "Input source file", CliCategory::InputOutput},
        {int(DriverOptId::Output), "output", "o", CliOpt::Value, "file", "Output file (default: stdout)", CliCategory::InputOutput},
        {int(DriverOptId::TempDir), "temp-dir", "", CliOpt::Value, "dir", "Temporary directory for intermediate files", CliCategory::InputOutput},
        // Compilation model / pipeline
        {int(DriverOptId::EmitTokens), "emit-tokens", "", CliOpt::Flag, "", "Print tokens after lexing", CliCategory::CompileModel},
        {int(DriverOptId::EmitAst), "emit-ast", "", CliOpt::Flag, "", "Print AST after parsing", CliCategory::CompileModel},
        {int(DriverOptId::EmitCpp), "emit-cpp", "", CliOpt::Flag, "", "Emit C++ source file", CliCategory::CompileModel},
        {int(DriverOptId::EmitLexemes), "emit-lexemes", "", CliOpt::Flag, "", "Stop after lexing, output lexemes only", CliCategory::CompileModel},
        {int(DriverOptId::EmitMacros), "emit-macros", "", CliOpt::Flag, "", "Print macro definitions after parsing", CliCategory::CompileModel},
        {int(DriverOptId::ObjectFile), "c", "", CliOpt::Flag, "", "Compile to object file (.o)", CliCategory::CompileModel},
        {int(DriverOptId::StaticLib), "static-lib", "a", CliOpt::Flag, "", "Compile to static library (.a)", CliCategory::CompileModel},
        {int(DriverOptId::SharedLib), "shared-lib", "", CliOpt::Flag, "", "Compile to shared library (.so)", CliCategory::CompileModel},
        {int(DriverOptId::TrustModule), "module", "m", CliOpt::Flag, "", "Compile to trust module (.trust)", CliCategory::CompileModel},
        {int(DriverOptId::Run), "run", "", CliOpt::Flag, "", "Build and run the program", CliCategory::CompileModel},
        {int(DriverOptId::ModuleInfo), "module-info", "", CliOpt::Flag, "", "Show exported symbols and version of a .trust module", CliCategory::CompileModel},
        // Linking
        {int(DriverOptId::LinkLib), "l", "", CliOpt::ValueList, "lib", "Additional library to link (repeatable; merged with @[link(...)])",
         CliCategory::Linking},
        {int(DriverOptId::LinkDir), "L", "", CliOpt::ValueList, "dir", "Add directory to the library search path (repeatable)", CliCategory::Linking},
        {int(DriverOptId::LinkRuntime), "link-runtime", "", CliOpt::Value, "mode", "How to link the trust runtime: static (default) or shared",
         CliCategory::Linking},
        // Toolchain
        {int(DriverOptId::Compiler), "compiler", "", CliOpt::Value, "path", "Compiler path", CliCategory::Toolchain},
        {int(DriverOptId::CompilerOptions), "options", "", CliOpt::Value, "opts", "Additional compiler options (quoted)", CliCategory::Toolchain},
        // Project-specific
        {int(DriverOptId::NoStdlib), "no-stdlib", "", CliOpt::Flag, "", "Disable standard library types", CliCategory::ProjectSpecific},
        {int(DriverOptId::Dsl), "dsl", "", CliOpt::Value, "file", "Load DSL macros from file instead of embedded trust/dsl.src", CliCategory::ProjectSpecific},
        {int(DriverOptId::NoDsl), "no-dsl", "", CliOpt::Flag, "", "Disable loading DSL macros", CliCategory::ProjectSpecific},
        {int(DriverOptId::SemanticOnErrors), "semantic-on-errors", "", CliOpt::Flag, "", "Run the semantic analyzer even when the lexer/parser produced errors",
         CliCategory::ProjectSpecific},
        // Formatting
        {int(DriverOptId::Format), "format", "", CliOpt::Flag, "", "Format the input Trust source file and print the result to stdout",
         CliCategory::Formatting},
        {int(DriverOptId::FormatCheck), "format-check", "", CliOpt::Flag, "",
         "Check whether the input file is already formatted; exit 0 if so, 1 if it would change", CliCategory::Formatting},
        {int(DriverOptId::FormatDumpConfig), "format-dump-config", "", CliOpt::Flag, "",
         "Print all formatting settings with default values and comments (like clang-format -dump-config)", CliCategory::Formatting},
        {int(DriverOptId::FormatConfig), "format-config", "", CliOpt::Value, "file", "Path to a .trust-format configuration file", CliCategory::Formatting},
        {int(DriverOptId::FormatStyle), "format-style", "", CliOpt::Value, "style",
         "Formatting style: 'file' (default, use .trust-format) or 'none' (defaults only)", CliCategory::Formatting},
        {int(DriverOptId::CompleteOptions), "complete-options", "", CliOpt::Flag, "",
         "Internal: print all option names (--...) and -W diagnostics for shell completion", CliCategory::Formatting},
        {int(DriverOptId::CompleteFiles), "complete-files", "", CliOpt::Flag, "",
         "Internal: print driver options whose value is a file/path (--...) for shell completion", CliCategory::Formatting},
    };
    // Общие опции анализа (--solver-mode, --keywords, -fsolver-loop-unroll) - ЕДИНЫЙ источник
    // commonAnalysisOptions (include/pipeline/cli.hpp), используемый и trust, и trust-lsp.
    // Здесь задаются только id из DriverOptId; имя/арность/help/категория централизованы.
    auto common = commonAnalysisOptions(int(DriverOptId::SolverMode), int(DriverOptId::Keywords), int(DriverOptId::SolverLoopUnroll));
    out.insert(out.end(), common.begin(), common.end());
    return out;
}
} // namespace

// -- Обёртка для trust (ParseResult + таблица trust). --
// Возвращает сообщение об ошибке (пусто = успех). ParseResult заполняется частично даже при
// ошибке (то, что успело разобраться). Печать - обязанность вызывающего (trust.cpp через
// Pipeline::parseArgs, applyAnalysisArgs). Это позволяет LSP отчитываться об ошибках опций
// анализа как диагностиках, а не печатать в errs().
std::string parseDriverArgs(ParseResult& result, std::span<std::string> args) {
    static const std::vector<DriverOption> table = buildTrustTable();
    auto apply = [&](int id, const std::string& value) -> bool {
        applyOption(static_cast<DriverOptId>(id), value, result.opts);
        return true;
    };
    return parseDriverArgs(args, table, apply, result.diag_args, result.opts.input_file, result.diag_help_requested);
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

// Имена driver-опций со значением-файлом/путём (type ∈ {file,dir,path}) для
// shell-completion (`--complete-files`). Источник — та же таблица buildTrustTable,
// поэтому список не дублируется и не «дрейфует» при изменении опций.
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
    // args уже содержит только `-W...`-токены (собраны арity-aware парсером parseDriverArgs
    // в ParseResult::diag_args). Здесь - единая точка применения -W-диагностик:
    // Options::parse_argv. При `-Whelp` parse_argv устанавливает opts.helpRequested() == true
    // (флаг справки диагностик, см. diag/OPTIONS.md §5).
    if (!args.empty()) {
        opts.parse_argv(args);
    }
    return args;
}

} // namespace trust
