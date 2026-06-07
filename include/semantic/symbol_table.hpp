#pragma once

#include "diag/diag.hpp"
#include "diag/location.hpp"
#include "ast/token.hpp"
#include "types/type_id.hpp"
#include <string>
#include <unordered_map>
#include <variant>

namespace trust {

// Forward declaration
class DiagnosticEngine;

// ── SymbolData — тип-специфичные данные для Symbol ──────────

/// Данные переменной.
/// AstNodePtr init == nullptr → forward declaration (только имя + тип, без инициализатора).
struct VariableSymbolData {
    AstNodePtr init; ///< Инициализатор (nullptr = forward declaration)
    bool is_mutable; ///< mutable qualifier
};

/// Данные функции.
/// AstNodePtr body == nullptr → forward declaration (только сигнатура, без тела).
struct FunctionSymbolData {
    AstNodePtr body; ///< Тело функции (nullptr = forward declaration)
};

/// SymbolData — variant тип-специфичных данных.
using SymbolData = std::variant<VariableSymbolData, FunctionSymbolData>;

/// Symbol entry stored in the symbol table.
/// TypeId type — общий для всех имён: для переменной это тип значения,
/// для функции — FunctionTypeId (сигнатура).
struct Symbol {
    std::string name;        ///< Canonical name
    TypeId type;             ///< Resolved type id (INVALID_TYPE_ID if not yet known)
    MapperRange sourceRange; ///< Source position of declaration
    SymbolData data;         ///< Тип-специфичные данные
};

/// SymbolTable — flat symbol table for a single scope.
/// Phase 1: no nested scopes, no function parameters.
class SymbolTable {
  public:
    explicit SymbolTable(DiagnosticEngine& diag);

    /// Add a symbol. Returns false and reports error if duplicate.
    bool addSymbol(Symbol sym);

    /// Look up a name. Returns nullptr if not found.
    const Symbol* lookup(std::string_view name) const;

    /// Number of symbols registered.
    std::size_t size() const { return m_symbols.size(); }

  private:
    DiagnosticEngine& m_diag;
    std::unordered_map<std::string, Symbol> m_symbols;
};

} // namespace trust