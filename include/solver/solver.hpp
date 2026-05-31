#pragma once

#include "solver/smt_ast.hpp"

#include <memory>
#include <string>
#include <vector>

namespace trust {
namespace solver {

/// Result of an SMT solver check
enum class SolverResult {
    kSat,         ///< formula is satisfiable
    kUnsat,       ///< formula is unsatisfiable
    kUnknown,     ///< solver could not decide
    kError,       ///< solver error
    kUnsupported, ///< no solver available (stub)
};

/// Abstract interface for SMT solver backends
class SolverInterface {
  public:
    virtual ~SolverInterface() = default;

    /// Set the logic (e.g. "QF_LIA", "AUFLIA")
    virtual void setLogic(const std::string& logic) = 0;

    /// Declare a sort (uninterpreted sort)
    virtual void declareSort(const std::string& name, uint32_t arity) = 0;

    /// Declare a function
    virtual void declareFun(const std::string& name, const std::vector<SmtSort>& arg_sorts, const SmtSort& result_sort) = 0;

    /// Define a function (with body)
    virtual void defineFun(const std::string& name, const std::vector<SmtSort>& arg_sorts, const SmtSort& result_sort, const SmtTerm& body) = 0;

    /// Assert a formula
    virtual void assertFormula(const SmtTerm& formula) = 0;

    /// Check satisfiability of current assertions
    virtual SolverResult checkSat() = 0;

    /// Push/pop stack
    virtual void push(uint32_t depth = 1) = 0;
    virtual void pop(uint32_t depth = 1) = 0;

    /// Get the SMT-LIB 2 text of all commands so far (for export)
    virtual std::string getSmtLibText() const = 0;

    /// Reset the solver
    virtual void reset() = 0;
};

/// Create a solver instance.
/// With WITH_SOLVER=ON returns SolverZ3, otherwise returns SolverStub.
std::unique_ptr<SolverInterface> createSolver();

} // namespace solver
} // namespace trust