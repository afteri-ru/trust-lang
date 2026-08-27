#pragma once

// include/diag/base_diags.hpp
// Базовые (diag-владение) диагностики, общие для нескольких компонентов:
//   - Deprecated - диагностика использования устаревших возможностей;
//   - ParseError - "синтаксическая"/ошибка определения, используется и transpiler, и types.
// Регистрируются в diag/registry.cpp (applyRegisteredDiagnostics).
// Единственный источник данных - список ниже; TRUST_DIAG_SET генерирует enum и ADL-доступы.

#include "diag/diag_set.hpp"

#define DIAG_BASE_LIST(M)                                                                                     \
    M(Deprecated, "deprecated", Warning, "Use of a deprecated feature", WG_Wall | WG_Wextra | WG_Wdeprecated) \
    M(ParseError, "parse-error", Error, "Syntax error in the source", WG_None)

TRUST_DIAG_SET(trust::diag, DiagId, DIAG_BASE_LIST)

#undef DIAG_BASE_LIST
