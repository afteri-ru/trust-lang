#pragma once

#include "cli/options.h"

#include <algorithm>
#include <array>
#include <string_view>

namespace trust {

enum class CliOptKind {
    Flag,      // без аргумента: -v, --verbose
    WithArg,   // с аргументом: -o file, --output file
    Positional // позиционный: input.trust
};

struct CliOptMeta {
    std::string_view name; // идентификатор (для кода): "Input", "Output", ...
    CliOptKind kind;
    std::string_view short_name; // "v", "o", ... (без "-")
    std::string_view long_name;  // "verbose", "output", ... (без "--")
    std::string_view arg_name;   // "file" для WithArg, иначе ""
    std::string_view description;
};

// X-macro — единственный источник CLI-опций.
// Формат: M(Ident, EnumName, Kind, ShortName, LongName, ArgName, Description)
#ifndef TRUST_CLI_OPTIONS
#define TRUST_CLI_OPTIONS(M)                                                                       \
    M(0, Input, Positional, "", "", "", "Input source file")                                       \
    M(1, Output, WithArg, "o", "output", "file", "Output file (default: stdout)")                  \
    M(2, Verbose, Flag, "v", "verbose", "", "Verbose output")                                      \
    M(3, Quiet, Flag, "q", "quiet", "", "Quiet mode (suppress warnings)")                          \
    M(4, EmitTokens, Flag, "", "emit-tokens", "", "Print tokens after lexing")                     \
    M(5, EmitAST, Flag, "", "emit-ast", "", "Print AST after parsing")                             \
    M(6, EmitCpp, Flag, "", "emit-cpp", "", "Emit C++ source file")                                \
    M(7, EmitModule, Flag, "", "emit-module", "", "Emit C++ module file")                          \
    M(8, Help, Flag, "h", "help", "", "Show this help message")                                    \
    M(9, Version, Flag, "", "version", "", "Show version information")                             \
    M(10, TempDir, WithArg, "", "temp-dir", "dir", "Temporary directory for intermediate files")   \
    M(11, Compiler, WithArg, "", "compiler", "path", "Compiler path (default: from CMake config)") \
    M(12, CompileOpts, WithArg, "", "options", "opts", "Additional compiler options (quoted)")     \
    M(13, ObjectFile, Flag, "c", "", "", "Compile to object file (.o)")                            \
    M(14, StaticLib, Flag, "a", "static-lib", "", "Compile to static library (.a)")                \
    M(15, SharedLib, Flag, "l", "shared-lib", "", "Compile to shared library (.so)")               \
    M(16, BindingHeader, Flag, "b", "binding-header", "", "Generate binding header file")          \
    M(17, NoBindingHeader, Flag, "", "no-binding-header", "", "Disable binding header generation")
#endif

// Числовой enum
enum class CliOpt : int {
#define CLI_DEF(idx, ename, kind, sn, ln, an, desc) ename = idx,
    TRUST_CLI_OPTIONS(CLI_DEF)
#undef CLI_DEF
};

// Количество
static constexpr int NumCliOptions = 18;

// Constexpr массив всех метаданных
static constexpr std::array<CliOptMeta, NumCliOptions> all_cli_opts = {
#define CLI_DEF_ARRAY(idx, ename, kind, sn, ln, an, desc) CliOptMeta{#ename, CliOptKind::kind, sn, ln, an, desc},
    TRUST_CLI_OPTIONS(CLI_DEF_ARRAY)
#undef CLI_DEF_ARRAY
};

inline constexpr CliOptMeta get_cli_opt_meta(CliOpt opt) noexcept {
    auto idx = static_cast<int>(opt);
    if (idx >= 0 && idx < NumCliOptions)
        return all_cli_opts[idx];
    return CliOptMeta{};
}

inline constexpr bool is_valid_cli_opt(CliOpt opt) noexcept {
    auto idx = static_cast<int>(opt);
    return idx >= 0 && idx < NumCliOptions;
}

inline constexpr CliOpt cli_opt_from_short(std::string_view name) noexcept {
    for (int i = 0; i < NumCliOptions; ++i) {
        auto &m = all_cli_opts[i];
        if (!m.short_name.empty() && m.short_name == name)
            return static_cast<CliOpt>(i);
    }
    return CliOpt::Input;
}

inline constexpr CliOpt cli_opt_from_long(std::string_view name) noexcept {
    for (int i = 0; i < NumCliOptions; ++i) {
        auto &m = all_cli_opts[i];
        if (!m.long_name.empty() && m.long_name == name)
            return static_cast<CliOpt>(i);
    }
    return CliOpt::Input;
}

inline constexpr CliOpt cli_opt_from_name(std::string_view name) noexcept {
    if (auto r = cli_opt_from_short(name); r != CliOpt::Input)
        return r;
    return cli_opt_from_long(name);
}

} // namespace trust