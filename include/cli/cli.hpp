#pragma once

#include "cli/cli_parser.hpp"
#include "diag/context.hpp"
#include <filesystem>

namespace trust {

class Cli {
  public:
    explicit Cli(CliParseResult result);
    ~Cli() = default;

    int run();

  private:
    int run_emit();
    int run_compile();

    CliParseResult result_;
    Context ctx_;
};

} // namespace trust