#pragma once

// include/driver/options.hpp
// Модель опций драйвера и конвейера: emit-флаги, шаги конвейера, режимы компиляции/линковки,
// разобранные опции (PipelineOpts) и результат разбора аргументов (ParseResult).
// Вынесена из pipeline_lib, чтобы драйверные бинарники (trust/trust-lsp/trust-dap/playground)
// и LSP-слой опций использовали её без линковки всего конвейера.

#include <string>
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
    /// Поведенческий режим контроля переполнения стека: `--stack-check=<mode>` (off|explicit|recursion|auto).
    /// Пустая строка = опция не передана (используется дефолт explicit из флага FlagKind::StackCheck).
    std::string stack_check_mode;
    /// Минимальный резерв стека (reserve) в байтах: `--stack-check-reserve=<bytes>`. Пустая строка =
    /// опция не передана (используется рантайм-дефолт 8192).
    std::string stack_check_reserve;
    /// Перечень функций (comma-separated trust-имена), ограничивающих m_stack_limit для limit-проверок:
    /// `--stack-check-functions=<names>`. Пусто = все функции .stack_sizes.
    std::string stack_check_functions;
    /// Compile-time дефолт таймаута детектора взаимной блокировки для синхронизированных ссылок:
    /// `-fsync-deadlock=<ms|s|us|ns|nano>` (без суффикса = секунды; дефолт рантайма 5s). Пустая строка =
    /// опция не передана. Встраивается в начало main через trust::runtime::setSyncDeadlockFromString.
    std::string sync_deadlock_timeout;
    bool run = false; ///< --run: после сборки исполняемого файла запустить его
    /// Аргументы командной строки, передаваемые запущенной программе при --run.
    /// Собираются из позиционных аргументов ПОСЛЕ входного файла (шебанг: ./prog.src a b).
    std::vector<std::string> run_args;
    /// Собрать build-каталог (.cppt/_main.cppt/Makefile/build.conf/trust/LICENSE) БЕЗ
    /// компиляции/линковки. Используется trust-lsp `--emit-build-dir` для скачиваемого
    /// архива. build.conf при этом формируется переносимым (без абсолютных путей и
    /// привязки к рантайм-библиотеке).
    bool emit_build_dir_only = false;
    /// Однофайловый режим генерации исполняемой программы (`-fsingle-file`/`-fno-single-file`):
    /// `int main`-обёртка встраивается в тот же `.cppt`, отдельный `_main.cppt` не создаётся.
    /// По умолчанию включается при `--run` (резолв - в pipeline_parser.cpp). Скрипт без
    /// `__main__` в этом режиме пока не поддерживается - выводится диагностика (включение
    /// даст будущий функционал «модуль-скрипт»).
    bool single_file = false;
    /// true, если флаг задан явно (`-fsingle-file`/`--single-file`/`-fno-single-file`). Если
    /// не задан - применяется дефолт (включить при `--run`). Нужен, чтобы `-fno-single-file`
    /// мог явно отключить авто-дефолт для `--run` (многофайловая сборка).
    bool single_file_set = false;

    /// Поведенческий флаг `-fcomments`/`-fno-comments` (не диагностика): вставлять ли
    /// документирующие комментарии в генерируемый C++-код. По умолчанию true (добавлять).
    bool comments = true;
    /// Поведенческий флаг `-foverflow-check`/`-fno-overflow-check` (не диагностика): детекция
    /// знакового целочисленного переполнения в арифметике (+,-,* и +=,-=,*=) с runtime-IntMinus.
    /// Default = true (безопасность по умолчанию).
    bool overflow_check = true;
    /// Каноничные строки ФАКТИЧЕСКИ заданных codegen-релевантных опций (заполняет applyOption):
    /// напр. "-fno-overflow-check", "--stack-check=recursion", "-lfoo". Используется в записи
    /// кеша --run (`.debug_trust_hash`): смена любой из них инвалидирует кеш. Нерелевантные
    /// (-v/-q/-W/--temp-dir/-o) сюда не попадают; порядок нормализуется сортировкой.
    std::vector<std::string> codegen_args;
    /// Поведенческий флаг `-fsourcemap`/`-fno-sourcemap` (не диагностика): добавлять ли
    /// маппинг (source map) и экспорт-таблицу в генерируемый C++-файл. Файл `.src_map`
    /// генерируется всегда; флаг управляет только `#embed "<name>.src_map"` и экспорт-таблицей.
    bool embed_source_map = true;

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
    /// --keyword-sigil=<mode>: remove|add|none — обработка '@'-сигила ключевых слов в форматтере
    /// (default = remove, убирать '@'). Пустая строка = CLI не задан (используется .trust-format).
    std::string keyword_sigil;
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

} // namespace trust
