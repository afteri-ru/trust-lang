#include "semantic/lint.hpp"

#include "diag/diag.hpp"
#include "diag/options.hpp"
#include "semantic/diag.hpp"

#include <string>
#include <string_view>

namespace trust {

namespace {
// Имя без служебного сигила локальности ('$x' → 'x') для читаемых диагностик.
std::string lintDisplayName(std::string_view name) {
    if (!name.empty() && name.front() == '$') {
        return std::string(name.substr(1));
    }
    return std::string(name);
}
} // namespace

LintHook::LintHook(AnalysisContext& actx)
: m_actx(actx)
, m_aggressive(false) {
    if (auto value = actx.ctx().opts().flag_value(semantic::FlagKind::Lint)) {
        m_aggressive = (*value == "aggressive");
    }
}

void LintHook::onDeclare(const Symbol& sym) {
    // Линтуются переменные и параметры функций (ранее - символы с VariableSymbolData).
    // Имя нормализуется (сигил локальности '$' отбрасывается) и в объявлении, и в
    // использовании (onResolve) - иначе ключи не совпадут.
    if (sym.decl && (sym.decl->kind() == ParserToken::Kind::VarDecl || sym.decl->kind() == ParserToken::Kind::ArgNode)) {
        const bool is_parameter = (sym.decl->kind() == ParserToken::Kind::ArgNode);
        m_declared[lintDisplayName(sym.name)] = {is_parameter, sym.decl->range()};
    }
}

void LintHook::onResolve(const AstNodeBase&, const Symbol* sym) {
    if (sym) {
        m_used.insert(lintDisplayName(sym->name));
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
