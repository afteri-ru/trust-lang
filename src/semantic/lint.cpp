#include "semantic/lint.hpp"

#include "diag/diag.hpp"
#include "diag/options.hpp"

namespace trust {

LintHook::LintHook(AnalysisContext& actx)
: m_actx(actx)
, m_aggressive(false) {
    if (auto value = actx.ctx().opts().flag_value(FlagKind::Lint)) {
        m_aggressive = (*value == "aggressive");
    }
}

void LintHook::onDeclare(const Symbol& sym) {
    // Линтуются переменные и параметры функций (ранее - символы с VariableSymbolData).
    if (sym.decl && (sym.decl->kind() == ParserToken::Kind::VarDecl || sym.decl->kind() == ParserToken::Kind::ArgNode)) {
        m_declared[sym.name] = sym.decl->range();
    }
}

void LintHook::onResolve(const AstNodeBase&, const Symbol* sym) {
    if (sym) {
        m_used.insert(sym->name);
    }
}

void LintHook::finalize() {
    for (const auto& [name, range] : m_declared) {
        if (m_used.count(name) != 0) {
            continue;
        }
        if (m_aggressive) {
            m_actx.ctx().diag().report(Severity::Error, range, "unused variable '{}' (aggressive lint)", name);
        } else {
            m_actx.ctx().report(range, OptKind::UnusedVar, "unused variable '{}'", name);
        }
    }
}

} // namespace trust
