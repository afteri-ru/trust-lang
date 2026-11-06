#pragma once

// Диагностики зоны ir/hir (единый источник данных для них).
// TRUST_DIAG_SET генерирует trust::hir::DiagId + ADL-доступы diagName/diagHelp/...
// `-Whir-invariant-unattached`: инвариант цикла не привязан к циклу. Контракт
// `@{ invariant --> ... @}` обязан стоять непосредственно перед циклом (с учётом меток
// lowering'а): иначе обещание индукции не к чему отнести, и проверка была бы пропущена.

#include "diag/diag_set.hpp"

#define HIR_DIAG_LIST(M)                                      \
    M(InvariantUnattached, "hir-invariant-unattached", Error, \
      "Loop invariant is not attached to a loop (place '@{ invariant --> ... @}' immediately before the loop being verified)", WG_None)

TRUST_DIAG_SET(trust::hir, DiagId, HIR_DIAG_LIST)

// HIR_DIAG_LIST намеренно НЕ #undef'ится: регистратор переиспользует его для авто-регистрации
// всех severity-опций (`opts.add(trust::hir::DiagId::X);`), без ручного списка.
#define HIR_DG_ADD_OPT(NAME, cli, sev, help, grp) opts.add(trust::hir::DiagId::NAME);
