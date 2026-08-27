#pragma once

// include/transpiler/diag.hpp
// Feature-флаги компонента transpiler (единый источник данных для них). Своих severity-диагностик
// у transpiler нет (ParseError - общая базовая, см. diag/base_diags.hpp).
// TRUST_FLAG_SET генерирует trust::transpiler::FlagKind + ADL-доступы flagName/flagHelp/flagCategory.

#include "diag/diag_set.hpp"

#define TRANSPILER_FLAG_LIST(M)                                                                     \
    M(Comments, "comments", "Emit documentation comments in the generated C++", DiagGroup::Codegen) \
    M(Assert, "assert", "Emit assertions in the generated code", DiagGroup::Codegen)                \
    M(Backtrace, "backtrace", "Emit backtrace info on abort", DiagGroup::Codegen)

TRUST_FLAG_SET(trust::transpiler, FlagKind, TRANSPILER_FLAG_LIST)

#undef TRANSPILER_FLAG_LIST
