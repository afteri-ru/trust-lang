#include "cli/cli.hpp"

#include "gencpp/cpp_generator.hpp"

#include <filesystem>
#include <fstream>
#include <iostream>
#include <sstream>
#include <string>

namespace trust {

Cli::Cli(CliOptions opts) : opts_(std::move(opts)), ctx_() {
    ctx_.diag().setOutput(&std::cerr);
    ctx_.diag().setMinSeverity(Severity::Remark);
}

int Cli::run() {
    // 1. Validate input file
    if (!std::filesystem::exists(opts_.input_file)) {
        std::cerr << "error: input file not found: " << opts_.input_file << "\n";
        return 1;
    }

    // 2. Read input file
    std::ifstream ifs(opts_.input_file);
    if (!ifs.is_open()) {
        std::cerr << "error: cannot open file: " << opts_.input_file << "\n";
        return 1;
    }

    std::string source_text((std::istreambuf_iterator<char>(ifs)),
                            std::istreambuf_iterator<char>());
    ifs.close();

    // 3. Load source into context
    auto sidx = ctx_.load_file(opts_.input_file);
    (void)sidx; // used internally by diagnostics

    // 4. TODO: Parse source -> Program
    // For now, report that parsing is not yet implemented
    std::cerr << "warning: parser integration is pending\n";
    std::cerr << "input file loaded: " << opts_.input_file
              << " (" << source_text.size() << " bytes)\n";

    // 5. TODO: Semantic analysis
    // 6. TODO: Code generation
    // 7. Write output
    if (!opts_.output_file.empty()) {
        std::ofstream ofs(opts_.output_file);
        if (!ofs.is_open()) {
            std::cerr << "error: cannot open output file: " << opts_.output_file << "\n";
            return 1;
        }
        ofs << "// Generated output placeholder\n";
        ofs.close();
    } else {
        std::cout << "// Generated output placeholder\n";
    }

    return 0;
}

} // namespace trust