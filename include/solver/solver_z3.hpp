#pragma once

#include "solver/solver.hpp"

#include <memory>
#include <string>
#include <vector>

namespace trust {
namespace solver {

/// Solver implementation using Z3 C API.
/// Only available when WITH_SOLVER=ON.
class SolverZ3 final : public SolverInterface {
  public:
    SolverZ3();
    ~SolverZ3() override;

    // Non-copyable
    SolverZ3(const SolverZ3&) = delete;
    SolverZ3& operator=(const SolverZ3&) = delete;
    SolverZ3(SolverZ3&&) = delete;
    SolverZ3& operator=(SolverZ3&&) = delete;

    void setLogic(const std::string& logic) override;
    void declareSort(const std::string& name, uint32_t arity) override;
    void declareFun(const std::string& name, const std::vector<SmtSort>& arg_sorts, const SmtSort& result_sort) override;
    void defineFun(const std::string& name, const std::vector<SmtSort>& arg_sorts, const SmtSort& result_sort, const SmtTerm& body) override;
    void assertFormula(const SmtTerm& formula) override;
    SolverResult checkSat() override;
    std::optional<std::string> getModelValue(const std::string& name, const SmtSort& srt) override;
    void push(uint32_t depth = 1) override;
    void pop(uint32_t depth = 1) override;
    std::string getSmtLibText() const override;
    void reset() override;

  private:
    struct Impl;
    std::unique_ptr<Impl> m_impl;
};

} // namespace solver
} // namespace trust