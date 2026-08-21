# MEMORY.md

> scope: include/solver
> role: persistent-memory
> last_reviewed: 2026-08-19
> review_period: 30
> max_size: 4096

## Solver Component - SMT-LIB 2 Integration

The `solver` component provides SMT-LIB 2 formula generation and optional Z3 solver integration for formal verification (Trust Checking).

### Architecture

- **SmtAst** - AST nodes for SMT-LIB 2 expressions (terms, sorts, commands)
- **SmtPrinter** - converts SmtAst → SMT-LIB 2 text format (works without Z3)
- **SolverInterface** - abstract interface for SMT solver backends
- **SolverZ3** - implementation using Z3 C API (requires `WITH_SOLVER=ON`)
- **SolverStub** - fallback stub when Z3 is not available, returns `kUnknown`

### Configuration

The component is controlled by the CMake option `WITH_SOLVER`:

- `WITH_SOLVER=ON` - links Z3, enables `SolverZ3`
- `WITH_SOLVER=OFF` (default) - only stub, only SMT-LIB 2 text generation

### Dependencies

- `WITH_SOLVER=ON`: Z3 (libz3-dev ≥ 4.8)
- Always: C++23, `<string>`, `<vector>`, `<variant>`, `<optional>`, `<memory>`