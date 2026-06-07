#include "solver/solver.hpp"

#ifdef TRUST_HAS_Z3
#include "solver/solver_z3.hpp"
#else
#include "solver/solver_stub.hpp"
#endif

#include <memory>

namespace trust {
namespace solver {

std::unique_ptr<SolverInterface> createSolver() {
#ifdef TRUST_HAS_Z3
    return std::make_unique<SolverZ3>();
#else
    return std::make_unique<SolverStub>();
#endif
}

} // namespace solver
} // namespace trust
