#pragma once

#include "pipeline/makefile_build.hpp"
#include "diag/context.hpp"
#include "ast/ast_nodes.hpp"
#include "transpiler/transpiler.hpp"

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
    Semantic = 1 << 1,  // SemanticAnalyzer
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

    // Standard library options
    bool use_stdlib = true; // false если --no-stdlib

    // DSL macros options
    bool no_dsl = false;  // true если --no-dsl
    std::string dsl_file; // --dsl <file> вместо встроенного std/dsl.src

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

    bool isValid() const { return astNodes.has_value(); }
};

// ── Pipeline ──

class Pipeline {
  public:
    Pipeline(Context& ctx, const PipelineOpts& opts);

    // ── CLI entry point ──
    int execute();

    // ── Базовый runPipeline (без Transpile) ──
    // Выполняет ParseAST, Semantic.
    // Transpile без cppOut — FAULT.
    // Возвращает PipelineResult с опциональным AST.
    PipelineResult runPipeline(PipelineSteps steps, MapperFile inputFile);

    // ── runPipeline с Transpile ──
    // Дополнительно выполняет CppTranspiler, записывая результат в cppOut.
    PipelineResult runPipeline(PipelineSteps steps, MapperFile inputFile, MapperFile cppOut, std::vector<CppTranspiler::ExportEntry>* out_exports = nullptr);

    // ── Статические методы для CLI ──
    static ParseResult parseArgs(int argc, char* argv[]);
    static ParseResult parseArgs(std::span<char*> argv);
    static bool isSpecialExit(const ParseResult& r);

  private:
    Context& m_ctx;
    const PipelineOpts m_opts;

    static PipelineSteps determineSteps(EmitFlags flags);

    int emitOutput(const PipelineResult& result);

    // Loads DSL macros into m_ctx (embedded std/dsl.src by default, --dsl <file>
    // replaces it, --no-dsl disables). Loaded once and inherited by every
    // nested Parser through Context.
    void loadDslMacros();

    // Helper: run Transpile pipeline + save .cppt + .src_map
    struct TranspileOutput {
        MapperFile outputIdx;
        std::filesystem::path cpptPath;
        std::vector<CppTranspiler::ExportEntry> exports;
        bool valid = false;
    };
    TranspileOutput runTranspileAndSave(MapperFile inputFile);
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
bool saveCppAndEmbedSourceMap(Context& ctx, MapperFile cpp_idx, const std::filesystem::path& cppt_path, bool verbose,
                              const std::vector<CppTranspiler::ExportEntry>& exports = {});

// Вычисление build_dir = temp_dir (если задан) или директория входного файла
std::filesystem::path computeBuildDir(const PipelineOpts& opts);

// cppt_path = build_dir / <input_stem>.cppt
std::filesystem::path computeCpptPath(const PipelineOpts& opts);

} // namespace trust