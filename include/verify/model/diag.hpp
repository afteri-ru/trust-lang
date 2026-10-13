#pragma once

// include/verify/model/diag.hpp
// Флаги компонента verify (единый источник данных для них). Владелец `--verify` — `verify`:
// гейт доказательств не является ни severity-диагностикой `solver`, ни режимом кодогена.
// TRUST_FLAG_SET генерирует trust::verify::FlagKind + ADL-доступы flagName/flagHelp/flagCategory.

#include "diag/diag_set.hpp"
#include "diag/flag_ids.hpp"

// Формат строки: M(EnumName, "cli", "default_value", DiagGroup, "help").
#define VERIFY_FLAG_LIST(M) \
    M(Verify, diag::flag_id::kVerify, "", DiagGroup::Analysis, "Proof gate: 'all' requires every obligation to be proved (writes *.trustcert)")

TRUST_FLAG_SET(trust::verify, FlagKind, VERIFY_FLAG_LIST)

// VERIFY_FLAG_LIST намеренно НЕ #undef'ится: регистратор переиспользует его для авто-регистрации.
#define VERIFY_FL_ADD_OPT(NAME, cli, defval, grp, help) opts.add_flag(trust::verify::FlagKind::NAME, defval);
