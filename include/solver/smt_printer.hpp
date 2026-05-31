#pragma once

#include "solver/smt_ast.hpp"

#include <string>

namespace trust {
namespace solver {

/// Converts SmtAst nodes to SMT-LIB 2 textual format.
/// Does NOT depend on Z3 — works with WITH_SOLVER=OFF.
class SmtPrinter {
  public:
    /// Print a complete SMT-LIB 2 script
    static std::string printScript(const SmtScript& script);

    /// Print a single command
    static std::string printCommand(const SmtCommand& cmd);

    /// Print a single term
    static std::string printTerm(const SmtTerm& term);

    /// Print a sort
    static std::string printSort(const SmtSort& sort);

    /// Escape an SMT-LIB 2 symbol
    static std::string escapeSymbol(const std::string& name);
};

} // namespace solver
} // namespace trust