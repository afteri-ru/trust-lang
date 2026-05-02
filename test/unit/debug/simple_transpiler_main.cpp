#include "transpiler.h"
#include "utils/utils.hpp"

#include <cassert>
#include <cstdlib>
#include <fstream>
#include <iostream>
#include <string>
#include <vector>
#include <filesystem>

namespace fs = std::filesystem;

static void print_help(const char *prog) {
    std::cerr << "Usage: " << prog << " <input_file> [options]\n"
              << "Transpile a Trust source file to C++ with embedded source map.\n"
              << "\n"
              << "Positional:\n"
              << "  <input_file>               Path to .trust source file\n"
              << "\n"
              << "Options:\n"
              << "  --emit-cpp <file>          Output C++ file path (default: auto)\n"
              << "  --temp-dir <dir>           Temporary directory for auto-generated output\n"
              << "                             (default: .trust/ next to input file)\n"
              << "  --emit-source-map <file>   Write external source map file (optional)\n"
              << "  --help, -h                 Show this help message\n"
              << "\n"
              << "Output C++ path (when --emit-cpp is not specified):\n"
              << "  Without --temp-dir:  <input_dir>/.trust/<basename>.cpp\n"
              << "  With --temp-dir:     <temp-dir>/<rel-path>/<basename>.cpp\n"
              << "    where <rel-path> is the input file's directory relative to CWD\n";
}

int main(int argc, char **argv) {
    std::string src_path;
    std::string emit_cpp;
    std::string temp_dir;
    std::string emit_source_map;
    bool help_requested = false;

    // Parse arguments manually
    for (int i = 1; i < argc; ++i) {
        std::string arg = argv[i];
        if (arg == "--help" || arg == "-h") {
            help_requested = true;
            break;
        } else if (arg == "--emit-cpp") {
            if (i + 1 >= argc) {
                std::cerr << "Error: --emit-cpp requires an argument\n";
                return 1;
            }
            emit_cpp = argv[++i];
        } else if (arg == "--temp-dir") {
            if (i + 1 >= argc) {
                std::cerr << "Error: --temp-dir requires an argument\n";
                return 1;
            }
            temp_dir = argv[++i];
        } else if (arg == "--emit-source-map") {
            if (i + 1 >= argc) {
                std::cerr << "Error: --emit-source-map requires an argument\n";
                return 1;
            }
            emit_source_map = argv[++i];
        } else if (arg[0] == '-') {
            std::cerr << "Error: Unknown option: " << arg << "\n";
            return 1;
        } else if (src_path.empty()) {
            src_path = arg;
        } else {
            std::cerr << "Error: Unexpected argument: " << arg << "\n";
            return 1;
        }
    }

    if (help_requested) {
        print_help(argv[0]);
        return 0;
    }

    if (src_path.empty()) {
        std::cerr << "Error: No input file specified\n";
        print_help(argv[0]);
        return 1;
    }

    // Determine output C++ path
    std::string cpp_path;
    if (!emit_cpp.empty()) {
        cpp_path = emit_cpp;
    } else {
        fs::path input_fs(src_path);
        fs::path input_dir = input_fs.parent_path();
        std::string basename = input_fs.stem().string();

        if (temp_dir.empty()) {
            // <input_dir>/.trust/<basename>.cpp
            cpp_path = (trust::utils::resolveTempDir(input_dir.string()) / (basename + ".cpp")).string();
        } else {
            // <temp-dir>/<rel-path-from-CWD>/<basename>.cpp
            fs::path rel_dir = fs::relative(input_dir, fs::current_path());
            cpp_path = (fs::path(temp_dir) / rel_dir / (basename + ".cpp")).string();
        }
    }

    // Create output directories
    std::error_code ec;
    fs::create_directories(fs::path(cpp_path).parent_path(), ec);
    if (ec) {
        std::cerr << "Error: Cannot create directory for " << cpp_path << ": " << ec.message() << "\n";
        return 1;
    }

    // Read source file
    std::ifstream infile(src_path);
    if (!infile.is_open()) {
        std::cerr << "Error: Cannot open " << src_path << "\n";
        return 1;
    }

    std::vector<std::string> trust_lines;
    std::string line;
    while (std::getline(infile, line)) {
        trust_lines.push_back(line);
    }
    infile.close();

    // Transpile напрямую в TrustSource
    // temp_dir задаёт cpp_directory_ для нормализации C++ путей
    // basePath = директория src-файла (не сам файл)
    trust::TrustSource source(fs::path(src_path).parent_path().string(), temp_dir);
    source.setFilePair(src_path, cpp_path);
    std::vector<std::string> cpp_lines = transpile(trust_lines, source);

    // Pack через TrustSource (msgpack)
    auto map_data = trust::TrustSource::pack(source);

    // Generate C++ with embedded map code
    std::string cpp;
    for (const auto &cl : cpp_lines) {
        cpp += cl + "\n";
    }
    cpp += trust::TrustSource::generateEmbeddedMapCode(map_data);

    // Write C++ output
    std::ofstream outfile(cpp_path);
    if (!outfile.is_open()) {
        std::cerr << "Error: Cannot write " << cpp_path << "\n";
        return 1;
    }
    outfile << cpp;
    outfile.close();

    // Write external map file (msgpack binary) if --emit-source-map was specified
    if (!emit_source_map.empty()) {
        fs::create_directories(fs::path(emit_source_map).parent_path(), ec);
        if (ec) {
            std::cerr << "Error: Cannot create directory for " << emit_source_map << ": " << ec.message() << "\n";
            return 1;
        }
        if (trust::TrustSource::writeMapFile(map_data, emit_source_map)) {
            std::cout << "Map file: " << emit_source_map << " (" << map_data.size() << " bytes)\n";
        } else {
            std::cerr << "Error: Cannot write map file " << emit_source_map << "\n";
            return 1;
        }
    }

    std::cout << "Transpiled: " << src_path << " -> " << cpp_path << "\n";
    std::cout << "Mappings: " << source.entries().size() << "\n";

    return 0;
}