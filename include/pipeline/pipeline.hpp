#pragma once

#include "pipeline/makefile_build.hpp"
#include "module_loader/module_loader.hpp"
#include "types/registry.hpp"
#include "diag/context.hpp"
#include "ast/ast_nodes.hpp"
#include "transpiler/transpiler.hpp"
#include "semantic/symbol_index.hpp"
#include "solver/smt_ast.hpp"

namespace trust {
class Macro;
} // namespace trust

#include <algorithm>
#include <array>
#include <cstdlib>
#include <filesystem>
#include <memory>
#include <optional>
#include <span>
#include <string>
#include <string_view>
#include <vector>

namespace trust {

// -- Emit flags --

enum class EmitFlags {
    None = 0,
    Tokens = 1 << 0,
    AST = 1 << 1,
    Cpp = 1 << 2,
    LexemesOnly = 1 << 3,
    Macros = 1 << 4, ///< --emit-macros: напечатать макроопределения после парсинга
};

inline constexpr EmitFlags operator|(EmitFlags a, EmitFlags b) {
    return static_cast<EmitFlags>(static_cast<int>(a) | static_cast<int>(b));
}

inline constexpr EmitFlags operator&(EmitFlags a, EmitFlags b) {
    return static_cast<EmitFlags>(static_cast<int>(a) & static_cast<int>(b));
}

// -- Pipeline steps (bitmask) --

enum class PipelineSteps {
    None = 0,
    ParseAST = 1 << 0,  // Parser (lexing + parsing together)
    Semantic = 1 << 1,  // pass-менеджер семантики (SemanticPassRunner)
    Transpile = 1 << 2, // CppTranspiler
    Solver = 1 << 3,    // генерация SMT-LIB 2 (TrustToSmt) для --solver-mode=export/calculate
};

inline constexpr PipelineSteps operator|(PipelineSteps a, PipelineSteps b) {
    return static_cast<PipelineSteps>(static_cast<int>(a) | static_cast<int>(b));
}

inline constexpr PipelineSteps operator&(PipelineSteps a, PipelineSteps b) {
    return static_cast<PipelineSteps>(static_cast<int>(a) & static_cast<int>(b));
}

inline constexpr bool hasStep(PipelineSteps flags, PipelineSteps step) {
    return (static_cast<int>(flags) & static_cast<int>(step)) != 0;
}

// -- Compile mode --

enum class CompileMode {
    Executable,  ///< Compile to executable (default)
    ObjectFile,  ///< Compile to object file only (-c)
    StaticLib,   ///< Compile to static library (-a / --static-lib)
    SharedLib,   ///< Compile to shared library (-l / --shared-lib)
    TrustModule, ///< Compile to trust module (.trust, shared library with exports)
};

// -- Runtime link mode --

enum class RuntimeLink {
    Static, ///< Link runtime as a static library (default; self-contained executable)
    Shared, ///< Link runtime as a dynamic library (trust-runtime.so)
};

// -- Pipeline parsed options --

struct PipelineOpts {
    std::string input_file;
    std::string output_file;
    EmitFlags emit_flags = EmitFlags::None;
    bool verbose = false;
    bool quiet = false;
    bool help_requested = false;
    bool version_requested = false;
    bool module_info_requested = false; ///< --module-info flag

    // Compile options
    std::string temp_dir;
    std::string compiler;
    std::string compiler_options;
    CompileMode compile_mode = CompileMode::Executable;
    RuntimeLink runtime_link = RuntimeLink::Static;
    /// Поведенческий режим обработки trust-конструкций: `--solver-mode=<mode>` (assert|export|calculate).
    /// Пустая строка = опция не передана (флаг FlagKind::SolverMode не задан - никакое поведение).
    /// Severity-диагностика «присутствуют trust-условия» управляется отдельно через `-Wsolver`.
    std::string solver_mode;
    /// Поведенческий флаг `--solver-loop-unroll`: глобально разворачивать циклы без инварианта
    /// (bounded). Не диагностика (не severity). По умолчанию выключен - циклы без инварианта дают
    /// диагностику `-Wsolver-loop`.
    bool solver_loop_unroll = false;
    bool run = false; ///< --run: после сборки исполняемого файла запустить его
    /// Собрать build-каталог (.cppt/_main.cppt/Makefile/build.conf/trust/LICENSE) БЕЗ
    /// компиляции/линковки. Используется trust-lsp `--emit-build-dir` для скачиваемого
    /// архива. build.conf при этом формируется переносимым (без абсолютных путей и
    /// привязки к рантайм-библиотеке).
    bool emit_build_dir_only = false;

    // -- Linking options (CLI-пересечение с @[link(...)] из исходника) --
    /// Дополнительные библиотеки линковки из CLI (`-l<name>`). Объединяются с
    /// `@[link("имя")]` из кода (linkLibs()) перед записью в build.conf (LIBS += ...).
    std::vector<std::string> link_libs_cli;
    /// Каталоги поиска библиотек из CLI (`-L<dir>`), пишутся в build.conf LDFLAGS.
    std::vector<std::string> link_dirs;

    // Standard library options
    bool use_stdlib = true; // false если --no-stdlib

    // DSL macros options
    bool no_dsl = false;  // true если --no-dsl
    std::string dsl_file; // --dsl <file> вместо встроенного trust/dsl.src

    // Режим LSP-сервера: разрешает запускать семантический анализатор даже при наличии
    // ошибок лексера/парсера (AST может быть частичным). Transpile при этом по-прежнему
    // отсекается (runner.run() вернёт false при ошибках). CLI по умолчанию выключен.
    bool allow_semantic_on_errors = false; // --semantic-on-errors

    // -- Форматирование (pretty-print) --
    bool format_requested = false;   ///< --format: отформатировать входной файл (вывод в stdout)
    bool format_check = false;       ///< --format-check: проверить, отформатирован ли файл
    bool format_dump_config = false; ///< --format-dump-config: вывести настройки с дефолтами/комментариями
    /// --keywords=<list>: имена макросов, допустимые без '@' (запятая без пробелов). Пишется в
    /// значение флага FlagKind::Keywords (управляет подавлением -Wsigil для bare-макросов).
    std::string keywords;
    // Переопределения форматирования: только выбор конфига/стиля. Значения IndentWidth/UseTabs/
    // ColumnLimit задаются в .trust-format (не через CLI-флаги переопределения).
    std::string format_config;     ///< --format-config=<file>
    bool format_no_config = false; ///< --format-style=none
    bool complete_options = false; ///< --complete-options: вывести имена опций для shell-completion
    bool complete_files = false;   ///< --complete-files: вывести опции со значением-файлом для shell-completion

    // True if no emit flags specified (full compile mode)
    bool should_compile() const { return emit_flags == EmitFlags::None; }
};

// Результат парсинга: опции + оставшиеся аргументы
struct ParseResult {
    PipelineOpts opts;
    std::vector<std::string> remaining_args;
    /// CLI-диагностики `-W...` (в т.ч. `-Whelp`), собранные арity-aware парсером
    /// `parseDriverArgs` (см. cli.hpp). Применяются через applyDiagnostics ->
    /// Options::parse_argv. Справка по диагностикам печатается в trust.cpp через
    /// Options::helpRequested() (единый флаг, set в parse_argv).
    std::vector<std::string> diag_args;
    /// true, если в diag_args есть `-Whelp` (справка по диагностикам). Выставляется
    /// парсером parseDriverArgs (единый источник для раннего пропуска проверки входного
    /// файла в Pipeline::parseArgs; сама справка - через Options::helpRequested()).
    bool diag_help_requested = false;
    int exit_code = 0; // 0 = OK, 1 = error
};

// -- Pipeline result (stateless output of runPipeline) --

struct PipelineResult {
    std::optional<std::vector<AstNodePtr>> astNodes;
    /// Собранные семантикой символы (имя→тип/диапазоны) для LSP; заполняется при
    /// semantic::FlagKind::Symbols (даже при ошибках лексера/парсера, на частичном AST).
    std::optional<SymbolIndex> symbols;

    bool isValid() const { return astNodes.has_value(); }
};

// Дособирает макросы из Context::macroDefs() в SymbolIndex (isMacro=true). Единая точка для
// обоих runPipeline и для LSP-сервера (transpileSource): имена макросов, записанные во время
// парсинга, не должны теряться даже если семантический шаг не выполнился/упал (Fatal/исключение).
inline void appendMacroSymbols(const Context& ctx, SymbolIndex& out) {
    for (const auto& md : ctx.macroDefs()) {
        SymbolInfo si;
        si.name = md.name;
        si.type = INVALID_TYPE_ID;
        si.nameRange = md.range;
        si.scopeRange = md.range;
        si.isMacro = true;
        si.documentation = md.documentation;
        out.push_back(std::move(si));
    }
}

// -- Pipeline --

class Pipeline {
  public:
    Pipeline(Context& ctx, const PipelineOpts& opts);

    // -- CLI entry point --
    int execute();

    /// Забирает владение реестром типов из Pipeline (для LSP): TypeId в SymbolInfo остаётся
    /// валидным после уничтожения Pipeline. Встроенные типы при этом разделяются через общее
    /// ядро TypeRegistry (без дублирования), пер-инстансовые - только пользовательские.
    std::unique_ptr<TypeRegistry> releaseTypes();

    /// Загружает DSL (если включён; уважает --dsl/--no-dsl) и возвращает эффективный список
    /// keywords (приоритет CLI --keywords > .trust-format "Keywords:" > дефолт dsl.src).
    /// Используется и для диагностики -Wsigil, и для форматирования (набор «ключевых слов»).
    std::vector<std::string> effectiveKeywords();

    // -- Базовый runPipeline (без Transpile) --
    // Выполняет ParseAST, Semantic.
    // Transpile без cppOut - FAULT.
    // Возвращает PipelineResult с опциональным AST.
    PipelineResult runPipeline(PipelineSteps steps, MapperFile inputFile);

    // -- runPipeline с Transpile --
    // Дополнительно выполняет CppTranspiler, записывая результат в cppOut.
    PipelineResult runPipeline(PipelineSteps steps, MapperFile inputFile, MapperFile cppOut, std::vector<ExportEntry>* out_exports = nullptr,
                               std::vector<std::string>* out_runtime_headers = nullptr, std::vector<std::string>* out_link_libs = nullptr,
                               solver::SmtScript* out_script = nullptr);

    // -- Статические методы для CLI --
    static ParseResult parseArgs(int argc, char* argv[]);
    static ParseResult parseArgs(std::span<char*> argv);
    static bool isSpecialExit(const ParseResult& r);

  private:
    Context& m_ctx;
    const PipelineOpts m_opts;
    std::unique_ptr<ModuleLoader> m_loader; ///< Владение загрузчиком модулей (внедряется в m_ctx)
    std::unique_ptr<TypeRegistry> m_types;  ///< Владение реестром типов (внедряется в m_ctx)

    static PipelineSteps determineSteps(EmitFlags flags);

    int emitOutput(const PipelineResult& result);

    // Loads DSL macros into m_ctx (embedded trust/dsl.src by default, --dsl <file>
    // replaces it, --no-dsl disables). Loaded once and inherited by every
    // nested Parser through Context.
    void loadDslMacros();

    // Helper: run Transpile pipeline + save .cppt + .src_map
    struct TranspileOutput {
        MapperFile outputIdx;
        std::filesystem::path cpptPath;
        std::vector<ExportEntry> exports;
        /// Рантайм-заголовки (напр. "trust/rational.hpp"), реально использованные
        /// сгенерированным кодом - pipeline извлечёт их из trust-runtime.so.
        std::vector<std::string> runtimeHeaders;
        /// Флаги линковки нативных библиотек (`-l<имя>`) из `@[link("имя")]`.
        std::vector<std::string> linkLibs;
        bool valid = false;
    };
    TranspileOutput runTranspileAndSave(MapperFile inputFile);

    // Генерация отдельного .cppt для каждого загруженного исходного модуля (кроме главного).
    // Определения модулей живут в отдельных единицах трансляции и линкуются с главным файлом.
    // Возвращает пути к сгенерированным .cppt модулей. Если задан, накапливает
    // рантайм-заголовки, использованные модульными единицами.
    std::vector<std::filesystem::path> generateModuleOutputs(std::vector<std::string>* module_runtime_headers = nullptr,
                                                             std::vector<std::string>* module_link_libs = nullptr);

    // Транспиляция тела одного модуля (индекс) в отдельный .cppt (полное тело - определения).
    void transpileModuleBody(std::size_t idx, const std::filesystem::path& cpptPath, std::vector<std::string>* runtime_headers = nullptr,
                             std::vector<std::string>* link_libs = nullptr);

    std::size_t m_mainModuleIndex{0}; ///< Индекс корневого (главного) модуля
};

// -- Free functions (old parse_args wrappers) --

inline ParseResult Pipeline::parseArgs(int argc, char* argv[]) {
    return parseArgs(std::span<char*>(argv, static_cast<size_t>(argc)));
}

inline bool Pipeline::isSpecialExit(const ParseResult& r) {
    return r.opts.help_requested || r.opts.version_requested || r.exit_code != 0;
}

} // namespace trust

// -- Модули конвейера (декомпозиция pipeline.cpp) --
// Свободные функции, ранее жившие в pipeline.cpp, разнесены по модулям по зонам
// ответственности. pipeline.hpp остаётся «зонтиком» и включает их, поэтому все
// потребители `pipeline/pipeline.hpp` видят прежний набор функций без правок.
#include "pipeline/io.hpp"
#include "pipeline/runtime_locator.hpp"
#include "pipeline/build.hpp"
#include "pipeline/run.hpp"
#include "pipeline/source_map.hpp"
#include "pipeline/archive.hpp"
#include "pipeline/module_info.hpp"