#pragma once

// include/syntax/diag.hpp
// Диагностики компонента syntax (единый источник данных для них).
// TRUST_DIAG_SET генерирует trust::syntax::DiagId + ADL-доступы diagName/diagHelp/...

#include "diag/diag_set.hpp"

#define SYNTAX_DIAG_LIST(M) M(MacroRedefined, "macro-redefined", Warning, "Macro redefined with a different body", WG_None)

TRUST_DIAG_SET(trust::syntax, DiagId, SYNTAX_DIAG_LIST)

#undef SYNTAX_DIAG_LIST
