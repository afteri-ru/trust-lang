#pragma once

// include/semantic/pass.hpp
// Общий контекст семантики (AnalysisContext).
// Семантика выполняется единым однопроходным ядром NameResolutionPass
// (см. name_resolution.hpp), к которому параллельно подключаются опциональные
// анализаторы - InlineAnalysisHook (см. inline_hook.hpp), управляемые feature-флагами
// `diag::Options`. Lowering выполняется в конце при отсутствии блокирующих ошибок.
// AnalysisContext владеет единой таблицей символов (SymbolTable) - стеком вложенных
// лексических скоупов, который служит одновременно и реестром объявлений, и
// иерархией вложенности для разрешения имён.

#include "diag/context.hpp"
#include "semantic/symbol_table.hpp"
#include "semantic/symbol_index.hpp"
#include "ast/ast_nodes.hpp"
#include "types/type_id.hpp"

#include <unordered_map>
#include <unordered_set>

namespace trust {

/// Общий контекст семантики: живёт всё время анализа, разделяется между ядром и хуками.
/// Владеет единой таблицей символов SymbolTable (стек вложенных скоупов).
class AnalysisContext {
  public:
    explicit AnalysisContext(Context& ctx);

    Context& ctx() { return m_ctx; }
    const Context& ctx() const { return m_ctx; }

    /// Таблица символов: стек вложенных скоупов (строится ядром, доступна хук-анализаторам).
    SymbolTable& symbols() { return m_symbols; }
    const SymbolTable& symbols() const { return m_symbols; }

    /// Истина, если накоплены ошибки (errorCount() > 0).
    bool hasErrors() const;

    /// Индекс собранных символов (заполняется SymbolCollectorHook при semantic::FlagKind::Symbols).
    SymbolIndex& symbolIndex() { return m_symbolIndex; }
    const SymbolIndex& symbolIndex() const { return m_symbolIndex; }

    // -- Контекст области имён и текущей функции (вывод из скоуп-стека) --
    // namespace-путь и текущая функция выводятся из скоуп-стека SymbolTable
    // (создателей скоупов), а не хранятся в отдельном состоянии. Единый источник
    // для ядра и любого хук-анализатора (все получают AnalysisContext), - чтобы
    // не дублировать разбор областей имён в каждом потребителе.

    /// Путь текущей области имён (например "ns::inner"); пустой - глобальная.
    [[nodiscard]] std::string namespacePath() const;
    /// Полная область имён: "::ns::name::" (глобальная → "::").
    [[nodiscard]] std::string namespaceFull() const;
    /// Ближайшая функция (по скоупам снизу вверх) или nullptr.
    [[nodiscard]] const FuncDecl* currentFunc() const;
    /// Ближайший класс (по скоупам снизу вверх) или nullptr. Класс создаёт скоуп с creator=ClassDecl
    /// (см. analyzeClassDecl); имя класса включается в namespacePath()/@__NAMESPACE__/@__CLASS__.
    [[nodiscard]] const ClassDecl* currentClass() const;
    /// Краткое имя текущей функции (без native-префикса '%').
    [[nodiscard]] std::string funcShortName() const;
    /// Полное имя функции: "ns::name" (квалифицированное областью имён).
    [[nodiscard]] std::string qualifiedFuncName() const;
    /// Требует нахождения внутри функции: иначе диагностика Severity::Error и false.
    [[nodiscard]] bool requireFunction(const AstNodeBase& node, const char* macro) const;

    // -- Резолв типов и runtime-символов (query-сервисы, единые для ядра и хуков) --

    /// Резолвит аннотацию типа (узел kind=TypeName) в TypeId: сначала по скоуп-стеку
    /// (пользовательские алиасы с учётом shadowing), затем в реестре типов (builtin).
    /// std::nullopt - тип не найден (диагностику формирует вызывающий).
    [[nodiscard]] std::optional<TypeId> resolveType(const AstNodeBase& type_node) const;

    /// Разрешённый тип узла (единый источник для ядра и хуков): составное выражение - из кеша
    /// `m_exprTypes` (заполняет ядро пост-порядково через `setExprType`); лист - литерал /
    /// символ (Ident) / каст; объявление и бинарная операция - из поля узла
    /// (VarDecl → inferredType, Binary → resultType). INVALID_TYPE_ID - не выведено.
    [[nodiscard]] TypeId resolvedType(const AstNodeBase& node) const;

    /// Сохраняет тип результата составного выражения в кеш типов (заполняет ядро
    /// пост-порядково; читается `resolvedType` для рекурсивной типизации вложенных узлов).
    void setExprType(const AstNodeBase* node, TypeId id);

    /// Истина, если для узла-объявления (decl) уже выдан «чтение до инициализации» (см.
    /// pass.cpp). Используется для дедупа диагностики «cannot infer type» на нетипизированных
    /// локальных `:= _`: если переменная уже прочитана до записи (=> Error), отдельная ошибка
    /// «не выведен тип» не дублируется. mutable: наполняется из const resolvedType.
    bool uninitVarReported(const AstNodeBase* decl) const;

    /// Следующий уникальный id синтезируемой временной scrutinee match (для имени `_matchN`).
    /// Семантика создаёт временную как const VarDecl (инвариант «временные — уровень анализатора»);
    /// счётчик здесь, а не в транспиляторе.
    int nextMatchTempId() noexcept { return ++matchTempCounter; }

    /// Следующий уникальный id синтезируемой временной hoist возврата (для имени `__trust_res_N`).
    /// Семантика создаёт её как const VarDecl для именованного return с пост-условиями
    /// (инвариант «временные — уровень анализатора»); счётчик здесь, а не в транспиляторе.
    int nextResultTempId() noexcept { return ++resultTempCounter; }

    /// Строит функциональный тип (FunctionTypeId) по сигнатуре функции через TypeRegistry.
    [[nodiscard]] TypeId buildFuncType(const FuncDecl& func_node) const;

    /// Истина, если имя (с native-префиксом '%' или без) - зарегистрированный runtime-символ
    /// (например %trust::trust__abort__ / %trust::formatMessage). Такие имена - известные
    /// нативные функции из публичного runtime-заголовка и не должны давать «undefined name».
    [[nodiscard]] bool isRegisteredRuntimeSymbol(std::string_view name) const;

    /// Истина, если имя - интринсик языка (например `trust::intrinsic_assert`; без native-префикса
    /// '%' - интринсик НЕ нативная функция). Интринсики распознаются компилятором и разворачиваются
    /// на этапе генерации (CppTranspiler::emitIntrinsic), поэтому не должны давать «undefined name».
    [[nodiscard]] bool isRegisteredIntrinsic(std::string_view name) const;

  private:
    Context& m_ctx;
    SymbolTable m_symbols;

    /// Кеш типов результатов выражений (node → TypeId), заполняется ядром пост-порядково
    /// (setExprType) и читается `resolvedType` для рекурсивной типизации вложенных выражений.
    std::unordered_map<const AstNodeBase*, TypeId> m_exprTypes;
    /// Узлы, для которых уже выдан «чтение до инициализации» (дедуп дублирующих resolvedType/сканов
    /// на одном узле, чтобы один сайт чтения давал одну диагностику). mutable: наполняется из const
    /// resolvedType.
    mutable std::unordered_set<const AstNodeBase*> m_uninitReadReported;
    /// Объявления, для которых уже выдан «чтение до инициализации» (decl → узел объявления),
    /// для дедупа диагностики «cannot infer type» (см. uninitVarReported). mutable: из resolvedType.
    mutable std::unordered_set<const AstNodeBase*> m_uninitVarReportedDecls;
    /// Счётчик синтезируемых временных scrutinee match (`_matchN`) — семантика создаёт их как
    /// const VarDecl (инвариант «временные — уровень анализатора»), счётчик здесь, а не в транспиляторе.
    int matchTempCounter = 0;
    /// Счётчик синтезируемых временных hoist возврата (`__trust_res_N`) — семантика создаёт их как
    /// const VarDecl (инвариант «временные — уровень анализатора»), счётчик здесь, а не в транспиляторе.
    int resultTempCounter = 0;
    /// Собранные символы (имя → тип/диапазоны) для LSP; заполняется SymbolCollectorHook.
    SymbolIndex m_symbolIndex;
};

} // namespace trust
