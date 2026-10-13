#pragma once

// include/solver/impl/diag.hpp
// Диагностики компонента solver (единый источник данных для них).
// TRUST_DIAG_SET генерирует trust::solver::DiagId + ADL-доступы diagName/diagHelp/diagCategory.

#include "diag/diag_set.hpp"

// `-Wverify-envelope`: код вне области проверки не проверялся (VC пропущена).
// Severity по умолчанию Warning: компиляция не блокируется, но VC никогда не считается
// доказанной. Профиль доказательств включает `-Wverify-envelope=error`.
#define SOLVER_DIAG_LIST(M)                                                                                                                     \
    M(VerifyEnvelope, "verify-envelope", Warning,                                                                                               \
      "Code outside the verification envelope is not verified (the proof is not attempted; use -Wverify-envelope=error to fail)", WG_None)

TRUST_DIAG_SET(trust::solver, DiagId, SOLVER_DIAG_LIST)

// SOLVER_DIAG_LIST намеренно НЕ #undef'ится: регистратор переиспользует его для авто-регистрации
// всех severity-опций (`opts.add(trust::solver::DiagId::X);`), без ручного списка.
#define SOLVER_DG_ADD_OPT(NAME, cli, sev, help, grp) opts.add(trust::solver::DiagId::NAME);
