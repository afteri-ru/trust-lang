// Generated: src/semantic/trust_analyzer.cpp
#include "semantic/trust_analyzer.hpp"
#include "semantic/name_resolution.hpp"
#include "semantic/analysis_common.hpp"
#include "semantic/format_check.hpp"
#include "semantic/solver.hpp"
#include "semantic/symbol_table.hpp"
#include "semantic/type_inference.hpp"
#include "ast/attr_builtin.hpp"
#include "ast/token.hpp"
#include "diag/diag.hpp"
#include "diag/options.hpp"
#include "diag/registry.hpp"
#include "semantic/diag.hpp"
#include "types/promotion.hpp"
#include "types/registry.hpp"
#include "types/type_id.hpp"
#include "utils/operators.hpp"
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
    // Два ортогональных механизма (конвенция GCC/Clang):
    //   - `-Wsolver` (DiagId::Solver, severity) - presence-диагностика «присутствуют trust-условия»;
    //   - `--solver-mode` (SolverMode, behavioral) - assert/export/calculate.
    const std::optional<Severity> sev = m_actx.ctx().opts().get(semantic::DiagId::Solver); // nullopt = ignore
    const auto mode = semantic::solverModeFromOptions(m_actx.ctx().opts());                // nullopt = нет поведения
    const bool ignore = !sev.has_value();                                                  // nullopt = ignore (severity-опция выключена)
    if (ignore && !mode.has_value()) {
        return; // всё выключено (ignore + нет поведенческого режима)
    }
    // Presence-диагностика: только когда severity включена и НЕ задан явный поведенческий режим
    // (вариант 1: если пользователь явно верифицирует через --solver-mode, «присутствуют условия» - шум).
    if (!ignore && !mode.has_value()) {
        if (*sev == Severity::Warning) {
            m_actx.ctx().diag().report(Severity::Warning, owner.range(), "trust condition(s) present");
        } else if (*sev == Severity::Error) {
            m_actx.ctx().diag().report(Severity::Error, owner.range(), "trust condition(s) present; compile with -Wsolver=ignore to ignore them");
        } else {
            m_actx.ctx().diag().report(*sev, owner.range(), "trust condition(s) present");
        }
    }
    // Резолв имён в условиях (для любого активного режима/severity): невалидные имена - ошибки.
    // export/calculate не генерируют здесь - сбор VCs в SMT-LIB выполняет проход Solver (см. solver/).
    for (const auto& t : trust) {
        if (auto* tc = dynamic_cast<TrustContract*>(t.get())) {
            analyzeTrustContract(*tc);
        }
    }
}
} // namespace trust
