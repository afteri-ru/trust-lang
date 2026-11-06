#pragma once

#include <cstddef>
#include <optional>
#include <string>
#include <vector>

namespace trust {

enum class EmitFlags {
    None = 0,
    Tokens = 1 << 0,
    AST = 1 << 1,
    Cpp = 1 << 2,
    LexemesOnly = 1 << 3,
    Macros = 1 << 4,
};

inline constexpr EmitFlags operator|(EmitFlags a, EmitFlags b) {
    return static_cast<EmitFlags>(static_cast<int>(a) | static_cast<int>(b));
}

inline constexpr EmitFlags operator&(EmitFlags a, EmitFlags b) {
    return static_cast<EmitFlags>(static_cast<int>(a) & static_cast<int>(b));
}

/// Бэкенды (C++/SMT) шагами не являются: они запрашиваются через `RunRequest::cpp`/`smt`
/// (присутствие optional = запрос).
enum class FrontendSteps {
    None = 0,
    ParseAST = 1 << 0,
    Semantic = 1 << 1,
};

inline constexpr FrontendSteps operator|(FrontendSteps a, FrontendSteps b) {
    return static_cast<FrontendSteps>(static_cast<int>(a) | static_cast<int>(b));
}

inline constexpr FrontendSteps operator&(FrontendSteps a, FrontendSteps b) {
    return static_cast<FrontendSteps>(static_cast<int>(a) & static_cast<int>(b));
}

inline constexpr bool hasStep(FrontendSteps flags, FrontendSteps step) {
    return (static_cast<int>(flags) & static_cast<int>(step)) != 0;
}

/// kProgram: главный модуль помечается в source map (`setMainModuleFile`), его индекс хранится для
/// модульных TU, сайты импорта получают экспорт-интерфейс (`resolveImportExports`). kAnalysis - без
/// программной обвязки (emit-режимы, тесты).
enum class RunMode {
    kAnalysis,
    kProgram,
};

/// X-макрос - единый источник и enum, и make-цели (makeTargetForMode в toolchain/build/compile.cpp).
#define TRUST_COMPILE_MODE_TARGETS(O)                   \
    O(Executable, "executable") /* default */           \
    O(ObjectFile, "object")     /* -c */                \
    O(StaticLib, "static-lib")  /* -a / --static-lib */ \
    O(SharedLib, "shared-lib")  /* -l / --shared-lib */

enum class CompileMode {
#define TRUST_COMPILE_MODE_ENUM(name, target) name,
    TRUST_COMPILE_MODE_TARGETS(TRUST_COMPILE_MODE_ENUM)
#undef TRUST_COMPILE_MODE_ENUM
};

enum class RuntimeLink {
    Static, ///< self-contained executable (default)
    Shared, ///< link trust-runtime.so
};

struct PipelineOpts {
    std::string input_file;
    std::string output_file;
    EmitFlags emit_flags = EmitFlags::None;
    bool verbose = false;
    bool quiet = false;
    bool help_requested = false;
    bool version_requested = false;

    std::string temp_dir;
    /// Корень проекта для путей `\file`/`\\module\...`; пусто = каталог входного файла.
    std::string module_root;
    /// Дополнительные корни поиска модулей (`-I`, repeatable; порядок значим).
    std::vector<std::string> module_paths;
    /// Дополнительные корни поиска пакетов (`--package-path`, repeatable; порядок значим).
    std::vector<std::string> package_paths;
    std::string compiler;
    std::string compiler_options;
    CompileMode compile_mode = CompileMode::Executable;
    RuntimeLink runtime_link = RuntimeLink::Static;
    /// `--solver-mode=<mode>` (assert|export|calculate); пусто = не передана. Severity-диагностика
    /// «присутствуют trust-условия» управляется отдельно через `-Wsolver`.
    std::string solver_mode;
    /// `--solver-loop-unroll`: разворачивать циклы без инварианта (bounded); не severity. Без флага
    /// такие циклы дают `-Wsolver-loop`.
    bool solver_loop_unroll = false;
    /// `--stack-check=<mode>` (off|explicit|recursion|auto); пусто = дефолт explicit.
    std::string stack_check_mode;
    /// `--stack-check-reserve=<bytes>`; пусто = рантайм-дефолт 8192.
    std::string stack_check_reserve;
    /// `--stack-check-functions=<names>` (comma-separated): функции, ограничивающие m_stack_limit;
    /// пусто = все функции .stack_sizes.
    std::string stack_check_functions;
    /// `--verify=<mode>` (сейчас: all); пусто = не передана.
    std::string verify;
    /// `--noop-backend`: внутренний триггер стаб-стадии бэкенда (R37) — доказательство
    /// подключаемости бэкенда данными (строкой `BACKEND_STAGE_LIST`) без правок языковой модели.
    /// Не пользовательская опция.
    bool noop_backend = false;
    /// `-fsync-deadlock=<ms|s|us|ns|nano>` (без суффикса - секунды); пусто = не передана.
    /// Встраивается в начало main через trust::runtime::setSyncDeadlockFromString.
    std::string sync_deadlock_timeout;
    /// `--package <dir>`: упаковать каталог исходников (`trust.toml`) в архив `.trust`; вход - каталог.
    bool package_requested = false;
    /// `--extract <archive.trust> [-o <dir>]`: распаковать архив (по умолчанию `<name>/` в cwd).
    bool extract_requested = false;
    bool run = false;
    /// Аргументы запускаемой программы при --run: позиционные ПОСЛЕ входного файла.
    std::vector<std::string> run_args;
    /// Собрать build-каталог (.cppt/_main.cppt/Makefile/build.conf/trust/LICENSE) БЕЗ компиляции/
    /// линковки (trust-lsp --emit-build-dir); build.conf при этом переносимый (без абсолютных путей).
    bool emit_build_dir_only = false;
    /// `--emit-module-deps`: make-правила `<target>: <deps>`; отдельный emit-режим, не EmitFlags.
    bool emit_module_deps = false;
    /// `-fsingle-file`/`-fno-single-file`: main-обёртка в том же `.cppt`, отдельный `_main.cppt`
    /// не создаётся. Авто-дефолт при `--run` (резолв - в pipeline/cli.cpp).
    bool single_file = false;
    /// true, если флаг задан явно; нужен, чтобы `-fno-single-file` отключил авто-дефолт для `--run`.
    bool single_file_set = false;

    /// `-fcomments`/`-fno-comments`: вставлять документирующие комментарии в генерируемый C++.
    bool comments = true;
    /// `-foverflow-check`/`-fno-overflow-check`: детекция знакового переполнения (+,-,* и +=,-=,*=).
    bool overflow_check = true;
    /// Каноничные строки ФАКТИЧЕСКИ заданных codegen-релевантных опций (заполняет applyOption):
    /// смена любой инвалидирует кеш --run (.debug_trust_hash). Нерелевантные (-v/-q/-W/--temp-dir/-o)
    /// не попадают; порядок нормализуется сортировкой.
    std::vector<std::string> codegen_args;
    /// `-fsourcemap`/`-fno-sourcemap`: управляет только встраиванием карты в `.cppt`
    /// (сам `.src_map` пишется всегда).
    bool embed_source_map = true;

    /// `-ferror-limit=<N>`/`--error-limit=<N>`: предел ошибок (0 = без лимита). nullopt = не передана
    /// (DiagnosticEngine использует kDefaultErrorLimit).
    std::optional<std::size_t> error_limit;

    /// `--diagnostics-format=<text|json|sarif>` (пусто = text); валидация - в parseCommandLine.
    /// Не codegen-опция: в кеш `--run` не входит.
    std::string diagnostics_format;

    /// Дополнительные библиотеки линковки (`-l<name>`); объединяются с `@[link("имя")]` из кода.
    std::vector<std::string> link_libs_cli;
    /// Каталоги поиска библиотек (`-L<dir>`); пишутся в build.conf LDFLAGS.
    std::vector<std::string> link_dirs;

    bool use_stdlib = true; ///< false при --no-stdlib
    bool no_dsl = false;    ///< true при --no-dsl
    std::string dsl_file;   ///< --dsl <file> вместо stdlib/dsl.src

    /// `--semantic-on-errors`: разрешает семантику даже при ошибках лексера/парсера (AST частичный).
    /// Transpile всё равно отсекается (runner.run() вернёт false при ошибках). CLI по умолчанию выключен.
    bool allow_semantic_on_errors = false;

    bool format_requested = false;
    bool format_check = false;
    bool format_dump_config = false;
    /// `--keywords=<list>`: макросы, допустимые без '@' (подавляет -Wsigil для bare-макросов).
    std::string keywords;
    /// `--keyword-sigil=<mode>` (remove|add|none); пусто = используется .trust-format.
    std::string keyword_sigil;
    /// Значения IndentWidth/UseTabs/ColumnLimit задаются только в .trust-format (не через CLI-флаги).
    std::string format_config;
    bool format_no_config = false; ///< --format-style=none
    bool complete_options = false;
    bool complete_files = false;

    bool should_compile() const { return emit_flags == EmitFlags::None; }
};

struct ParseResult {
    PipelineOpts opts;
    std::vector<std::string> remaining_args;
    /// CLI-диагностики `-W...` (в т.ч. `-Whelp`), собранные parseDriverArgs (cli.hpp); применяются
    /// через applyDiagnostics -> Options::parse_argv.
    std::vector<std::string> diag_args;
    /// true, если в diag_args есть `-Whelp`: ранний пропуск проверки входного файла в parseCommandLine.
    bool diag_help_requested = false;
    int exit_code = 0; // 0 = OK, 1 = error
};

} // namespace trust
