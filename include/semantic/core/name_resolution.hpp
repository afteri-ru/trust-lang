#pragma once

// include/semantic/core/name_resolution.hpp
// Единый однопроходный проход разрешения имён (обязательное ядро семантики).
// NameResolutionPass - ДРАЙВЕР (издатель событий): владеет обходом AST, скоуп-стеком и
// подключёнными InlineAnalysisHook. Тяжёлая анализ-логика - в компонентах
// DeclAnalyzer/ExprTyper/AccessResolver/TrustAnalyzer (см. *_analyzer.hpp/*_analyzer.cpp),
// разделяющие AnalysisContext (m_actx) и обращающиеся к драйверу (рекурсия/скоупы/резолв имён)
// через ссылку m_core (дружба). Публичный контракт и модель «драйвер + хуки» сохранены.

#include "semantic/core/pass.hpp"
#include "semantic/core/inline_hook.hpp"
#include "semantic/decl/decl_analyzer.hpp"
#include "semantic/typer/expr_typer.hpp"
#include "semantic/typer/access_resolver.hpp"
#include "semantic/verify/trust_analyzer.hpp"
#include "ast/ast_nodes.hpp"
#include "ast/kind_dispatch.hpp"
#include "location/module_index.hpp"

#include <array>
#include <cstddef>
#include <functional>
#include <memory>
#include <string>
#include <string_view>
#include <unordered_map>
#include <unordered_set>
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

    // -- Traversal API для анализаторов (контракт драйвера; дружба не нужна) --
    void analyzeNode(AstNodePtr& self);
    void enterScope(const AstNodeBase& node);
    void exitScope();
    Symbol* resolveSimple(AstNodeBase* node, std::string_view name);
    QualType applyRefAttrs(QualType base, const AstNodeAttr& node, MapperRange range);
    std::vector<std::unique_ptr<InlineAnalysisHook>>& hooks() noexcept { return m_hooks; }
    ExprTyper& typer() noexcept { return m_typer; }
    TrustAnalyzer& trust() noexcept { return m_trust; }

  private:
    /// Обход реальных детей через единый источник AstNodeBase::collectChildren
    /// (ссылки на слоты, чтобы хук мог заменять узлы). Не открывает скоупы -
    /// это делает analyzeNode.
    void analyzeChildren(AstNodePtr& self);

    // -- Kind-диспетчеризация partial pass (ast/kind_dispatch.hpp) --
    // ОДНА таблица: default doTraverse (обход детей + пост-обработка), override-ы - полное
    // действие фазы для группы kinds. `consumed` - узел заменён хуком: leaf-действие
    // пропускается, обход детей сохраняется. `END` - reject, а не default-обход.
    using NodeHandler = void (NameResolutionPass::*)(AstNodePtr&, bool consumed);
    using NodeHandlerTable = std::array<NodeHandler, kParserKindSlotCount>;

    static const NodeHandlerTable& semaActions();
    /// Явный reject для END: END - не узел (см. ast/MEMORY.md).
    void doReject(AstNodePtr& self, bool consumed);

    // Групповые обработчики: каждый - полное действие фазы для группы kinds.
    void doModuleDecl(AstNodePtr& self, bool consumed);
    void doScopeBlock(AstNodePtr& self, bool consumed);
    void doScopeContainer(AstNodePtr& self, bool consumed);
    void doLoop(AstNodePtr& self, bool consumed);
    void doDoWhile(AstNodePtr& self, bool consumed);
    void doWith(AstNodePtr& self, bool consumed);
    void doFuncDecl(AstNodePtr& self, bool consumed);
    void doSkip(AstNodePtr& self, bool consumed);
    void doCatch(AstNodePtr& self, bool consumed);
    void doDestructure(AstNodePtr& self, bool consumed);
    void doDict(AstNodePtr& self, bool consumed);
    void doArray(AstNodePtr& self, bool consumed);
    void doRange(AstNodePtr& self, bool consumed);
    void doCheckArea(AstNodePtr& self, bool consumed);
    void doDebug(AstNodePtr& self, bool consumed);
    void doTrustContract(AstNodePtr& self, bool consumed);
    void doTrustElem(AstNodePtr& self, bool consumed);
    void doAccess(AstNodePtr& self, bool consumed);
    void doAssign(AstNodePtr& self, bool consumed);
    void doBranch(AstNodePtr& self, bool consumed);
    void doTraverse(AstNodePtr& self, bool consumed);

    // Групповые обработчики: leaf-действие + стандартный обход детей/пост-обработка.
    void doVarDecl(AstNodePtr& self, bool consumed);
    void doTypeDecl(AstNodePtr& self, bool consumed);
    void doReturnStmt(AstNodePtr& self, bool consumed);
    void doIdentOrArg(AstNodePtr& self, bool consumed);
    void doEmbedExpr(AstNodePtr& self, bool consumed);
    void doAppendStmt(AstNodePtr& self, bool consumed);

    /// Единая диагностика неразрешённой псевдопеременной `$^` (Ident/ArgNode).
    void reportResultCaptureError(const AstNodePtr& self);

    // Leaf-действия (без рекурсии); вызываются только из групповых обработчиков.
    void handleVarDecl(AstNodePtr& self);
    void handleTypeDecl(AstNodePtr& self);
    void handleReturnStmt(AstNodePtr& self);
    void handleIdent(AstNodePtr& self);
    void handleEmbedExpr(AstNodePtr& self);
    void handleAppendNode(AstNodePtr& self);

    /// Стандартное завершение обработчика: обход детей + пост-порядковая обработка.
    void finishTraverse(AstNodePtr& self);

    // -- Обработчики групп узлов обхода (name_resolution_nodes.cpp) --
    /// Скоуп-контейнеры (ModuleDecl/sequence/ScopeBlock/TryCatchStmt): вложенный скоуп на тело.
    void analyzeScopeContainer(AstNodePtr& self);
    /// Оператор with(...){...}else{...} (RAII-менеджер контекста): биндинги, тело, ветка else.
    void analyzeWithStmt(AstNodePtr& self);
    /// Объявление функции/лямбды/нативного шаблона-типа.
    void analyzeFuncDeclNode(AstNodePtr& self);
    /// Ветка catch(...) : отдельный вложенный скоуп + связывание переменной.
    void analyzeCatchBlockNode(AstNodePtr& self);
    /// Термин решателя trust-контракта @( term, args... @); для кванторов - связка.
    void analyzeTrustElemNode(AstNodePtr& self);
    /// Пост-порядковая обработка узла: типизация + синтетические временные ($^ / match / return).
    void analyzeNodeTail(AstNodePtr& self);

    // Вспомогательные: вход/выход скоупа с уведомлением хуков.
    /// Проверка зарегистрированной области поиска (`... = X`): `using namespace` принимает ТОЛЬКО
    /// namespace-области. Тип владеет членским скоупом (`T::member`) и отклоняется; значение/функция —
    /// тоже; отсутствующая область — «нет имён».
    void checkSearchScope(const UsingScopeRecord& search);
    /// Ключ сайта импорта в скоупе: импортёр + модуль части + нормализованные маски.
    /// ЕДИНЫЙ типизированный формат идентичности сайта.
    struct ImportSiteKey {
        ModuleIndex importer{};
        ModuleIndex imported{};
        std::string masksKey;

        bool operator==(const ImportSiteKey&) const = default;
    };

    struct ImportSiteKeyHash {
        [[nodiscard]] std::size_t operator()(const ImportSiteKey& key) const noexcept {
            const std::size_t importerHash = std::hash<ModuleIndex>{}(key.importer);
            const std::size_t importedHash = std::hash<ModuleIndex>{}(key.imported);
            const std::size_t masksHash = std::hash<std::string_view>{}(key.masksKey);
            return importerHash ^ (importedHash * 0x9e3779b9u) ^ (masksHash << 1);
        }
    };

    /// Ключи импорт-сайтов текущего scope: (moduleIndex|normalized masks) -> сайт.
    /// Очищаются при выходе из scope; повторный ключ — duplicate-import.
    std::unordered_map<std::size_t, std::unordered_set<ImportSiteKey, ImportSiteKeyHash>> m_importKeys;

    /// Ссылка, разрешённая в объявление ДРУГОГО модуля (алиас функции, реэкспорт): текст узла
    /// нормализуется на имя объявления, `moduleRef` — на модуль-владелец (иначе кодоген напечатал
    /// бы несуществующий C++-символ по имени-алиасу).
    void normalizeResolvedRef(AstNodeBase* node, const Symbol& sym);
    /// Обработка оператора `... = X` (using): регистрирует область/области имён RHS
    /// как префиксы поиска в текущем скоупе. Возвращает true, если узел обработан
    /// (AssignOp с левым Ellipsis) - RHS как значение не резолвится.
    bool handleUsingImport(const AstNodeBase& self);
    /// Резолв имени узла с диагностикой «undefined name» (единый источник для компонентов).
    const Symbol* lookupOrError(AstNodeBase& node);
    /// `using`-резолв уже выдал диагностику неоднозначности для текущего узла: повторное
    /// «undefined name» не печатается (иначе одна ошибка даёт две строки).
    bool m_usingAmbiguityReported = false;
    /// Проверка встроенного маркера `@__CHECK_AREA__`: валидирует текущую область (из
    /// единого скоуп-стека - создатели скоупов) и атрибуты области, затем удаляет маркер.
    void analyzeCheckAreaStmt(AstNodePtr& self);
    /// Учёт `[]=` в статическом размере словаря: `d []= v` увеличивает известный размер (dims);
    /// spread-merge `d []= ... dict` переносит размер/типы полей операнда. LHS — простой Ident.
    void handleAppendStmt(const Binary& append);

    /// Обработка встроенных системных маркеров отладочного вывода (УРОВЕНЬ 1): `@__DEBUG__(...)`
    /// устанавливает (пустой => сбрасывает) фильтр вывода; `@__DEBUG_SCOPE__(...)` печатает дамп
    /// состояния скоупа анализатора в этой точке (если ключевые слова проходят под фильтр).
    /// Маркер кода не генерирует и УДАЛЯЕТСЯ после обработки.
    void analyzeDebugStmt(AstNodePtr& self);
    /// Проверка атрибута `@[matcher("fn")]` на операторе match: резолвит имя функции-предиката
    /// `bool fn(T_value, T_pattern)` и проверяет её объявление (арность=2, возврат bool). Вызывается
    /// пост-порядково для MatchingStmt (типы scrutinee/pattern уже известны). Код не генерирует.
    void analyzeMatchMatcher(MatchStmt& match);

    /// Общий контекст семантики (разделяется между драйвером и анализаторами).
    AnalysisContext& m_actx;
    /// Опциональные анализаторы (подписчики событий обхода).
    std::vector<std::unique_ptr<InlineAnalysisHook>> m_hooks;

    /// Компоненты-анализаторы (владеют анализом деклараций/типизацией/доступами/контрактами).
    DeclAnalyzer m_decl;
    ExprTyper m_typer;
    AccessResolver m_access;
    TrustAnalyzer m_trust;

};

} // namespace trust
