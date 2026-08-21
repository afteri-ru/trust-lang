#include "semantic/symbol_table.hpp"

#include "ast/ast_nodes.hpp"
#include "ast/token.hpp"

namespace trust {

const Symbol* SymbolTable::Scope::lookup(std::string_view name) const {
    auto it = symbols.find(std::string(name));
    return it == symbols.end() ? nullptr : &it->second;
}

void SymbolTable::push(const AstNodeBase* creator) {
    m_scopes.push_back(Scope{creator, {}});
}

void SymbolTable::pop() {
    if (m_scopes.size() > 1) {
        m_scopes.pop_back();
    }
}

bool SymbolTable::declare(const Symbol& sym) {
    auto& scope = current();
    auto [it, inserted] = scope.symbols.try_emplace(sym.name, sym);
    return inserted;
}

bool SymbolTable::isForwardDecl(const Symbol& sym) {
    EXPECT(sym.decl && "Symbol::isForwardDecl requires a declaration node");
    switch (sym.decl->kind()) {
    case ParserToken::Kind::VarDecl:
        return static_cast<const VarDecl&>(*sym.decl).m_initializer == nullptr;
    case ParserToken::Kind::FuncDecl:
        return !static_cast<const FuncDecl&>(*sym.decl).m_body.has_value();
    default:
        return false; // TypeDecl и прочие - всегда определение
    }
}

DeclResult SymbolTable::declareOrComplete(Symbol& sym) {
    auto& scope = current();
    auto it = scope.symbols.find(sym.name);
    if (it == scope.symbols.end()) {
        scope.symbols.emplace(sym.name, std::move(sym));
        return DeclResult::Inserted;
    }
    Symbol& existing = it->second;
    EXPECT(sym.decl && "declareOrComplete requires a declaration node");
    // Завершение forward-объявления определением того же kind в том же скоупе.
    if (isForwardDecl(existing) && !isForwardDecl(sym) && existing.decl->kind() == sym.decl->kind()) {
        existing = std::move(sym);
        return DeclResult::Completed;
    }
    return DeclResult::Duplicate;
}

const Symbol* SymbolTable::resolve(std::string_view name) const {
    for (auto it = m_scopes.rbegin(); it != m_scopes.rend(); ++it) {
        if (const Symbol* found = it->lookup(name)) {
            return found;
        }
    }
    return nullptr;
}

Symbol* SymbolTable::resolveMutable(std::string_view name) {
    for (auto it = m_scopes.rbegin(); it != m_scopes.rend(); ++it) {
        auto found = it->symbols.find(std::string(name));
        if (found != it->symbols.end()) {
            return &found->second;
        }
    }
    return nullptr;
}

} // namespace trust
