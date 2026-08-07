#pragma once

#include "pipeline/makefile_build.hpp"
#include "module_loader/module_loader.hpp"
#include "types/registry.hpp"
#include "diag/context.hpp"
#include "ast/ast_nodes.hpp"
#include "transpiler/transpiler.hpp"
#include "semantic/symbol_index.hpp"

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

// ── Emit flags ──

enum class EmitFlags {
    None = 0,
    Tokens = 1 << 0,
    AST = 1 << 1,
    Cpp = 1 << 2,
    LexemesOnly = 1 << 3,
};

inline constexpr EmitFlags operator|(EmitFlags a, EmitFlags b) {
    return static_cast<EmitFlags>(static_cast<int>(a) | static_cast<int>(b));
}

inline constexpr EmitFlags operator&(EmitFlags a, EmitFlags b) {
    return static_cast<EmitFlags>(static_cast<int>(a) & static_cast<int>(b));
}

// ── Pipeline steps (bitmask) ──

enum class PipelineSteps {
    None = 0,
    ParseAST = 1 << 0,  // Parser (lexing + parsing done together in legacy)
    Semantic = 1 << 1,  // pass-менеджер семантики (SemanticPassRunner)
    Transpile = 1 << 2, // CppTranspiler
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

// ── Compile mode ──

enum class CompileMode {
    Executable,  ///< Compile to executable (default)
    ObjectFile,  ///< Compile to object file only (-c)
    StaticLib,   ///< Compile to static library (-a / --static-lib)
    SharedLib,   ///< Compile to shared library (-l / --shared-lib)
    TrustModule, ///< Compile to trust module (.trust, shared library with exports)
};

// ── Runtime link mode ──

enum class RuntimeLink {
    Static, ///< Link runtime as a static library (default; self-contained executable)
    Shared, ///< Link runtime as a dynamic library (trust-runtime.so)
};

// ── Pipeline parsed options ──

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
    bool run = false; ///< --run: после сборки исполняемого файла запустить его
    /// Собрать build-каталог (.cppt/_main.cppt/Makefile/build.conf/trust/LICENSE) БЕЗ
    /// компиляции/линковки. Используется trust-lsp `--emit-build-dir` для скачиваемого
    /// архива. build.conf при этом формируется переносимым (без абсолютных путей и
    /// привязки к рантайм-библиотеке).
    bool emit_build_dir_only = false;

    // Standard library options
    bool use_stdlib = true; // false если --no-stdlib

    // DSL macros options
    bool no_dsl = false;  // true если --no-dsl
    std::string dsl_file; // --dsl <file> вместо встроенного trust/dsl.src

    // Режим LSP-сервера: разрешает запускать семантический анализатор даже при наличии
    // ошибок лексера/парсера (AST может быть частичным). Transpile при этом по-прежнему
    // отсекается (runner.run() вернёт false при ошибках). CLI по умолчанию выключен.
    bool allow_semantic_on_errors = false; // --semantic-on-errors

    // True if no emit flags specified (full compile mode)
    bool should_compile() const { return emit_flags == EmitFlags::None; }
};

// Результат парсинга: опции + оставшиеся аргументы
struct ParseResult {
    PipelineOpts opts;
    std::vector<std::string> remaining_args;
    int exit_code = 0; // 0 = OK, 1 = error
};

// ── Pipeline result (stateless output of runPipeline) ──

struct PipelineResult {
    std::optional<std::vector<AstNodePtr>> astNodes;
    /// Собранные семантикой символы (имя→тип/диапазоны) для LSP; заполняется при
    /// FlagKind::Symbols (даже при ошибках лексера/парсера, на частичном AST).
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

// ── Pipeline ──

class Pipeline {
  public:
    Pipeline(Context& ctx, const PipelineOpts& opts);

    // ── CLI entry point ──
    int execute();

    /// Забирает владение реестром типов из Pipeline (для LSP): TypeId в SymbolInfo остаётся
    /// валидным после уничтожения Pipeline. Встроенные типы при этом разделяются через общее
    /// ядро TypeRegistry (без дублирования), пер-инстансовые — только пользовательские.
    std::unique_ptr<TypeRegistry> releaseTypes();

    // ── Базовый runPipeline (без Transpile) ──
    // Выполняет ParseAST, Semantic.
    // Transpile без cppOut — FAULT.
    // Возвращает PipelineResult с опциональным AST.
    PipelineResult runPipeline(PipelineSteps steps, MapperFile inputFile);

    // ── runPipeline с Transpile ──
    // Дополнительно выполняет CppTranspiler, записывая результат в cppOut.
    PipelineResult runPipeline(PipelineSteps steps, MapperFile inputFile, MapperFile cppOut, std::vector<CppTranspiler::ExportEntry>* out_exports = nullptr,
                               std::vector<std::string>* out_runtime_headers = nullptr, std::vector<std::string>* out_link_libs = nullptr);

    // ── Статические методы для CLI ──
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
        std::vector<CppTranspiler::ExportEntry> exports;
        /// Рантайм-заголовки (напр. "trust/rational.hpp"), реально использованные
        /// сгенерированным кодом — pipeline извлечёт их из trust-runtime.so.
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

    // Транспиляция тела одного модуля (индекс) в отдельный .cppt (полное тело — определения).
    void transpileModuleBody(std::size_t idx, const std::filesystem::path& cpptPath, std::vector<std::string>* runtime_headers = nullptr,
                             std::vector<std::string>* link_libs = nullptr);

    std::size_t m_mainModuleIndex{0}; ///< Индекс корневого (главного) модуля
};

// ── Free functions (old parse_args wrappers) ──

inline ParseResult Pipeline::parseArgs(int argc, char* argv[]) {
    return parseArgs(std::span<char*>(argv, static_cast<size_t>(argc)));
}

inline bool Pipeline::isSpecialExit(const ParseResult& r) {
    return r.opts.help_requested || r.opts.version_requested || r.exit_code != 0;
}

// ── Свободные функции ──

// Сохранение .cppt + .src_map рядом + #embed + export table
// embed_export_table=false — не встраивать экспорт-таблицу (модуль-исходник, линкуемый
// в программу: таблица принадлежит главному файлу, иначе дубли __trust_get_exports).
// program_record — запись кеша --run: первая строка ВСЕГДА версия компилятора
// "trust-lang\t<TRUST_VERSION_FULL>", далее строки "файл\tmd5" — главный файл (2-я строка),
// затем импортированные модули (встраивается в секцию .debug_trust_hash главного файла).
bool saveCppAndEmbedSourceMap(Context& ctx, MapperFile cpp_idx, const std::filesystem::path& cppt_path, bool verbose,
                              const std::vector<CppTranspiler::ExportEntry>& exports = {}, bool embed_export_table = true,
                              const std::string& program_record = {});

// Вычисление build_dir = temp_dir (если задан) или директория входного файла
std::filesystem::path computeBuildDir(const PipelineOpts& opts);

// cppt_path = build_dir / <input_stem>.cppt
std::filesystem::path computeCpptPath(const PipelineOpts& opts);

// ── Генерация build-каталога / архива для скачивания (trust-lsp --emit-build-dir) ──

// Генерирует build-файлы (Makefile, build.conf, _main.cppt, LICENSE и trust/ рантайм-
// заголовки) в build_dir рядом с .cppt. НЕ компилирует и не линкует. Вызывается и
// `trust build` (compileAndLink), и trust-lsp `--emit-build-dir`.
// build.conf — единый переносимый (без абсолютных путей и привязки к .so/.a):
// include-путь `-I.` (каталог сборки) + `LIBS += -ltrust-runtime -lgmp`. Локальная
// сборка резолвит `-ltrust-runtime` через LIBRARY_PATH (см. compileAndLink).
bool writeBuildFiles(const PipelineOpts& opts, const std::filesystem::path& cppt_path, const std::vector<std::filesystem::path>& module_cppt_paths,
                     const std::vector<std::string>& runtime_headers, const std::vector<std::string>& link_libs, const std::string& entry_func_name);

// Транспилирует trust_code и собирает tar.gz-архив build-каталога по пути
// <emit_dir>/trust-lang-<версия>-generated.tar.gz (без компиляции). Временные файлы
// build-каталога удаляются (RAII). Возвращает путь к архиву; пусто при ошибке (причина в
// out_error).
std::filesystem::path emitBuildDirArchive(const std::string& trust_code, const std::filesystem::path& emit_dir, std::string& out_error);

} // namespace trust