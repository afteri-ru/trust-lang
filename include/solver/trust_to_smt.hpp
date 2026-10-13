#pragma once

#include "solver/smt_ast.hpp"
#include "solver/impl/diag.hpp"
#include "solver/vc/sort_mapper.hpp"
#include "solver/vc/vc_plan.hpp"
#include "ir/cfg.hpp"
#include "analysis/modes.hpp"
#include "ir/places.hpp"
#include "ast/token_base.hpp"
#include "diag/report.hpp"
#include "ir/hir_module.hpp"
#include "session/context.hpp"
#include "types/type_id.hpp"

#include <format>
#include <cstddef>
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

/// Значения МЕСТ тела: ключ — `flow::PlaceId` (тот же словарь идентичности, что у flow-анализов).
/// Порядок — порядок регистрации в `PlaceTable` (параметры → блоки в порядке индекса → элементы),
/// поэтому формулы (ite-слияния, havoc-константы) байт-стабильны без сортировки по указателям.
/// Затенение и одноимённые места разных ветвей не смешиваются (ключ — узел объявления).
using VcValues = std::map<flow::PlaceId, SmtTerm>;
/// Генерация SMT-LIB 2 скрипта (верификационных условий контрактов функций) из trust-конструкций.
///
/// Вход — `HirModule` (единственный источник ДАННЫХ: декларации, контракты, материализованные
/// семантикой факты типов/имён/сигнатур) + `MirModule` (СТРУКТУРА тела: CFG/элементы/места).
/// Тела исполняются символически по MIR (см. `smt_vcgen.cpp`), выражения кодирует общий `toTerm`;
/// контракты формулируются на уровне источника. Резолва имён/типов по тексту здесь нет.
///
/// Модель: каждая функция - uninterpreted declare-fun; для функции с пред/пост-условиями
/// формируется VC `(assert (and pre (not post)))`; check-sat: unsat ⇒ контракт выполняется,
/// sat ⇒ контрпример. Код вне области проверки (verification envelope) VC не отменяет тихо:
/// диагностика + счётчик.
class TrustToSmt {
  public:
    /// `modes` — разрешённые поведенческие режимы (данные от `Pipeline`): верификатор НЕ читает
    /// `diag::Options` и не зависит от `semantic`.
    explicit TrustToSmt(Context& ctx, analysis::BehavioralModes modes = {});

    /// Собрать скрипт из HIR-модуля и MIR (CFG тел). nullopt - ВНУТРЕННЯЯ ошибка кодирования
    /// (диагностика Error уже выдана). Код вне области проверки НЕ прерывает экспорт: такая VC
    /// пропускается, выдаётся `-Wverify-envelope`, а `SmtScript::outsideEnvelopeCount` учитывает
    /// пропущенные VC (они не считаются доказанными). Скрипт без команд-условий - контрактов нет.
    std::optional<SmtScript> generate(const HirModule& module, const flow::MirModule& mir);

  private:
    /// Диагностика "solver export: <msg>" (ВНУТРЕННЯЯ ошибка: Error, экспорт прерывается).
    template <typename... Args>
    void report(const AstNodeBase& node, std::format_string<Args...> fmt, Args&&... args) const {
        m_ctx.diag().report(Severity::Error, node.range(), "solver export: {}", std::format(fmt, std::forward<Args>(args)...));
    }
    /// Код вне области проверки: диагностика с severity из `-Wverify-envelope`, VC пропускается.
    template <typename... Args>
    void reportOutsideEnvelope(const AstNodeBase& node, std::format_string<Args...> fmt, Args&&... args) {
        diag::reportWithOptions(m_ctx.diag(), m_ctx.opts(), solver::DiagId::VerifyEnvelope, node.range(), "outside verification envelope: {}",
                                std::format(fmt, std::forward<Args>(args)...));
    }
    void walk(const AstNodeBase* node, const std::map<const FuncDecl*, const flow::MirFunction*>& mirByFunc);
    void processFuncContract(const FuncDecl& f, const flow::MirFunction* body);
    /// Тип-утверждение `MyInt ::= Int32 @{ MyInt > 0 @}` → ∀v:sort. A(v) (2.7).
    void processTypeAssert(const Binary& typeDecl);
    /// Цель (изолированная проверка): вид обязательства обязателен — попадает в отчёт.
    void addGoal(std::optional<SmtTerm>&& vc, MapperRange srcRange, std::string owner, VcObligationKind kind);
    /// Аксиома контракта: глобальное допущение, активное для ЦУЖИХ целей (владелец — `owner`).
    void addAxiom(std::optional<SmtTerm>&& vc, MapperRange srcRange, std::string owner);
    /// Символьное исполнение тела функции на MIR (см. `smt_vcgen.cpp`): цели утверждений/инвариантов
    /// попадают в план, выходы — в VC функции. false - код вне области проверки/внутренняя ошибка.
    bool encodeBody(const FuncDecl& f, const flow::MirFunction& fn, std::vector<SmtTerm> preTerms, std::vector<const AstNodeBase*> postExprs);

    /// Символьный исполнитель тела (определён в `smt_vcgen.cpp`); имеет доступ к приватным членам.
    struct BodyEncoder;

    /// Выражение → SmtTerm. expected - ожидаемый сорт операнда (для согласования литералов/ширин);
    /// state - значения МЕСТ тела (nullptr - нет: резолв параметров/глобалов/связок).
    std::optional<SmtTerm> toTerm(const AstNodeBase* node, const std::optional<SmtSort>& expected, const VcValues* state = nullptr);

    // -- Кодирование выражений по категориям узлов (вынесено из toTerm; см. smt_expr_enc.cpp) --
    std::optional<SmtTerm> encodeIntLiteralTerm(const AstNodeBase* node, const std::optional<SmtSort>& expected);
    std::optional<SmtTerm> encodeFloatLiteralTerm(const AstNodeBase* node);
    std::optional<SmtTerm> encodeIdentTerm(const AstNodeBase* node, const std::optional<SmtSort>& expected, const VcValues* state);
    std::optional<SmtTerm> encodeOperatorTerm(const AstNodeBase* node, const std::optional<SmtSort>& expected, const VcValues* state);
    std::optional<SmtTerm> encodeCallTerm(const AstNodeBase* node, const VcValues* state);
    std::optional<SmtTerm> encodeArrayAccessTerm(const AstNodeBase* node, const VcValues* state);
    std::optional<SmtTerm> encodeArrayInitTerm(const AstNodeBase* node, const VcValues* state);
    std::optional<SmtTerm> encodeTrustElemTerm(const AstNodeBase* node, const std::optional<SmtSort>& expected, const VcValues* state);

    Context& m_ctx;
    /// Поведенческие режимы — ДАННЫЕ (владелец флагов — `semantic`, разрешает `Pipeline`).
    analysis::BehavioralModes m_modes;
    /// План проверки: декларации/аксиомы/цели. SMT-скрипт из него собирает `SmtAssembler`
    /// в фиксированном порядке, поэтому вердикт не зависит от порядка обхода функций.
    VcPlan m_plan;
    /// Таблица мест текущей функции (ключ — узел HIR: параметр/объявление/результат вызова) и
    /// параллельный вектор «PlaceId → узел» (нужен фолбэку по имени и havoc-именам).
    /// Указатели валидны только на время кодирования тела одной функции.
    const flow::PlaceTable<const AstNodeBase*>* m_curPlaces = nullptr;
    const std::vector<const AstNodeBase*>* m_curSlotNodes = nullptr;
    /// PlaceId узла в таблице мест текущей функции; nullopt — узел не является местом.
    [[nodiscard]] std::optional<flow::PlaceId> placeIdOf(const AstNodeBase* node) const;
    /// PlaceId зарегистрированного места (внутренняя ошибка, если узел не зарегистрирован).
    [[nodiscard]] flow::PlaceId slotOf(const AstNodeBase* node) const;
    /// Узел по PlaceId (внутренняя ошибка при выходе за диапазон).
    [[nodiscard]] const AstNodeBase* slotNode(flow::PlaceId id) const;

    /// Компонент маппинга trust-типов → SMT-сорта (sortOf/isSignedType). Типы приходят
    /// материализованными фактами (`TypeId`), резолва имён по тексту здесь нет.
    SortMapper m_sorts;

    // Контекст текущей функции (для пост-условия: имя функции = возвращаемое значение).
    std::string m_curFuncName;
    std::vector<std::string> m_curParams;                         ///< Исходные имена параметров.
    std::unordered_map<std::string, std::string> m_paramSmtNames; ///< Исходное → уникальное SMT-имя.
    std::unordered_map<std::string, TypeId> m_paramTypes;
    std::unordered_map<std::string, MapperRange> m_paramRanges; ///< Исходное имя параметра → его диапазон.
    std::optional<SmtSort> m_curResultSort;                     ///< Сорт результата функции (для пост-условия).
    /// Знак результата функции (kIntegers → знаковый) - для пост-условия (имя функции
    /// = возвращаемое значение). Заполняется в processFuncContract из типа возврата.
    bool m_curResultSigned = true;
    /// Терм возврата функции на выходе из пути (символьное значение); nullopt - void-возврат.
    /// В пост-условии имя функции заменяется этим термом вместо uninterpreted-вызова.
    std::optional<SmtTerm> m_curReturn;
    /// Начальное (входное) состояние функции: параметры → их константы (func_param). Для термина
    /// `@( old, x @)` (старое значение на входе).
    VcValues m_entryState;
    /// Связки кванторов (`forall`/`exists`, z3-термы): имя → терм. Проверяются ПОСЛЕ карты мест:
    /// у bound-переменной нет AST-объявления.
    std::map<std::string, SmtTerm> m_boundNames;
    /// Места, объявленные в текущей функции (из MIR `Declare`): чтение такого места без значения
    /// в состоянии — внутренняя ошибка кодирования, а не свободная константа.
    std::unordered_set<const AstNodeBase*> m_localPlaces;
    /// true - строим аксиому контракта: в пост-условии имя функции → uninterpreted-вызов
    /// (не инлайн тела), параметры → bound-переменные квантора (2.4).
    bool m_buildAxiom = false;
    /// Начальное входное состояние функции для термина `@( old, x @)`.
    /// Число итераций bounded-unrolling для циклов без инварианта (2.6). По умолчанию 3.
    int m_unroll = 3;
    /// Счётчик свежих констант массивов (ArrayInit, 2.8) - z3 4.8 не поддерживает (as const ...).
    unsigned m_arrCounter = 0;
    std::unordered_set<std::string> m_declared; ///< Уже объявленные функции/константы (без дублей).
    /// Символьный маппинг SMT-имя → {trust-имя, диапазон} (параметры func_param, функции,
    /// глобалы). Детерминированный порядок по SMT-имени; копируется в SmtScript::symbolMap.
    std::map<std::string, SmtSymbolRef> m_symbolMap;
    bool m_inPost = false;
    /// Число условий проверки, пропущенных как непроверяемые (копируется в SmtScript).
    std::size_t m_outsideEnvelopeVcs = 0;
};

} // namespace solver
} // namespace trust
