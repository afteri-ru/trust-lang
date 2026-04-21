#include "cli/cli.hpp"

#include <iostream>
#include <string_view>

static constexpr std::string_view kVersion = "0.1.0";

static void print_usage(const char *prog) {
    std::cout << "Usage: " << prog << " <input.trust> [options]\n"
              << "\n"
              << "Options:\n"
              << "  -o <file>   Output file (default: stdout)\n"
              << "  -h          Show this help message\n"
              << "  --version   Show version information\n";
}

int main(int argc, char *argv[]) {
    trust::CliOptions opts;

    // Parse arguments
    int i = 1;
    for (; i < argc; ++i) {
        std::string_view arg = argv[i];

        if (arg == "-h" || arg == "--help") {
            opts.show_help = true;
            print_usage(argv[0]);
            return 0;
        } else if (arg == "--version") {
            opts.show_version = true;
            std::cout << "trust " << kVersion << "\n";
            return 0;
        } else if (arg == "-o") {
            if (i + 1 >= argc) {
                std::cerr << "error: -o requires an argument\n";
                return 1;
            }
            opts.output_file = argv[++i];
        } else if (arg.starts_with("-")) {
            std::cerr << "error: unknown option: " << arg << "\n";
            return 1;
        } else {
            // Positional argument: input file
            if (opts.input_file.empty()) {
                opts.input_file = arg;
            } else {
                std::cerr << "error: multiple input files not supported\n";
                return 1;
            }
        }
    }

    if (opts.input_file.empty()) {
        std::cerr << "error: no input file specified\n";
        print_usage(argv[0]);
        return 1;
    }

    trust::Cli cli(std::move(opts));
    return cli.run();
}