#include "analysis/symbol_table.hpp"

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
    if (sym.decl->is<VarDecl>()) {
        return sym.decl->as<VarDecl>()->m_initializer == nullptr;
    }
    if (sym.decl->is<FuncDecl>()) {
        return !sym.decl->as<FuncDecl>()->m_body.has_value();
    }
    return false; // TypeDecl и прочие - всегда определение
}

DeclResult SymbolTable::declareOrComplete(const Symbol& sym) {
    auto& scope = current();
    auto it = scope.symbols.find(sym.name);
    if (it == scope.symbols.end()) {
        scope.symbols.emplace(sym.name, sym);
        return DeclResult::Inserted;
    }
    Symbol& existing = it->second;
    EXPECT(sym.decl && "declareOrComplete requires a declaration node");

    // Функции: одно имя может адресовать НАБОР перегрузок (разные сигнатуры). Сигнатура
    // сравнивается структурно (structuralType снимает поведенческие флаги). Точный дубль
    // сигнатуры - ошибка; различающиеся сигнатуры - расширение набора (Overloaded).
    const bool bothFunc = existing.decl != nullptr && existing.decl->is<FuncDecl>() && sym.decl->is<FuncDecl>();
    if (bothFunc) {
        const TypeId newSig = structuralType(sym.type);
        const TypeId curSig = structuralType(existing.type);
        // Завершение forward-объявления определением ИМЕННО той же сигнатуры (пока имя не стало
        // набором - иначе различать, какую из перегрузок завершает определение, негде).
        if (existing.overloads.empty() && isForwardDecl(existing) && !isForwardDecl(sym) && curSig == newSig) {
            existing = sym;
            return DeclResult::Completed;
        }
        if (curSig == newSig) {
            return DeclResult::Duplicate; // два определения/два forward одной сигнатуры
        }
        for (const TypeId s : existing.overloads) {
            if (structuralType(s) == newSig) {
                return DeclResult::Duplicate;
            }
        }
        if (existing.overloads.empty()) {
            existing.overloads.push_back(curSig);
        }
        existing.overloads.push_back(newSig);
        // Пометить ВСЕ объявления набора «перегружено» (кодоген: уникальная экспорт-запись +
        // однозначный адрес символа). Существующий узел + новый узел.
        if (FuncDecl* ed = existing.decl->as<FuncDecl>()) {
            ed->m_isOverloaded = true;
        }
        if (FuncDecl* sd = sym.decl->as<FuncDecl>()) {
            sd->m_isOverloaded = true;
        }
        return DeclResult::Overloaded;
    }

    // Завершение forward-объявления определением того же kind в том же скоупе (не-функции).
    if (isForwardDecl(existing) && !isForwardDecl(sym) && existing.decl->kind() == sym.decl->kind()) {
        existing = sym;
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
