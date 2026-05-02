import trust;

#include "ast_format_parser.hpp"

#include <fstream>
#include <iostream>
#include <sstream>
#include <string>

// Test-only helper classes (moved out of module)
namespace {

struct GenCppResult {
    bool success;
    std::string ast_output;
    std::string cpp_code;
    std::string error_message;
};

class GenCpp {
  public:
    GenCpp(trust::Context& ctx) : ctx_(ctx) {}
    ~GenCpp() = default;

    void set_program(std::unique_ptr<trust::Program> program) {
        program_ = std::move(program);
        parsed_ = (program_ != nullptr);
        built_ = parsed_;
    }

    std::string get_ast_text() const {
        if (!built_ || !program_) return "";
        return trust::print_ast(program_.get());
    }

    std::string generate_cpp_code() const {
        if (!built_ || !program_) return "";
        trust::CppGenerator generator;
        return generator.generate(program_.get());
    }

    size_t get_root_count() const { return parsed_ ? 1 : 0; }
    const trust::Program* get_program() const { return program_.get(); }
    void set_source_file(const std::string& file) { source_file_ = file; }
    const std::string& get_source_file() const { return source_file_; }
    trust::Context& context() { return ctx_; }

  private:
    trust::Context& ctx_;
    std::unique_ptr<trust::Program> program_;
    std::string source_file_;
    bool parsed_ = false;
    bool built_ = false;
};

} // anonymous namespace

int main(int argc, char* argv[]) {
    if (argc < 2) {
        std::cerr << "Usage: gencpp_from_ast <input.ast>\n";
        return 1;
    }

    std::string input_file = argv[1];

    std::ifstream file(input_file);
    if (!file.is_open()) {
        std::cerr << "Cannot open: " << input_file << "\n";
        return 1;
    }

    // Read source AST text
    std::string source_text((std::istreambuf_iterator<char>(file)), std::istreambuf_iterator<char>());
    file.close();

    // Create diagnostic context
    trust::Context ctx;
    ctx.diag().setOutput(&std::cerr);
    ctx.diag().setMinSeverity(trust::Severity::Remark);

    // Use test parser to parse and process
    auto parsed = trust::parse_ast_format(source_text, ctx);
    if (!parsed.has_value()) {
        std::cerr << "Failed to parse AST from: " << input_file << "\n";
        return 1;
    }

    std::vector<trust::ParsedNode*> root_ptrs;
    for (auto& r : *parsed) root_ptrs.push_back(r.get());

    auto program = trust::build_ast_from_roots(root_ptrs, ctx);
    if (!program) {
        std::cerr << "Failed to build Program from: " << input_file << "\n";
        return 1;
    }

    // Use GenCpp helper class to generate output
    GenCpp gen(ctx);
    gen.set_source_file(input_file);
    gen.set_program(std::move(program));

    // Print AST structure (with indentation)
    std::string ast_text = gen.get_ast_text();

    // Generate C++ code
    std::string cpp_code = gen.generate_cpp_code();

    std::cout << cpp_code;

    // Append AST as comment at the end of generated C++ code
    std::cout << "\n// === Input AST Structure ===\n";
    std::cout << "// Source: " << input_file << "\n";
    std::istringstream ss(ast_text);
    std::string line;
    while (std::getline(ss, line)) {
        std::cout << "// " << line << "\n";
    }
    std::cout << "// === End AST ===\n";

    // AST dump to stderr for debugging
    std::cerr << "// Transpiled: " << input_file << "\n";

    return 0;
}