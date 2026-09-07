#include "semantic/lint.hpp"

#include "diag/diag.hpp"
#include "diag/options.hpp"
#include "semantic/diag.hpp"

namespace trust {

LintHook::LintHook(AnalysisContext& actx)
: m_actx(actx)
, m_aggressive(false) {
    if (auto value = actx.ctx().opts().flag_value(semantic::FlagKind::Lint)) {
        m_aggressive = (*value == "aggressive");
    }
}

void LintHook::onDeclare(const Symbol& sym) {
    // Линтуются переменные и параметры функций (ранее - символы с VariableSymbolData).
    if (sym.decl && (sym.decl->kind() == ParserToken::Kind::VarDecl || sym.decl->kind() == ParserToken::Kind::ArgNode)) {
        const bool is_parameter = (sym.decl->kind() == ParserToken::Kind::ArgNode);
        m_declared[sym.name] = {is_parameter, sym.decl->range()};
    }
}

void LintHook::onResolve(const AstNodeBase&, const Symbol* sym) {
    if (sym) {
        m_used.insert(sym->name);
    }
}

void LintHook::finalize() {
    for (const auto& [name, info] : m_declared) {
        if (m_used.count(name) != 0) {
            continue;
        }
        const bool is_parameter = info.first;
        const MapperRange& range = info.second;
        const semantic::DiagId kind = is_parameter ? semantic::DiagId::UnusedParameter : semantic::DiagId::UnusedVariable;
        if (m_aggressive) {
            const char* what = is_parameter ? "parameter" : "variable";
            m_actx.ctx().diag().report(Severity::Error, range, "unused {} '{}' (aggressive lint)", what, name);
        } else {
            const char* what = is_parameter ? "parameter" : "variable";
            m_actx.ctx().report(range, kind, "unused {} '{}'", what, name);
        }
    }
}

} // namespace trust
