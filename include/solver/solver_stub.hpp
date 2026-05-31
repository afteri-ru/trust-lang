#pragma once

#include "solver/solver.hpp"

#include <string>
#include <vector>

namespace trust {
namespace solver {

/// Stub solver — always returns kUnsupported.
/// Used when WITH_SOLVER=OFF.
class SolverStub final : public SolverInterface {
  public:
    void setLogic(const std::string& /*logic*/) override {}
    void declareSort(const std::string& /*name*/, uint32_t /*arity*/) override {}
    void declareFun(const std::string& /*name*/, const std::vector<SmtSort>& /*arg_sorts*/, const SmtSort& /*result_sort*/) override {}
    void defineFun(const std::string& /*name*/, const std::vector<SmtSort>& /*arg_sorts*/, const SmtSort& /*result_sort*/, const SmtTerm& /*body*/) override {}
    void assertFormula(const SmtTerm& /*formula*/) override {}
    SolverResult checkSat() override { return SolverResult::kUnsupported; }
    void push(uint32_t /*depth*/) override {}
    void pop(uint32_t /*depth*/) override {}
    std::string getSmtLibText() const override { return ""; }
    void reset() override {}
};

} // namespace solver
} // namespace trust