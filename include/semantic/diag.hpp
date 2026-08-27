#pragma once

// include/semantic/diag.hpp
// Диагностики и feature-флаги компонента semantic (единый источник данных для них).
// TRUST_DIAG_SET/TRUST_FLAG_SET генерируют trust::semantic::DiagId/FlagKind + ADL-доступы.

#include "diag/diag_set.hpp"

#define SEMANTIC_DIAG_LIST(M)                                                                                      \
    M(UnusedVariable, "unused-variable", Warning, "Unused variable", WG_Wall | WG_Wextra | WG_Wunused)             \
    M(UnusedParameter, "unused-parameter", Warning, "Unused function parameter", WG_Wall | WG_Wextra | WG_Wunused) \
    M(Embed, "embed", Warning, "#embed / embed directive", WG_Wall | WG_Wpedantic)                                 \
    M(NoSigil, "sigil", Warning, "Missing sigil (@, $, ...)", WG_Wpedantic)                                        \
    M(Format, "format", Error, "Format string / argument mismatch", WG_Wall | WG_Wformat)                          \
    M(WidenAny, "widen-any", Warning, "Implicit widening numeric conversion", WG_Wextra | WG_Wconversion)          \
    M(Solver, "solver", Warning, "Trust condition(s) present (severity of the presence diagnostic; silence with -Wsolver=ignore)", WG_None)

TRUST_DIAG_SET(trust::semantic, DiagId, SEMANTIC_DIAG_LIST)

#undef SEMANTIC_DIAG_LIST

#define SEMANTIC_FLAG_LIST(M)                                                                                                                        \
    M(Lint, "lint", "Lint analyzer (unused variables/parameters); =aggressive for errors", DiagGroup::Analysis)                                      \
    M(Effect, "effect", "Effect/effects analyzer", DiagGroup::Analysis)                                                                              \
    M(Trust, "trust", "Trust checker (memory safety)", DiagGroup::Analysis)                                                                          \
    M(Extended, "extended", "Extended analysis", DiagGroup::Analysis)                                                                                \
    M(Symbols, "symbols", "Collect symbol index in the result", DiagGroup::Analysis)                                                                 \
    M(SolverLoop, "solver-loop", "Loop verification when no invariant/unroll: ignore|warning|error (default: warning)", DiagGroup::Analysis)         \
    M(SolverLoopUnroll, "solver-loop-unroll", "Unroll loops without an invariant (behavioral; default: off)", DiagGroup::Analysis)                   \
    M(SolverMode, "solver-mode", "Solver behavior: assert|export|calculate (runtime checks / SMT export + run)", DiagGroup::Analysis)                \
    M(Keywords, "keywords", "Macro names that may be written without the '@' sigil (keyword-like); comma-separated, entries may keep a leading '@'", \
      DiagGroup::Analysis)

TRUST_FLAG_SET(trust::semantic, FlagKind, SEMANTIC_FLAG_LIST)

#undef SEMANTIC_FLAG_LIST
