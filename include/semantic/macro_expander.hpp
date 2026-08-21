#pragma once

// include/semantic/macro_expander.hpp
// Раскрытие контекст-макросов (@::/@__NAMESPACE__, @__FUNCTION__, @__FUNCSIG__, @__FUNCDNAME__).
// Вынесено из ядра NameResolutionPass в отдельный ВСЕГДА подключаемый хук
// InlineAnalysisHook, чтобы ядро оставалось чистым разрешителем имён (скоупы,
// регистрация объявлений, резолв Ident). Централизует всю строковую логику имён
// макросов (стрингификация @#/@#'/@#", имя-аналог, метка, квалификатор @::) в одном
// месте вместо разрозненных методов ядра.
//
// Контекст области имён и текущей функции НЕ выводится здесь повторно - это общие
// методы AnalysisContext (namespacePath/currentFunc/funcShortName/...), которые строят
// ядро через скоуп-стек SymbolTable и доступны любому хук-анализатору. Хук вызывается
// ядром в начале обработки каждого узла (до регистрации объявлений и резолва имён),
// поэтому раскрытое имя объявления попадает в таблицу символов, а ContextMacro
// заменяется до резолва.

#include "semantic/inline_hook.hpp"
#include "semantic/pass.hpp"
#include "ast/ast_nodes.hpp"

namespace trust {

class ContextMacroExpander : public InlineAnalysisHook {
  public:
    explicit ContextMacroExpander(AnalysisContext& actx);

    /// Всегда подключается (ядро не зависит от feature-флага).
    std::optional<FlagKind> gateFlag() const override { return std::nullopt; }

    /// Мутирующий обход узла: раскрывает контекст-макросы и квалификатор @::.
    /// Возвращает true, если узел был ПОЛНОСТЬЮ заменён (ContextMacro → Literal/IdentName):
    /// в этом случае ядро пропускает свою обработку (не резолвит результат как имя),
    /// но продолжает обход детей (у заменённого листа детей нет).
    bool onNode(AstNodePtr& node) override;

  private:
    /// Раскрытие самого узла ContextMacro → Literal/IdentName.
    void expandContextMacro(AstNodePtr& self);
    /// Раскрытие ведущего квалификатора @:: в имени-идентификаторе (in-place).
    void expandQualifierName(AstNodePtr& self);
    /// Раскрытие квалификатора @:: в имени объявления (VarDecl/FuncDecl - имя в text()).
    void expandDeclName(AstNodePtr& self);
    /// Раскрытие метки @__FUNCTION__/@::/@__FUNCDNAME__ (JumpStmt::m_label).
    void expandLabel(AstNodePtr& self);

    AnalysisContext& m_actx;
};

} // namespace trust
