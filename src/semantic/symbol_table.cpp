#include "semantic/symbol_table.hpp"

namespace trust {

SymbolTable::SymbolTable(DiagnosticEngine& diag)
: m_diag(diag) {
}

bool SymbolTable::addSymbol(Symbol sym) {
    auto [it, inserted] = m_symbols.try_emplace(sym.name, std::move(sym));
    if (!inserted) {
        m_diag.report(Severity::Error, sym.sourceRange, "duplicate declaration '{}'", sym.name);
        return false;
    }
    return true;
}

const Symbol* SymbolTable::lookup(std::string_view name) const {
    auto it = m_symbols.find(std::string(name));
    if (it != m_symbols.end())
        return &it->second;
    return nullptr;
}

} // namespace trust
