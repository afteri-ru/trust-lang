#pragma once

// include/semantic/core/inline_hook.hpp
// Интерфейс опционального анализатора, подключаемого ПАРАЛЛЕЛЬНО к ядру
// разрешения имён (NameResolutionPass). Хук получает события в реальном времени
// обхода и читает временные данные ядра (SymbolTable) через AnalysisContext.
//
// Флаг включения проверяется ОДИН раз при сборке реестра в `SemanticPassRunner`
// (не в каждом узле): хук, вернувший `isEnabled() == false`, в список активных не попадает,
// и его колбэки не вызываются вовсе (ноль накладных расходов). Условие включения объявляет
// САМ хук (владелец политики), раннер содержит только упорядоченный список фабрик.
//
// ЭТАЛОН реализации анализатора - ContextMacroExpander (semantic/core/macro_expander.hpp):
// минимальный рабочий хук с мутирующим onNode(AstNodePtr&) и чтением query-сервисов
// AnalysisContext (namespacePath/currentFunc/resolveTypeRef/...). Новые анализаторы
// (эффекты, @trust, линт) пишутся по его образцу - см. semantic/MEMORY.md
// «Как написать анализатор».

#include "symbols/symbol_table.hpp"
#include "ast/ast_nodes.hpp"

#include <string_view>

namespace trust {

class InlineAnalysisHook {
  public:
    virtual ~InlineAnalysisHook() = default;

    /// Стабильное имя зоны анализа (трассировка и контракт порядка подключения),
    /// как `flow::Analysis::name()`.
    [[nodiscard]] virtual std::string_view name() const = 0;

    /// Условие подключения; проверяется ОДИН раз при сборке реестра хуков.
    [[nodiscard]] virtual bool isEnabled() const { return true; }

    /// Вход/выход скоупа (вложенность).
    virtual void enterScope() {}
    virtual void exitScope() {}

    /// Объявление символа в текущем скоупе (VarDecl/FuncDecl/TypeDecl).
    virtual void onDeclare(const Symbol& sym) {}

    /// Разрешение имени (Ident); sym может быть nullptr при "undefined name".
    virtual void onResolve(const AstNodeBase& node, const Symbol* sym) {}

    /// Произвольный узел при обходе (МУТИРУЮЩИЙ): позволяет хуку заменять узлы
    /// (напр. ContextMacroExpander раскрывает ContextMacro → Literal/IdentName).
    /// Возвращает true, если узел был ПОЛНОСТЬЮ заменён/потреблён: тогда ядро
    /// пропускает собственную обработку этого узла, но продолжает обход детей.
    virtual bool onNode(AstNodePtr& node) {
        (void)node;
        return false;
    }

    /// Вызывается после завершения обхода ядра (для финального отчёта).
    virtual void finalize() {}
};

} // namespace trust
