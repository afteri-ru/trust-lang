#pragma once

#include "diag/context.hpp"
#include "gencpp/cpp_generator.hpp"
#include <filesystem>
#include <fstream>
#include <iostream>
#include <memory>
#include <optional>
#include <string>

namespace trust {

struct CliOptions {
    std::string input_file;
    std::string output_file;
    bool show_help = false;
    bool show_version = false;
};

class Cli {
public:
    explicit Cli(CliOptions opts);
    ~Cli() = default;

    // Main entry point: returns 0 on success, non-zero on error
    int run();

private:
    CliOptions opts_;
    Context ctx_;
};

} // namespace trust