#pragma once

#include "pipeline/pipeline_parser.hpp"
#include "diag/context.hpp"
#include <filesystem>

namespace trust {

class Pipeline {
  public:
    static int main(int argc, char* argv[], char* envp[]);

  private:
    static int run_emit(const ParseResult& result, Context& ctx);
    static int run_compile(const ParseResult& result, Context& ctx);
};

} // namespace trust