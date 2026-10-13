#pragma once

// include/transpiler/emit/decl_emit_common.hpp
// Помощники эмиссии объявлений, общие для decl_emit/type_decl_emit/module_emit.

#include "transpiler/emit/emit_ctx.hpp"
#include "ast/ast_nodes.hpp"
#include "attrs/attr_builtin.hpp"
#include <set>
#include <vector>

namespace trust {

/// Тело метода может жить в заголовке модуля только если его НЕЛЬЗЯ вынести out-of-line:
/// член шаблонного record либо шаблонный метод и `@[constexpr]`.
inline bool methodBodyMustBeInHeader(const FuncDecl& func, bool recordIsTemplate, const AttrPool& attrs) {
    const bool generic = func.m_templateParams.has_value() && !func.m_templateParams->empty();
    return recordIsTemplate || generic || func.has_attr(attrs, attr::FuncConstexpr);
}

/// Ссылается ли тело на сущность МОДУЛЬНОГО уровня, не опубликованную заголовком.
inline bool bodyUsesNonPublishedEntity(const FuncDecl& func, const std::set<const AstNodeBase*>& published, ModuleIndex module) {
    if (!func.m_body.has_value()) {
        return false;
    }
    std::vector<const AstNodeBase*> stack;
    for (const AstNodePtr& node : *func.m_body) {
        if (node) {
            stack.push_back(node.get());
        }
    }
    while (!stack.empty()) {
        const AstNodeBase* node = stack.back();
        stack.pop_back();
        if (node->is<IdentName>()) {
            const AstNodeBase* resolved = node->as<IdentName>()->resolvedDecl;
            if (resolved != nullptr &&
                (resolved->isFilePrivate() || (module.isValid() && resolved->module == module && published.find(resolved) == published.end()))) {
                return true;
            }
        }
        for (const AstNodePtr& child : node->children()) {
            if (child) {
                stack.push_back(child.get());
            }
        }
    }
    return false;
}

/// Временное снятие forward-режима заголовка на время эмиссии ОДНОГО метода.
class BodyEmitInHeaderGuard {
  public:
    explicit BodyEmitInHeaderGuard(CppEmitContext& ectx)
    : m_ectx(ectx)
    , m_saved(ectx.m_mode.forwardDeclOnly) {
        m_ectx.m_mode.forwardDeclOnly = false;
    }
    ~BodyEmitInHeaderGuard() { m_ectx.m_mode.forwardDeclOnly = m_saved; }
    BodyEmitInHeaderGuard(const BodyEmitInHeaderGuard&) = delete;
    BodyEmitInHeaderGuard& operator=(const BodyEmitInHeaderGuard&) = delete;

  private:
    CppEmitContext& m_ectx;
    bool m_saved;
};

} // namespace trust