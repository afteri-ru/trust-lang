#include "semantic/verify/trust_analyzer.hpp"
#include "semantic/core/name_resolution.hpp"
#include "semantic/verify/format_check.hpp"
#include "semantic/solver.hpp"
#include "symbols/symbol_table.hpp"
#include "semantic/typer/type_inference.hpp"
#include "attrs/attr_builtin.hpp"
#include "ast/token.hpp"
#include "diag/diag.hpp"
#include "diag/options.hpp"
#include "diag/registry.hpp"
#include "semantic/diag.hpp"
#include "types/call.hpp"
#include "types/type_system.hpp"
#include "types/type_id.hpp"
#include "utils/strings.hpp"
#include <algorithm>
#include <format>
#include <string>

namespace trust {

void TrustAnalyzer::analyzeTrustContract(TrustContract& tc) {
    if (!tc.m_expr) {
        return;
    }
    // Скоуп с creator = сам TrustContract: его kind (TrustContract::kind) виден из стека
    // (currentTrustKind) - по нему lookupOrError применяет правила пред/пост-условий.
    m_core.enterScope(tc);
    m_core.analyzeNode(tc.m_expr);
    m_core.exitScope();
}

void TrustAnalyzer::processTrustConditions(const std::vector<AstNodePtr>& trust, const AstNodeBase& owner) {
    if (trust.empty()) {
        return;
    }
    // Два ортогональных механизма: -Wsolver (severity, presence) и --solver-mode (поведение).
    // Presence-диагностика — только без явного режима (иначе шум); резолв имён — всегда.
    const Severity sev = m_actx.ctx().opts().get(semantic::DiagId::Solver); // Severity::Ignore = выключено
    const auto mode = semantic::solverModeFromOptions(m_actx.ctx().opts()); // nullopt = нет поведения
    const bool ignore = (sev == Severity::Ignore);                          // severity-опция выключена
    if (ignore && !mode.has_value()) {
        return; // всё выключено (ignore + нет поведенческого режима)
    }
    if (!ignore && !mode.has_value()) {
        if (sev == Severity::Warning) {
            m_actx.ctx().diag().report(Severity::Warning, owner.range(), "trust condition(s) present");
        } else if (sev == Severity::Error) {
            m_actx.ctx().diag().report(Severity::Error, owner.range(), "trust condition(s) present; compile with -Wsolver=ignore to ignore them");
        } else {
            m_actx.ctx().diag().report(sev, owner.range(), "trust condition(s) present");
        }
    }
    // Резолв имён в условиях (для любого активного режима/severity): невалидные имена - ошибки.
    // export/calculate не генерируют здесь - сбор VCs в SMT-LIB выполняет проход Solver (см. solver/).
    for (const auto& t : trust) {
        if (t && t->is<TrustContract>()) {
            analyzeTrustContract(*t->as<TrustContract>());
        }
    }
}
} // namespace trust
