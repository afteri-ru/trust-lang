#include "cli/cli.hpp"
#include "cli/options.h"
#include "gencpp/ast_builder.hpp"
#include "gencpp/cpp_generator.hpp"
#include "parser/lexer.hpp"
#include "parser/mmproc.hpp"
#include "parser/parser.hpp"

#include <cstdlib>
#include <filesystem>
#include <fstream>
#include <iostream>
#include <string>
#include <variant>

namespace trust {

Cli::Cli(CliParseResult result) : result_(std::move(result)), ctx_() {
    ctx_.diag().setOutput(&std::cerr);
    ctx_.diag().setMinSeverity(result_.opts.quiet ? Severity::Error : Severity::Remark);
}

int Cli::run() {
    // Validate input file
    if (!std::filesystem::exists(result_.opts.input_file)) {
        std::cerr << "error: input file not found: " << result_.opts.input_file << "\n";
        return 1;
    }

    if (result_.opts.should_compile()) {
        return run_compile();
    }

    return run_emit();
}

int Cli::run_emit() {
    auto src_idx = ctx_.load_file(result_.opts.input_file);

    if (result_.opts.verbose) {
        std::cerr << "info: loaded " << result_.opts.input_file << "\n";
    }

    // Lex → MMProcess → Build Program → Generate C++
    auto lexemes = Lexer::tokenize(ctx_, src_idx);
    auto ast_nodes = MMProcessor::process(ctx_, lexemes);

    // Build Program from AST nodes (separate Decl vs other nodes)
    AstBuilder builder;
    std::vector<std::unique_ptr<Decl>> items;
    std::vector<AstNodeBase *> embed_nodes;
    for (auto &node : ast_nodes) {
        if (auto *decl = dynamic_cast<Decl *>(node.get())) {
            items.push_back(std::unique_ptr<Decl>(static_cast<Decl *>(node.release())));
        } else {
            embed_nodes.push_back(node.get());
        }
    }
    auto program = builder.program(std::move(items));

    // Generate C++ code
    CppGenerator generator;
    std::string output = generator.generate(program.get());

    // Emit embed-only nodes directly
    if (!embed_nodes.empty() && items.empty()) {
        output.clear();
        for (auto *node : embed_nodes) {
            if (auto *embed = dynamic_cast<EmbedExpr *>(node)) {
                output += embed->value() + "\n";
            }
        }
    }

    // Write output
    if (!result_.opts.output_file.empty()) {
        std::ofstream ofs(result_.opts.output_file);
        if (!ofs.is_open()) {
            std::cerr << "error: cannot open output file: " << result_.opts.output_file << "\n";
            return 1;
        }
        ofs << output;
    } else {
        std::cout << output;
    }

    return 0;
}

enum class CompileMode {
    Executable,
    ObjectFile,
    StaticLib,
    SharedLib,
};

int Cli::run_compile() {
    auto src_idx = ctx_.load_file(result_.opts.input_file);

    if (result_.opts.verbose) {
        std::cerr << "info: loaded " << result_.opts.input_file << "\n";
    }

    // Lex → MMProcess → Build Program → Generate C++
    auto lexemes = Lexer::tokenize(ctx_, src_idx);
    auto ast_nodes = MMProcessor::process(ctx_, lexemes);

    // Build Program from AST nodes (separate Decl vs other nodes)
    AstBuilder builder;
    std::vector<std::unique_ptr<Decl>> items;
    std::string embed_output;
    for (auto &node : ast_nodes) {
        if (auto *decl = dynamic_cast<Decl *>(node.get())) {
            items.push_back(std::unique_ptr<Decl>(static_cast<Decl *>(node.release())));
        } else if (auto *embed = dynamic_cast<EmbedExpr *>(node.get())) {
            embed_output += embed->value() + "\n";
        }
    }

    // Determine compile mode
    CompileMode mode = CompileMode::Executable;
    if (result_.opts.compile_to_static_lib) {
        mode = CompileMode::StaticLib;
    } else if (result_.opts.compile_to_shared_lib) {
        mode = CompileMode::SharedLib;
    } else if (result_.opts.compile_to_object) {
        mode = CompileMode::ObjectFile;
    }

    // Determine output file first (needed for binding header path)
    std::filesystem::path output_path;
    if (!result_.opts.output_file.empty()) {
        output_path = result_.opts.output_file;
    } else {
        auto stem = std::filesystem::path(result_.opts.input_file).stem();
        switch (mode) {
        case CompileMode::ObjectFile:
            output_path = stem.string() + ".o";
            break;
        case CompileMode::StaticLib:
            output_path = "lib" + stem.string() + ".a";
            break;
        case CompileMode::SharedLib:
            output_path = "lib" + stem.string() + ".so";
            break;
        default:
            output_path = stem.string();
            break;
        }
    }

    // Determine binding header parameters (default: next to output file)
    std::string guard;
    std::filesystem::path header_path;
    if (result_.opts.gen_binding_header) {
        if (!result_.opts.binding_header_file.empty()) {
            header_path = result_.opts.binding_header_file;
        } else {
            header_path = output_path;
            header_path.replace_extension(".h");
        }
        guard = std::string("_") + header_path.stem().string() + "_H_";
    }

    // Generate C++ code and binding header
    std::string cpp_source;
    std::string binding_header;
    if (!items.empty()) {
        auto program = builder.program(std::move(items));
        CppGenerator generator;
        cpp_source = generator.generate(program.get(), result_.opts.gen_binding_header ? &binding_header : nullptr,
                                        result_.opts.gen_binding_header ? guard.c_str() : nullptr);
    }

    // If only embed nodes, use them directly and generate simple binding header
    if (cpp_source.empty() && !embed_output.empty()) {
        cpp_source = embed_output;

        // Generate binding header from embed code by parsing simple function signatures
        if (result_.opts.gen_binding_header) {
            std::ostringstream hdr;
            hdr << "/* Auto-generated binding header */\n";
            hdr << "#ifndef " << guard << "\n";
            hdr << "#define " << guard << "\n";
            hdr << "\n";

            // Simple regex-like parsing of embed code for function definitions
            std::string code = embed_output;
            // Find patterns like: <type> <name>(<params>) {
            size_t pos = 0;
            while ((pos = code.find("(", pos)) != std::string::npos) {
                // Look backwards for function name and return type
                size_t paren_open = pos;
                size_t name_end = paren_open;

                // Skip whitespace before (
                while (name_end > 0 && std::isspace(code[name_end - 1]))
                    --name_end;

                // Find function name
                size_t name_start = name_end;
                while (name_start > 0 && (std::isalnum(code[name_start - 1]) || code[name_start - 1] == '_'))
                    --name_start;

                if (name_start < name_end) {
                    std::string func_name = code.substr(name_start, name_end - name_start);

                    // Skip common non-function patterns
                    if (func_name != "if" && func_name != "while" && func_name != "for" && func_name != "switch" && func_name != "return") {
                        // Find return type (simplified - just grab word before name)
                        size_t type_end = name_start;
                        while (type_end > 0 && std::isspace(code[type_end - 1]))
                            --type_end;

                        size_t type_start = type_end;
                        while (type_start > 0 && (std::isalnum(code[type_start - 1]) || code[type_start - 1] == '_' || code[type_start - 1] == '*' ||
                                                  code[type_start - 1] == '&'))
                            --type_start;

                        std::string ret_type = code.substr(type_start, type_end - type_start);
                        if (ret_type.empty())
                            ret_type = "int";

                        // Get parameter string
                        size_t paren_close = code.find(")", paren_open + 1);
                        if (paren_close != std::string::npos) {
                            std::string params = code.substr(paren_open + 1, paren_close - paren_open - 1);

                            // Check if there's a { after )
                            size_t brace_pos = paren_close + 1;
                            while (brace_pos < code.size() && std::isspace(code[brace_pos]))
                                ++brace_pos;

                            if (brace_pos < code.size() && code[brace_pos] == '{') {
                                hdr << ret_type << " " << func_name << "(" << params << ");\n";
                            }
                        }
                    }
                }
                ++pos;
            }

            hdr << "\n";
            hdr << "#endif /* " << guard << " */\n";
            binding_header = hdr.str();
        }
    }

    // Determine temp directory
    std::filesystem::path temp_dir;
    if (!result_.opts.temp_dir.empty()) {
        temp_dir = result_.opts.temp_dir;
    } else {
        temp_dir = std::filesystem::temp_directory_path();
    }

    if (!std::filesystem::exists(temp_dir)) {
        std::filesystem::create_directories(temp_dir);
    }

    // Write temporary C++ file
    auto temp_cpp = temp_dir / "trust_temp.cpp";
    {
        std::ofstream ofs(temp_cpp);
        if (!ofs.is_open()) {
            std::cerr << "error: cannot open temp file: " << temp_cpp << "\n";
            return 1;
        }
        ofs << cpp_source;
    }

    // Write binding header if needed (next to output file by default)
    if (result_.opts.gen_binding_header && !binding_header.empty()) {
        std::ofstream hdr(header_path);
        if (!hdr.is_open()) {
            std::cerr << "error: cannot open binding header file: " << header_path << "\n";
            return 1;
        }
        hdr << binding_header;
    }

    // Build compiler command
    std::string cmd = result_.opts.compiler;
    cmd += " ";

    switch (mode) {
    case CompileMode::ObjectFile:
        cmd += "-c ";
        break;
    case CompileMode::SharedLib:
        cmd += "-fPIC -shared ";
        break;
    default:
        break;
    }

    if (!result_.opts.compiler_options.empty()) {
        cmd += result_.opts.compiler_options;
        cmd += " ";
    }

    cmd += temp_cpp.string();
    cmd += " -o ";

    // For static lib, compile to object first, then archive
    std::filesystem::path object_file;
    if (mode == CompileMode::StaticLib) {
        object_file = temp_dir / "trust_temp.o";
        std::string compile_cmd = cmd;
        compile_cmd += temp_cpp.string();
        compile_cmd += " -fPIC -c -o ";
        compile_cmd += object_file.string();

        if (result_.opts.verbose) {
            std::cerr << "Running: " << compile_cmd << "\n";
        }

        int status = std::system(compile_cmd.c_str());
        if (status != 0) {
            std::cerr << "error: compilation failed with exit code " << status << "\n";
            return 1;
        }

        // Create static library with ar
        std::string ar_cmd = "ar rcs ";
        ar_cmd += output_path.string();
        ar_cmd += " ";
        ar_cmd += object_file.string();

        if (result_.opts.verbose) {
            std::cerr << "Running: " << ar_cmd << "\n";
        }

        status = std::system(ar_cmd.c_str());
        if (status != 0) {
            std::cerr << "error: ar failed with exit code " << status << "\n";
            return 1;
        }

        return 0;
    } else {
        cmd += output_path.string();
    }

    if (result_.opts.verbose) {
        std::cerr << "Running: " << cmd << "\n";
    }

    int status = std::system(cmd.c_str());
    if (status != 0) {
        std::cerr << "error: compilation failed with exit code " << status << "\n";
        return 1;
    }

    return 0;
}

} // namespace trust