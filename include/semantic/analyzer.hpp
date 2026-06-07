#pragma once

#include "ast/ast_nodes.hpp"
#include "diag/context.hpp"
#include "semantic/symbol_table.hpp"
#include <memory>
#include <string>

namespace trust {

/// SemanticAnalyzer — обходит AST, строит таблицу имён, проверяет согласованность.
/// Phase 1: только объявления переменных с инициализацией и литералы.
/// SymbolTable владеется самим анализатором.
class SemanticAnalyzer {
  public:
    explicit SemanticAnalyzer(Context& ctx);

    /// Запуск анализа.
    /// @param ast_nodes Выход парсера (вектор AstNodePtr).
    /// @return true если ошибок нет, false если есть ошибки.
    bool analyze(const std::vector<AstNodePtr>& ast_nodes);

    /// Доступ к таблице имён (используется генератором кода).
    const SymbolTable& symbols() const { return *m_symbols; }

  private:
    /// Обработка одного узла AST.
    void analyzeNode(const AstNodeBase& node);

    /// Обработка VarDecl (объявление переменной).
    void analyzeVarDecl(const VarDecl& var_node);

    /// Обработка BinaryOp ::= (объявление типа).
    void analyzeTypeDecl(const Binary& binary_node);

    /// Обработка FuncDecl (объявление функции).
    void analyzeFuncDecl(const FuncDecl& func_node);

    /// Рекурсивный обход выражений в поиске Ident.
    void walkExprForIdents(const Binary& node);

    /// Поиск идентификатора в таблице имён. Если не найден — сообщает об ошибке.
    /// Возвращает nullptr если не найден или не является IdentName.
    const Symbol* lookupOrError(const AstNodeBase& node);

    Context& m_ctx;
    std::unique_ptr<SymbolTable> m_symbols;
};

} // namespace trust