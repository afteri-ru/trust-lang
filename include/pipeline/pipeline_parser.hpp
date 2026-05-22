#pragma once

#include "pipeline/options.h"

#include <algorithm>
#include <array>
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
    Module = 1 << 3,
};

inline constexpr EmitFlags operator|(EmitFlags a, EmitFlags b) {
    return static_cast<EmitFlags>(static_cast<int>(a) | static_cast<int>(b));
}

inline constexpr EmitFlags operator&(EmitFlags a, EmitFlags b) {
    return static_cast<EmitFlags>(static_cast<int>(a) & static_cast<int>(b));
}

// ── Pipeline parsed options ──

struct PipelineOpts {
    std::string input_file;
    std::string output_file;
    EmitFlags emit_flags = EmitFlags::None;
    bool verbose = false;
    bool quiet = false;
    bool help_requested = false;
    bool version_requested = false;

    // Compile options
    std::string temp_dir;
    std::string compiler;
    std::string compiler_options;
    bool compile_to_object = false;
    bool compile_to_static_lib = false;
    bool compile_to_shared_lib = false;
    bool gen_binding_header = false;
    std::string binding_header_file;
    bool binding_header_explicitly_set = false;

    // True if no emit flags specified (full compile mode)
    bool should_compile() const { return emit_flags == EmitFlags::None; }
};

// Результат парсинга: опции + оставшиеся аргументы
struct ParseResult {
    PipelineOpts opts;
    std::vector<std::string> remaining_args;
    int exit_code = 0; // 0 = OK, 1 = error
};

// ── Functions ──

// Парсинг аргументов через CLI11
// Возвращает ParseResult с распознанными опциями и оставшимися аргументами
ParseResult parse_args(int argc, char* argv[]);

// Перегрузка для span
ParseResult parse_args(std::span<char*> argv);

// Встроенная справка: true если --help или --version
inline bool is_special_exit(const ParseResult& r) {
    return r.opts.help_requested || r.opts.version_requested || r.exit_code != 0;
}

} // namespace trust