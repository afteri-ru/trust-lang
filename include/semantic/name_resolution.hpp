#pragma once

// include/semantic/name_resolution.hpp
// Единый однопроходный проход разрешения имён (обязательное ядро семантики).
// NameResolutionPass - ДРАЙВЕР (издатель событий): владеет обходом AST, скоуп-стеком и
// подключёнными InlineAnalysisHook. Тяжёлая анализ-логика вынесена в компоненты
// DeclAnalyzer/ExprTyper/AccessResolver/TrustAnalyzer (см. *_analyzer.hpp/*_analyzer.cpp),
// разделяющие AnalysisContext (m_actx) и обращающиеся к драйверу (рекурсия/скоупы/резолв имён)
// через ссылку m_core (дружба). Публичный контракт и модель «драйвер + хуки» сохранены.

#include "semantic/pass.hpp"
#include "semantic/inline_hook.hpp"
#include "semantic/decl_analyzer.hpp"
#include "semantic/expr_typer.hpp"
#include "semantic/access_resolver.hpp"
#include "semantic/trust_analyzer.hpp"
#include "ast/ast_nodes.hpp"

#include <memory>
#include <vector>

namespace trust {

class NameResolutionPass {
  public:
    explicit NameResolutionPass(AnalysisContext& actx);

    /// Подключает опциональный анализатор (параллельно к обходу ядра).
    /// ВАЖНО: всегда-подключённый ContextMacroExpander должен быть добавлен до
    /// остальных хуков (его onNode первым раскрывает ContextMacro/квалификатор @::).
    void addHook(std::unique_ptr<InlineAnalysisHook> hook);

    /// Выполняет однопроходное разрешение имён над корневым списком операторов.
    void run(std::vector<AstNodePtr>& ast_nodes);

    /// Финальный проход по подключённым хукам (после завершения обхода).
    void finalize();

  private:
    /// Обработка узла по kind (объявления, типы, Ident); БЕЗ рекурсии в детей -
    /// полный обход детей выполняет analyzeNode через analyzeChildren.
    void handleNode(AstNodePtr& self);

    void analyzeNode(AstNodePtr& self);
    /// Обход реальных детей через единый источник AstNodeBase::collectChildren
    /// (ссылки на слоты, чтобы хук мог заменять узлы). Не открывает скоупы -
    /// это делает analyzeNode.
    void analyzeChildren(AstNodePtr& self);

    // Вспомогательные: вход/выход скоупа с уведомлением хуков.
    void enterScope(const AstNodeBase& node);
    void exitScope();

    /// Единый алгоритм разрешения простого имени (имя без сигила/квалификатора): `x` ищется
    /// сначала как локальная `$x` (если такой символ есть в текущем/охватывающем скоупе - текст
    /// узла-ссылки нормализуется на `$x`). Квалифицированные/сигилные/нативные имена - без изменений.
    Symbol* resolveSimple(AstNodeBase* node, std::string_view name);
    /// Не-мутирующий вариант разрешения простого имени (для чтения dims/типов до обхода детей).
    const Symbol* resolveSimpleRead(std::string_view name) const;
    /// Резолв имени узла с диагностикой «undefined name» (единый источник для компонентов).
    const Symbol* lookupOrError(AstNodeBase& node);
    /// Текущая функция (ближайший creator скоупа с kind FuncDecl) или nullptr.
    const FuncDecl* currentFuncDecl() const;
    /// Контекст trust-контракта из стека скоупов (ближайший creator с kind TrustContract).
    PropertyKind currentTrustKind() const;
    /// Узел декларации ТИПА для доверенного типа (источник trust-условий).
    const AstNodeBase* trustTypeDeclOf(TypeId typeId) const;
    /// True, если текущий скоуп - локальный (в стеке скоупов есть FuncDecl).
    [[nodiscard]] bool isInLocalScope() const;
    /// True, если текущий узел находится ВНУТРИ тела цикла.
    [[nodiscard]] bool isInLoop() const;
    /// Применяет к базовому типу ортогональные квалификаторы из атрибутов узла.
    TypeId applyRefAttrs(TypeId base, const AstNodeAttr& node, MapperRange range);

    /// Общий контекст семантики (разделяется между драйвером и анализаторами).
    AnalysisContext& m_actx;
    /// Опциональные анализаторы (подписчики событий обхода).
    std::vector<std::unique_ptr<InlineAnalysisHook>> m_hooks;

    /// Компоненты-анализаторы (владеют анализом деклараций/типизацией/доступами/контрактами).
    DeclAnalyzer m_decl;
    ExprTyper m_typer;
    AccessResolver m_access;
    TrustAnalyzer m_trust;

    friend class DeclAnalyzer;
    friend class ExprTyper;
    friend class AccessResolver;
    friend class TrustAnalyzer;
};

} // namespace trust
