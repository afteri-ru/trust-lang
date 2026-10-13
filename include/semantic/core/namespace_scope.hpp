#pragma once

// include/semantic/core/namespace_scope.hpp
// Query-сервис «текущая позиция в скоуп-стеке»: путь области имён, текущая функция/класс,
// признаки тела типа/цикла/локального скоупа. Собственного состояния не добавляет: всё
// выводится из SymbolTable, кэшируется только namespace-путь (ключ: глубина стека +
// узел-создатель — каждый creator посещается ОДИН раз).

#include "ast/ast_nodes.hpp"
#include "ast/trust_prop.hpp"
#include "symbols/symbol_table.hpp"

#include <cstddef>
#include <string>

namespace trust {

class Context;

class NamespaceScope {
  public:
    NamespaceScope(Context& ctx, const SymbolTable& symbols);

    /// Путь текущей области имён (например "ns::inner"); пустой - глобальная.
    [[nodiscard]] std::string namespacePath() const;
    /// Полная область имён: "::ns::name::" (глобальная → "::").
    [[nodiscard]] std::string namespaceFull() const;
    /// Ближайшая функция (по скоупам снизу вверх) или nullptr.
    [[nodiscard]] const FuncDecl* currentFunc() const;
    /// Ближайший класс (по скоупам снизу вверх) или nullptr.
    [[nodiscard]] const ClassDecl* currentClass() const;
    /// Анализ идёт ВНУТРИ тела типа (Record/Class) - член-объявление, а не top-level функция.
    [[nodiscard]] bool insideTypeBody() const;
    /// Краткое имя текущей функции (без native-префикса '%').
    [[nodiscard]] std::string funcShortName() const;
    /// Полное имя функции: "ns::name" (квалифицированное областью имён).
    [[nodiscard]] std::string qualifiedFuncName() const;
    /// Требует нахождения внутри функции: иначе диагностика Severity::Error и false.
    [[nodiscard]] bool requireFunction(const AstNodeBase& node, const char* macro) const;
    /// True, если текущий скоуп локальный (в стеке скоупов есть FuncDecl).
    [[nodiscard]] bool isInLocalScope() const;
    /// True, если текущий узел находится ВНУТРИ тела цикла.
    [[nodiscard]] bool isInLoop() const;
    /// Вид ближайшего trust-контракта из стека скоупов (kUnknown - вне контракта).
    [[nodiscard]] PropertyKind currentTrustKind() const;

  private:
    Context& m_ctx;
    const SymbolTable& m_symbols;

    mutable std::size_t m_pathDepth = 0;
    mutable const AstNodeBase* m_pathCreator = nullptr;
    mutable std::string m_path;
};

} // namespace trust
