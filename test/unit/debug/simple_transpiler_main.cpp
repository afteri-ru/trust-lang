#include "lsp/transpile.h"
#include "utils/utils.hpp"

#include <cassert>
#include <cstdlib>
#include <fstream>
#include <iostream>
#include <string>
#include <vector>
#include <filesystem>

using namespace trust::lsp;

namespace fs = std::filesystem;

// ── Вспомогательные функции для сериализации ──

// Генерирует C++ код для внедрения бинарного map в секцию .debug_trust_map
static std::string generate_embedded_map_code(const std::vector<unsigned char>& data) {
    std::string code = "\n// Embedded source map data\n";
    code += "#if defined(__ELF__)\n// clang-format off\n";
    code += "__attribute__((used, section(\".debug_trust_map\")))\n";
    code += "static const unsigned char __trust_map_data[] = {\n";
    for (size_t i = 0; i < data.size(); ++i) {
        if (i % 10 == 0)
            code += "  ";
        code += "0x" + std::format("{:02x}", data[i]);
        if (i + 1 < data.size())
            code += ", ";
        if ((i + 1) % 10 == 0 && i + 1 < data.size())
            code += "\n";
    }
    code += "\n};\n// clang-format on\n";
    code += "#endif // __ELF__\n";
    return code;
}

// Записывает бинарный map в файл
static bool write_map_file(const std::vector<unsigned char>& data, const std::string& path) {
    std::ofstream out(path, std::ios::binary);
    if (!out.is_open())
        return false;
    out.write(reinterpret_cast<const char*>(data.data()), static_cast<std::streamsize>(data.size()));
    return out.good();
}

static void print_help(const char* prog) {
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

int main(int argc, char** argv) {
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

    std::string trust_code((std::istreambuf_iterator<char>(infile)), std::istreambuf_iterator<char>());
    infile.close();

    // Создаём Context и транслируем
    std::string trust_name = fs::path(src_path).filename().string();
    std::string cpp_name = fs::path(cpp_path).filename().string();

    trust::Context ctx;
    auto [trustIdx, cppIdx] = transpile(trust_code, trust_name, cpp_name, ctx);

    std::string cpp_result = ctx.output_result(cppIdx);

    // Pack via Context serialization
    auto map_data = ctx.toReader()->packToMsgpack();

    // Generate C++ with embedded map code
    std::string cpp = cpp_result;
    cpp += generate_embedded_map_code(map_data);

    // Write C++ output
    std::ofstream outfile(cpp_path);
    if (!outfile.is_open()) {
        std::cerr << "Error: Cannot write " << cpp_path << "\n";
        return 1;
    }
    outfile << cpp;
    outfile.close();

    // Write external map file (binary) if --emit-source-map was specified
    if (!emit_source_map.empty()) {
        fs::create_directories(fs::path(emit_source_map).parent_path(), ec);
        if (ec) {
            std::cerr << "Error: Cannot create directory for " << emit_source_map << ": " << ec.message() << "\n";
            return 1;
        }
        if (write_map_file(map_data, emit_source_map)) {
            std::cout << "Map file: " << emit_source_map << " (" << map_data.size() << " bytes)\n";
        } else {
            std::cerr << "Error: Cannot write map file " << emit_source_map << "\n";
            return 1;
        }
    }

    std::cout << "Transpiled: " << src_path << " -> " << cpp_path << "\n";
    std::cout << "Mappings: " << ctx.file_count() << " files\n";

    return 0;
}