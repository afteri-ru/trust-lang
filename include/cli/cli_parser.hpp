#pragma once

#include "cli/option_cli_def.hpp"

#include <span>
#include <string>
#include <vector>

namespace trust {

// Флаги вывода
enum class CliEmitFlags {
    None = 0,
    Tokens = 1 << 0,
    AST = 1 << 1,
    Cpp = 1 << 2,
    Module = 1 << 3,
};

inline constexpr CliEmitFlags operator|(CliEmitFlags a, CliEmitFlags b) {
    return static_cast<CliEmitFlags>(static_cast<int>(a) | static_cast<int>(b));
}

inline constexpr CliEmitFlags operator&(CliEmitFlags a, CliEmitFlags b) {
    return static_cast<CliEmitFlags>(static_cast<int>(a) & static_cast<int>(b));
}

// Результат парсинга CLI
struct CliOptions {
    std::string input_file;
    std::string output_file;
    CliEmitFlags emit_flags = CliEmitFlags::None;
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
    bool should_compile() const { return emit_flags == CliEmitFlags::None; }
};

// Результат парсинга: опции + оставшиеся аргументы (для Options::parse_argv)
struct CliParseResult {
    CliOptions opts;
    std::vector<std::string> remaining_args;
    int exit_code = 0; // 0 = OK, 1 = error
};

// Распечатать usage
void print_usage(const char *prog);

// Распечатать версию
void print_version();

// Распечатать все опции в виде строк (для проверки пересечений)
[[nodiscard]] std::vector<std::string> get_all_cli_option_names();

// Парсинг аргументов
// Возвращает CliParseResult с распознанными CLI-опциями и оставшимися аргументами
CliParseResult parse_cli_args(int argc, char *argv[]);

// Перегрузка для span
CliParseResult parse_cli_args(std::span<char *> argv);

// Встроенная справка: true если --help или --version
inline bool is_special_exit(const CliParseResult &r) {
    return r.opts.help_requested || r.opts.version_requested || r.exit_code != 0;
}

} // namespace trust