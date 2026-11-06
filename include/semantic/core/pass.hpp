#pragma once

// Агрегат одного прогона семантики. СОБСТВЕННОЙ ВЫЧИСЛИТЕЛЬНОЙ ЛОГИКИ НЕ СОДЕРЖИТ:
// только владение зонами ответственности и доступ к ним. Зоны:
//   symbols()    - SymbolTable (стек лексических скоупов, единственный владелец объявлений);
//   namespaces() - NamespaceScope (текущая позиция в скоуп-стеке, namespace-путь);
//   types()      - TypeResolution (резолв типов/модульных ссылок/runtime-символов);
//   facts()      - ExprFacts (type-state выражений в полях AST);
//   state()      - AnalysisState (LSP-проекция, module-набор, счётчики временных).
// Факты для бэкендов материализуются в полях узлов AST; HIR/flow-анализы выполняет Pipeline.

#include "session/context.hpp"
#include "symbols/symbol_table.hpp"
#include "semantic/core/analysis_state.hpp"
#include "semantic/core/expr_facts.hpp"
#include "semantic/core/namespace_scope.hpp"
#include "semantic/core/type_resolver.hpp"

namespace trust {

class TypeSystem;

class AnalysisContext {
  public:
    /// `registry` — WRITER-ссылка владельца (`Pipeline`): единственный канал записи в реестр `types`
    /// (E1). Передаётся явно (не через фасад `Context`), поэтому мутабельный реестр недостижим
    /// потребителям без права записи (R12).
    AnalysisContext(Context& ctx, TypeSystem& registry);

    Context& ctx() { return m_ctx; }
    const Context& ctx() const { return m_ctx; }

    /// WRITE-зона реестра типов (E1): мутабельный доступ к `types` есть ТОЛЬКО у зоны записи
    /// (семантика/Pipeline). Потребители без права записи читают через `Context::readTypes()`
    /// (R12). Второго пути записи нет.
    [[nodiscard]] TypeSystem& registry() { return m_registry; }

    SymbolTable& symbols() { return m_symbols; }
    const SymbolTable& symbols() const { return m_symbols; }

    NamespaceScope& namespaces() { return m_namespaces; }
    const NamespaceScope& namespaces() const { return m_namespaces; }

    TypeResolution& types() { return m_types; }
    const TypeResolution& types() const { return m_types; }

    ExprFacts& facts() { return m_facts; }
    const ExprFacts& facts() const { return m_facts; }

    AnalysisState& state() { return m_state; }
    const AnalysisState& state() const { return m_state; }

    /// Истина, если накоплены ошибки (errorCount() > 0).
    [[nodiscard]] bool hasErrors() const;

  private:
    Context& m_ctx;
    TypeSystem& m_registry;
    SymbolTable m_symbols;
    NamespaceScope m_namespaces;
    TypeResolution m_types;
    ExprFacts m_facts;
    AnalysisState m_state;
};

} // namespace trust
