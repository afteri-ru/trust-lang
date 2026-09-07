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

    // -- Definite-assignment (ветвящиеся конструкты if/else-if/else и match) --
    // ЕДИНЫЙ механизм для всех пер-Symbol флагов (kSymbolFlagsMask: Inferred/Const/Uninit).
    // На входе creator-скоупа снапшотим маску флагов переменных; каждый «arm» ветвления
    // (для if: условие + тело; для else-if: своё условие + тело; для match: scrutinee/паттерны +
    // тело case) пересматривается ОТ снапшота (переопределение флагов в отдельном скоупе с
    // сохранением исходных - изменения в одной ветке не текут в другие); после завершения всех
    // вложенных скоупов - merge по покрытию путей (см. applyMergeFlags): Uninit (не инициализирована)
    // - OR выходов веток («хоть один путь оставил неинициализированной»; без else/default учитывается
    // путь entry), Const/Inferred (свойство «на всех путях») - AND выходов веток (+ путь entry).
    void analyzeBranchConstruct(AstNodePtr& self);
    /// Снапшот маски пер-Symbol флагов (kSymbolFlagsMask) всех символов текущего скоуп-стека.
    std::vector<std::pair<Symbol*, TypeId>> snapshotSymbolFlags();
    /// Восстановить маску флагов по снапшоту (сбросить эффекты проанализированной ветки).
    void restoreSymbolFlags(const std::vector<std::pair<Symbol*, TypeId>>& snap);
    /// Обработать тело одной ветки («arm») из состояния entry изолированно (restore → analyze →
    /// capture) и добавить маску её выхода в pathExitMasks (выровнена по entry).
    void analyzeBranchBody(AstNodePtr& body, const std::vector<std::pair<Symbol*, TypeId>>& entry, std::vector<std::vector<TypeId>>& pathExitMasks);
    /// Definite-assignment для циклов while/do-while (недоказуемый путь): снапшот на входе,
    /// анализ тела из entry, merge консервативно - переменная после цикла инициализирована,
    /// только если гарантированно на всех путях (для while обязателен и путь «0 итераций» =
    /// entry). Иначе остаётся Uninit (чтение после цикла → Error, не тихий пропуск).
    void analyzeLoopConstruct(AstNodePtr& self, bool isDoWhile);
    /// Применить merged покрытие флагов по путям к символам снапшота. pathExitMasks[path][i] -
    /// маска пер-Symbol флагов символа snap[i] на выходе пути path (выровнена по snap).
    void applyMergeFlags(const std::vector<std::pair<Symbol*, TypeId>>& snap, const std::vector<std::vector<TypeId>>& pathExitMasks, bool hasFallback);

    // Вспомогательные: вход/выход скоупа с уведомлением хуков.
    void enterScope(const AstNodeBase& node);
    void exitScope();
    /// При выходе из скоупа (перед pop) проверить объявленные в нём нетипизированные локальные
    /// `x := _;`, так и не получившие тип из записей (structuralType == INVALID) → Error
    /// «cannot infer type … annotate or remove» (это НЕ -Wunused-variable, типа нет).
    void finishUntypedUnderscoreDecls();

    /// Единый алгоритм разрешения простого имени (имя без сигила/квалификатора): `x` ищется
    /// сначала как локальная `$x` (если такой символ есть в текущем/охватывающем скоупе - текст
    /// узла-ссылки нормализуется на `$x`). Квалифицированные/сигилные/нативные имена - без изменений.
    /// Если прямого имени нет, работает fallback оператора `... = X` (using): пробуется
    /// `prefix::name` для зарегистрированных областей имён. node == nullptr - read-only
    /// (чтение dims/типов до обхода детей, без нормализации текста узла).
    Symbol* resolveSimple(AstNodeBase* node, std::string_view name);
    /// Обработка оператора `... = X` (using): регистрирует область/области имён RHS
    /// как префиксы поиска в текущем скоупе. Возвращает true, если узел обработан
    /// (AssignOp с левым Ellipsis) - RHS как значение не резолвится.
    bool handleUsingImport(const AstNodeBase& self);
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
    /// Проверка встроенного маркера `@__CHECK_AREA__`: валидирует текущую область (из
    /// единого скоуп-стека - создатели скоупов) и атрибуты области, затем удаляет маркер.
    void analyzeCheckAreaStmt(AstNodePtr& self);
    /// Проверка атрибута `@[matcher("fn")]` на операторе match: резолвит имя функции-предиката
    /// `bool fn(T_value, T_pattern)` и проверяет её объявление (арность=2, возврат bool). Вызывается
    /// пост-порядково для MatchingStmt (типы scrutinee/pattern уже известны). Код не генерирует.
    void analyzeMatchMatcher(MatchStmt& match);
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
