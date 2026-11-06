#pragma once

// Binder-контекст объявления (позиционная C++-идентичность FunctionIdentity). Единственный
// экстрактор: используется и SymbolTable (per-overload forward/definition), и semantic
// (addMethod/hasOwnMethodSignature). Не зависит от semantic/fn_identity-registry.

#include "ast/ast_nodes.hpp"
#include "types/call.hpp"

#include <optional>
#include <string>
#include <vector>

namespace trust {

[[nodiscard]] inline TemplateBinders bindersOf(const std::optional<std::vector<AstNodePtr>>& params) {
    TemplateBinders out;
    if (!params) {
        return out;
    }
    out.reserve(params->size());
    for (const auto& p : *params) {
        if (!p || p->kind() != ParserToken::Kind::ArgNode) {
            continue;
        }
        const auto& a = static_cast<const ArgNode&>(*p);
        out.push_back(TemplateBinder{std::string(a.text()), a.isTypePack});
    }
    return out;
}

[[nodiscard]] inline TemplateBinders bindersOf(const FuncDecl& f) {
    return bindersOf(f.m_templateParams);
}

/// Binder'ы узла объявления (пусто для не-функций).
[[nodiscard]] inline TemplateBinders bindersOfDecl(const AstNodeBase* decl) {
    if (decl == nullptr || !decl->is<FuncDecl>()) {
        return {};
    }
    return bindersOf(*decl->as<FuncDecl>());
}

} // namespace trust
