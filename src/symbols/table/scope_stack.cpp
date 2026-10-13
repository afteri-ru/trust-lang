#include "symbols/table/scope_stack.hpp"

#include <utility>

namespace trust {

const Symbol* Scope::lookup(const StorageKey& key) const {
    auto it = symbols.find(key);
    return it == symbols.end() ? nullptr : &it->second;
}

ScopeStack::ScopeStack() {
    Scope global;
    global.kind = ScopeKind::Global;
    m_allScopes.push_back(std::move(global));
    m_activePath.push_back(0);
}

void ScopeStack::push(const AstNodeBase* creator, ScopeKind kind) {
    Scope scope;
    scope.kind = kind;
    scope.creator = creator;
    scope.parent = m_activePath.back();
    m_allScopes.push_back(std::move(scope));
    m_activePath.push_back(m_allScopes.size() - 1);
}

void ScopeStack::pop() {
    if (m_activePath.size() > 1) {
        node(m_activePath.back()).closed = true;
        m_activePath.pop_back();
    }
}

} // namespace trust
