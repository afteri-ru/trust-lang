// src/semantic/core/local_name.cpp
#include "semantic/core/local_name.hpp"

#include "semantic/core/pass.hpp"
#include "ast/ast_nodes.hpp"
#include "diag/diag.hpp"
#include "semantic/diag.hpp"
#include "utils/strings.hpp"

namespace trust {

std::string normalizeLocalSigil(AnalysisContext& actx, IdentName& node, MapperRange range, bool isLocal) {
    std::string name{node.text()};
    if (isLocal && isSimpleVarName(name)) {
        const std::string sigil = "$" + name;
        node.set_text_interned(sigil);
        // Быстрый фикс: заменить bare-имя на сигнальное `$name`. Отчёт через diag().report(...)
        // (возвращает DiagnosticEntry*) вместо ctx().report(...) (discard) - нужен fixit.
        const Severity sev = actx.ctx().opts().get(semantic::DiagId::NoSigil);
        if (sev != Severity::Ignore) {
            auto* entry = actx.ctx().diag().report(sev, range, semantic::DiagId::NoSigil, "creating a local variable '${}'", name);
            if (entry != nullptr && !range.isInvalid()) {
                actx.ctx().diag().fixit(entry, range, sigil);
            }
        }
        return sigil;
    }
    return name;
}

std::string canonicalTargetName(const AnalysisContext& actx, const IdentName& target) {
    std::string name{target.text()};
    if (actx.namespaces().isInLocalScope() && isSimpleVarName(name)) {
        return "$" + name;
    }
    return name;
}

} // namespace trust
