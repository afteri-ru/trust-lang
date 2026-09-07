#pragma once

// include/transpiler/diag.hpp
// Feature-флаги компонента transpiler (единый источник данных для них). Своих severity-диагностик
// у transpiler нет (ParseError - общая базовая, см. diag/base_diags.hpp).
// TRUST_FLAG_SET генерирует trust::transpiler::FlagKind + ADL-доступы flagName/flagHelp/flagCategory.

#include "diag/diag_set.hpp"

// Флаги, управляемые через `-W<name>`/`-Wno-<name>` (диагностические). Здесь — Assert и Backtrace.
#define TRANSPILER_W_FLAG_LIST(M)                                                        \
    M(Assert, "assert", "", DiagGroup::Codegen, "Emit assertions in the generated code") \
    M(Backtrace, "backtrace", "", DiagGroup::Codegen, "Emit backtrace info on abort")

// Полный список флагов транспилятора (для генерации enum/desc через TRUST_FLAG_SET).
// Comments включён в enum, но НЕ управляется через -W — только через -fcomments/-fno-comments
// (регистрируется отдельно через add_flag_nonw; см. registrar в transpiler.cpp).
#define TRANSPILER_FLAG_LIST(M)                                                                         \
    M(Comments, "comments", "", DiagGroup::Codegen, "Emit documentation comments in the generated C++") \
    TRANSPILER_W_FLAG_LIST(M)

TRUST_FLAG_SET(trust::transpiler, FlagKind, TRANSPILER_FLAG_LIST)

// TRANSPILER_W_FLAG_LIST намеренно НЕ #undef'ится: регистратор переиспользует его для авто-регистрации
// флагов-диагностик (`opts.add_flag(FlagKind::X, defval)`; дефолт задаётся внутри add_flag). Включение
// флагов по умолчанию (`opts.set_enabled(..., true)`) - отдельное действие (это состояние вкл/выкл,
// не value), остаётся в регистраторе.
#define TRANSPILER_FL_ADD_OPT(NAME, cli, defval, grp, help) opts.add_flag(trust::transpiler::FlagKind::NAME, defval);
