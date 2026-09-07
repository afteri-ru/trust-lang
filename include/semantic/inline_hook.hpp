#pragma once

// include/semantic/inline_hook.hpp
// Интерфейс опционального анализатора, подключаемого ПАРАЛЛЕЛЬНО к ядру
// разрешения имён (NameResolutionPass). Хук получает события в реальном времени
// обхода и читает временные данные ядра (SymbolTable) через AnalysisContext.
//
// Флаг включения проверяется ОДИН раз при подключении в SemanticPassRunner
// (не в каждом узле); если флаг выключен - хук не добавляется в список активных,
// и его колбэки не вызываются вовсе (ноль накладных расходов).
//
// ЭТАЛОН реализации анализатора - ContextMacroExpander (semantic/macro_expander.hpp):
// минимальный рабочий хук с мутирующим onNode(AstNodePtr&) и чтением query-сервисов
// AnalysisContext (namespacePath/currentFunc/resolveType/...). Новые анализаторы
// (эффекты, @trust, линт) пишутся по его образцу - см. semantic/MEMORY.md
// «Как написать анализатор».

#include "diag/options.hpp"
#include "semantic/symbol_table.hpp"
#include "semantic/diag.hpp"
#include "ast/ast_nodes.hpp"

#include <optional>

namespace trust {

class InlineAnalysisHook {
  public:
    virtual ~InlineAnalysisHook() = default;

    /// Флаг, включающий хук; nullopt - подключается всегда.
    virtual std::optional<semantic::FlagKind> gateFlag() const { return std::nullopt; }

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
