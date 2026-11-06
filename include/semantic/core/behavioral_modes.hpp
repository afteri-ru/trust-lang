#pragma once

// Единая точка РАЗРЕШЕНИЯ поведенческих режимов из diag::Options (владелец флагов — semantic)
// для передачи в кодоген как ДАННЫЕ (`analysis::BehavioralModes`). Так кодоген не зависит от
// diag::Options/FlagKind и от самого анализатора.

#include "analysis/modes.hpp"
#include "diag/options.hpp"
#include "semantic/solver.hpp"
#include "semantic/ref/stack_check.hpp"

namespace trust {
namespace semantic {

/// Разрешает поведенческие режимы кодогена из уже применённых опций.
[[nodiscard]] inline analysis::BehavioralModes behavioralModesFromOptions(const Options& opts) {
    analysis::BehavioralModes m;
    m.solver = solverModeFromOptions(opts);
    m.stackCheck = stackCheckModeFromOptions(opts);
    m.stackCheckReserve = stackCheckReserveFromOptions(opts);
    m.stackCheckFunctions = stackCheckFunctionsFromOptions(opts);
    m.solverLoop = solverLoopModeFromOptions(opts);
    m.solverLoopUnroll = solverLoopUnrollEnabled(opts);
    return m;
}

} // namespace semantic
} // namespace trust
