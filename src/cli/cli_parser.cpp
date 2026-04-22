#include "cli/cli_parser.hpp"

#include <iostream>

namespace trust {

constexpr std::string_view kVersion = "0.1.0";

void print_usage(const char *prog) {
    std::cout << "Usage: " << prog << " <input.trust> [options]\n"
              << "\n"
              << "Options:\n";

    const int max_width = 20;
    for (int i = 0; i < NumCliOptions; ++i) {
        auto &m = all_cli_opts[i];
        if (m.kind == CliOptKind::Positional)
            continue;

        std::string opt_str;
        if (!m.short_name.empty()) {
            opt_str += "-";
            opt_str += m.short_name;
        }
        if (!m.short_name.empty() && !m.long_name.empty()) {
            opt_str += ", ";
        }
        if (!m.long_name.empty()) {
            opt_str += "--";
            opt_str += m.long_name;
        }

        if (m.kind == CliOptKind::WithArg && !m.arg_name.empty()) {
            opt_str += " <";
            opt_str += m.arg_name;
            opt_str += ">";
        }

        int padding = max_width - static_cast<int>(opt_str.size());
        if (padding < 1)
            padding = 1;

        std::cout << "  " << opt_str;
        for (int j = 0; j < padding; ++j)
            std::cout << ' ';
        std::cout << m.description << "\n";
    }
    std::cout << "\n"
              << "Positional arguments:\n";
    for (int i = 0; i < NumCliOptions; ++i) {
        auto &m = all_cli_opts[i];
        if (m.kind == CliOptKind::Positional) {
            std::cout << "  <" << m.name << ">" << m.description << "\n";
        }
    }
}

void print_version() {
    std::cout << "trust " << kVersion << "\n";
}

std::vector<std::string> get_all_cli_option_names() {
    std::vector<std::string> result;
    for (int i = 0; i < NumCliOptions; ++i) {
        auto &m = all_cli_opts[i];
        if (!m.long_name.empty())
            result.push_back("--" + std::string(m.long_name));
        if (!m.short_name.empty())
            result.push_back("-" + std::string(m.short_name));
    }
    return result;
}

static void apply_cli_opt(CliOptions &opts, CliOpt opt) {
    switch (opt) {
    case CliOpt::Help:
        opts.help_requested = true;
        break;
    case CliOpt::Version:
        opts.version_requested = true;
        break;
    case CliOpt::Verbose:
        opts.verbose = true;
        break;
    case CliOpt::Quiet:
        opts.quiet = true;
        break;
    case CliOpt::EmitTokens:
        opts.emit_flags = opts.emit_flags | CliEmitFlags::Tokens;
        break;
    case CliOpt::EmitAST:
        opts.emit_flags = opts.emit_flags | CliEmitFlags::AST;
        break;
    case CliOpt::EmitCpp:
        opts.emit_flags = opts.emit_flags | CliEmitFlags::Cpp;
        break;
    case CliOpt::EmitModule:
        opts.emit_flags = opts.emit_flags | CliEmitFlags::Module;
        break;
    case CliOpt::ObjectFile:
        opts.compile_to_object = true;
        break;
    case CliOpt::StaticLib:
        opts.compile_to_static_lib = true;
        break;
    case CliOpt::SharedLib:
        opts.compile_to_shared_lib = true;
        break;
    case CliOpt::NoBindingHeader:
        opts.gen_binding_header = false;
        opts.binding_header_explicitly_set = true;
        break;
    case CliOpt::BindingHeader:
        opts.gen_binding_header = true;
        opts.binding_header_explicitly_set = true;
        break;
    default:
        break;
    }
}

static void set_comp_option(CliOptions &opts, CliOpt opt, std::string_view value) {
    switch (opt) {
    case CliOpt::TempDir:
        opts.temp_dir = std::string(value);
        break;
    case CliOpt::Compiler:
        opts.compiler = std::string(value);
        break;
    case CliOpt::CompileOpts:
        opts.compiler_options = std::string(value);
        break;
    default:
        break;
    }
}

static void set_output_file(CliOptions &opts, std::string_view value) {
    opts.output_file = std::string(value);
}

CliParseResult parse_cli_args(int argc, char *argv[]) {
    CliParseResult result;
    // Set default compiler from CMake config
    result.opts.compiler = TRUST_DEFAULT_COMPILER;

    int idx = 1;
    while (idx < argc) {
        std::string_view arg = argv[idx];
        ++idx;

        if (arg == "-h" || arg == "--help") {
            result.opts.help_requested = true;
            continue;
        }
        if (arg == "--version") {
            result.opts.version_requested = true;
            continue;
        }

        // Длинная опция: --name или --name=value
        if (arg.starts_with("--")) {
            auto name = arg.substr(2);
            auto eq_pos = name.find('=');
            std::string_view opt_name = name.substr(0, eq_pos);
            bool has_value = eq_pos != std::string_view::npos;

            CliOpt opt = cli_opt_from_long(opt_name);
            auto meta = get_cli_opt_meta(opt);

            if (meta.kind == CliOptKind::Flag) {
                apply_cli_opt(result.opts, opt);
                // Handle --binding-header=FILE (flag with optional value via '=')
                if (opt == CliOpt::BindingHeader && has_value) {
                    result.opts.binding_header_file = std::string(name.substr(eq_pos + 1));
                }
            } else if (meta.kind == CliOptKind::WithArg) {
                std::string_view arg_value;
                if (has_value) {
                    arg_value = name.substr(eq_pos + 1);
                } else if (idx < argc) {
                    arg_value = argv[idx];
                    ++idx;
                } else {
                    std::cerr << "error: --" << opt_name << " requires an argument\n";
                    result.exit_code = 1;
                    return result;
                }
                if (opt == CliOpt::Output) {
                    set_output_file(result.opts, arg_value);
                } else {
                    set_comp_option(result.opts, opt, arg_value);
                }
            } else {
                // Позиционная — ошибка при попытке задать через --
                std::cerr << "error: unknown option: --" << opt_name << "\n";
                result.exit_code = 1;
                return result;
            }
            continue;
        }

        // Короткая опция: -X
        if (arg.starts_with("-") && arg.size() > 1) {
            auto name = arg.substr(1);

            CliOpt opt = cli_opt_from_short(name);
            auto meta = get_cli_opt_meta(opt);

            if (meta.kind == CliOptKind::Flag) {
                apply_cli_opt(result.opts, opt);
            } else if (meta.kind == CliOptKind::WithArg) {
                std::string_view arg_value;
                if (arg.size() > 2) {
                    arg_value = name.substr(1);
                } else if (idx < argc) {
                    arg_value = argv[idx];
                    ++idx;
                } else {
                    std::cerr << "error: -" << name << " requires an argument\n";
                    result.exit_code = 1;
                    return result;
                }
                if (opt == CliOpt::Output) {
                    set_output_file(result.opts, arg_value);
                } else {
                    set_comp_option(result.opts, opt, arg_value);
                }
            } else {
                // Не распознано — передаём дальше
                result.remaining_args.push_back(std::string(arg));
            }
            continue;
        }

        // Позиционный аргумент (файл)
        if (result.opts.input_file.empty()) {
            result.opts.input_file = std::string(arg);
        } else {
            result.remaining_args.push_back(std::string(arg));
        }
    }

    // Специальный выход
    if (result.opts.help_requested && result.exit_code == 0) {
        print_usage(argv[0]);
    } else if (result.opts.version_requested && result.exit_code == 0) {
        print_version();
    } else if (result.opts.input_file.empty() && result.exit_code == 0) {
        std::cerr << "error: no input file specified\n";
        print_usage(argv[0]);
        result.exit_code = 1;
    }

    // Auto-enable binding header for library builds (only if not explicitly disabled)
    if (result.exit_code == 0 && (result.opts.compile_to_static_lib || result.opts.compile_to_shared_lib)) {
        if (!result.opts.binding_header_explicitly_set || result.opts.gen_binding_header) {
            result.opts.gen_binding_header = true;
        }
    }

    // Warn about compile options being used with emit flags
    if (result.exit_code == 0 && !result.opts.should_compile()) {
        if (!result.opts.temp_dir.empty())
            std::cerr << "warning: --temp-dir is ignored when using emit flags\n";
        if (result.opts.compiler != TRUST_DEFAULT_COMPILER)
            std::cerr << "warning: --compiler is ignored when using emit flags\n";
        if (!result.opts.compiler_options.empty())
            std::cerr << "warning: --options is ignored when using emit flags\n";
        if (result.opts.compile_to_object)
            std::cerr << "warning: -c is ignored when using emit flags\n";
        if (result.opts.compile_to_static_lib)
            std::cerr << "warning: -a is ignored when using emit flags\n";
        if (result.opts.compile_to_shared_lib)
            std::cerr << "warning: -l is ignored when using emit flags\n";
    }

    return result;
}

CliParseResult parse_cli_args(std::span<char *> argv) {
    if (argv.empty())
        return {};
    return parse_cli_args(static_cast<int>(argv.size()), argv.data());
}

} // namespace trust