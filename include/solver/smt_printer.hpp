#pragma once

#include "solver/smt_ast.hpp"

#include <string>
#include <string_view>

namespace trust {
class Context;
} // namespace trust

namespace trust {
namespace solver {

/// Converts SmtAst nodes to SMT-LIB 2 textual format.
/// Does NOT depend on Z3 - works with WITH_SOLVER=OFF.
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

    /// Строит текстовый .smt2.map: маппинг SMT-символов и (assert ...) на trust-источник
    /// (файл/диапазон/имя). smt2_text - уже напечатанный .smt2 (для диапазонов в .smt2).
    /// По аналогии с .src_map/.cppt.map, но в человекочитаемом текстовом формате.
    static std::string buildSmt2Map(const Context& ctx, const SmtScript& script, std::string_view smt2_text);
};

} // namespace solver
} // namespace trust