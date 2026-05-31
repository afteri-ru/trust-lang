#include "solver/solver.hpp"

#include "solver/solver_stub.hpp"

#include <memory>

namespace trust {
namespace solver {

std::unique_ptr<SolverInterface> createSolver() {
    // When WITH_SOLVER=OFF (default), return stub.
    // The WITH_SOLVER=ON path uses SolverZ3 from solver_z3.cpp.
    return std::make_unique<SolverStub>();
}

} // namespace solver
} // namespace trust