#pragma once

#include "solver/smt_ast.hpp"
#include "solver/sort_mapper.hpp"
#include "ast/token_base.hpp"
#include "diag/context.hpp"
#include "types/type_id.hpp"

#include <format>
#include <map>
#include <optional>
#include <string>
#include <unordered_map>
#include <unordered_set>
#include <utility>
#include <vector>

namespace trust {

class Context;
class AstNodeBase;
class FuncDecl;

namespace solver {

/// Генерация SMT-LIB 2 скрипта (верификационных условий контрактов функций) из trust-конструкций.
///
/// Мост TrustLang-AST → SmtScript: переводит пред/пост-условия и утверждения функций в SMT-LIB 2.
/// Сорта типов - из TypeRegistry (целые → BitVec, вещественные → Real, Bool → Bool, пользовательские
/// → uninterpreted). Типы параметров/возврата - из АННОТАЦИЙ AST + реестра (как транспилятор, без
/// зависимости от вытолкнутого после анализа скоуп-стека). Работает БЕЗ Z3 (только текст; для
/// `--solver-mode=calculate` скрипт исполняется SolverInterface снаружи).
///
/// Модель (фаза 1): каждая функция - uninterpreted declare-fun; для функции с пред/пост-условиями
/// формируется VC `(assert (and pre (not post)))`; check-sat: unsat ⇒ контракт выполняется, sat ⇒
/// контрпример. Утверждения, ссылающиеся на состояние/тело (автономные, на переменной/типе), а также
/// вызовы/члены/строки в условиях - вне фазы 1: выдают диагностику (не тихий пропуск).
/// Результат кодирования блока/тела (SSA): состояние переменных, терм возврата, утверждения.
struct BlockResult {
    std::unordered_map<std::string, SmtTerm> state;
    std::optional<SmtTerm> ret;
    std::vector<SmtTerm> asserts;
};

class TrustToSmt {
  public:
    explicit TrustToSmt(Context& ctx);

    /// Собрать скрипт из trust-конструкций модуля. nullopt - ошибка/неподдерживаемая конструкция
    /// (диагностика уже выдана). Скрипт без команд-условий - контрактов нет.
    std::optional<SmtScript> generate(const std::vector<AstNodePtr>& astNodes);

  private:
    /// Диагностика "solver export: <msg>". Формат - std::format (как у diag().report).
    template <typename... Args>
    void report(const AstNodeBase& node, std::format_string<Args...> fmt, Args&&... args) const {
        m_ctx.diag().report(Severity::Error, node.range(), "solver export: {}", std::format(fmt, std::forward<Args>(args)...));
    }
    void walk(const AstNodeBase* node);
    void processFuncContract(const FuncDecl& f);
    /// Тип-утверждение `MyInt ::= Int32 @{ MyInt > 0 @}` → ∀v:sort. A(v) (2.7).
    void processTypeAssert(const Binary& typeDecl);
    void addAssert(std::optional<SmtTerm>&& vc, MapperRange srcRange, bool isolated = true);
    /// Кодирует тело функции (последовательность statements) в термы значений переменных (SSA),
    /// терм возврата и утверждения (автономные/переменные). Возвращает ret; state/asserts ->
    /// m_curReturn/m_bodyAsserts (методы пишут напрямую). nullopt ret - нет return/void/ошибка.
    std::optional<SmtTerm> encodeBody(const FuncDecl& f);
    /// Кодирует последовательность statements от входного состояния; мутабельные ветки (if)
    /// кодируются от копий состояния и сливаются через ite.
    BlockResult encodeBlock(const std::vector<AstNodePtr>& nodes, const std::unordered_map<std::string, SmtTerm>& inState, const std::optional<SmtTerm>& inRet);

    /// Знак целочисленного выражения: 1 - знаковый (kIntegers), 0 - беззнаковый (kUnsigned),
    /// -1 - неизвестно (литерал/INVALID/глобал). Рекурсивно по выражению: знак результата
    /// арифметики/битовой операции следует знаку операндов-переменных; узлы сравнения/логики не
    /// дают знак. Нужен для выбора знаковых/беззнаковых BV-операторов (типы вложенных выражений
    /// и литералов в узле сравнения приходят как INVALID/Bool - полагаться на них нельзя).
    int exprSign(const AstNodeBase* node) const;
    /// Выражение → SmtTerm. expected - ожидаемый сорт операнда (для согласования литералов/ширин);
    /// state - текущее SSA-состояние переменных тела (nullptr - нет, резолв параметров/глобалов).
    std::optional<SmtTerm> toTerm(const AstNodeBase* node, const std::optional<SmtSort>& expected,
                                  const std::unordered_map<std::string, SmtTerm>* state = nullptr);

    Context& m_ctx;
    SmtScript m_script;
    /// Компонент маппинга trust-типов → SMT-сорта (sortOf/isSignedType/resolveTypeByName).
    SortMapper m_sorts;

    // Контекст текущей функции (для пост-условия: имя функции = возвращаемое значение).
    std::string m_curFuncName;
    std::vector<std::string> m_curParams;                         ///< Исходные имена параметров.
    std::unordered_map<std::string, std::string> m_paramSmtNames; ///< Исходное → уникальное SMT-имя.
    std::unordered_map<std::string, TypeId> m_paramTypes;
    std::unordered_map<std::string, MapperRange> m_paramRanges; ///< Исходное имя параметра → его диапазон.
    std::optional<SmtSort> m_curResultSort;                     ///< Сорт результата функции (для пост-условия).
    /// Знак результата функции (kIntegers → знаковый) - для пост-условия/exprSign (имя функции
    /// = возвращаемое значение). Заполняется в processFuncContract из типа возврата.
    bool m_curResultSigned = true;
    /// Терм возврата функции при кодировании тела (SSA); nullopt - не кодировали тело/нет return.
    /// В пост-условии имя функции заменяется этим термом вместо uninterpreted-вызова.
    std::optional<SmtTerm> m_curReturn;
    /// Начальное (входное) состояние функции: параметры → их константы (func_param). Для термина
    /// `@( old, x @)` (старое значение на входе). Заполняется в encodeBody до кодирования условий.
    std::unordered_map<std::string, SmtTerm> m_entryState;
    /// Утверждения, собранные при кодировании тела (автономные `@{...@};` и переменные
    /// `y trust_assert(...) := ...`). Добавляются в консеквенты VC функции (2.3).
    std::vector<SmtTerm> m_bodyAsserts;
    /// Карта имя функции → FuncDecl модуля (пре-скан generate; для вызовов/аксиом контрактов 2.4).
    std::unordered_map<std::string, const FuncDecl*> m_funcDecls;
    /// true - строим аксиому контракта: в пост-условии имя функции → uninterpreted-вызов
    /// (не инлайн тела), параметры → bound-переменные квантора (2.4).
    bool m_buildAxiom = false;
    /// Число итераций bounded-unrolling для циклов без инварианта (2.6). По умолчанию 3.
    int m_unroll = 3;
    /// Счётчик свежих констант массивов (ArrayInit, 2.8) - z3 4.8 не поддерживает (as const ...).
    unsigned m_arrCounter = 0;
    std::unordered_set<std::string> m_declared; ///< Уже объявленные функции/константы (без дублей).
    /// Символьный маппинг SMT-имя → {trust-имя, диапазон} (параметры func_param, функции,
    /// глобалы). Детерминированный порядок по SMT-имени; копируется в SmtScript::symbolMap.
    std::map<std::string, SmtSymbolRef> m_symbolMap;
    bool m_inPost = false;
};

} // namespace solver
} // namespace trust
