#pragma once

#include "solver/smt/smt_op.hpp"
#include "location/location.hpp"

#include <cstddef>
#include <cstdint>
#include <memory>
#include <optional>
#include <string>
#include <utility>
#include <variant>
#include <vector>

namespace trust {
namespace solver {

/// Sort (type) in SMT-LIB 2
enum class SmtSortKind {
    kBool,
    kInt,
    kReal,
    kBitVec,
    kArray,
    kUninterpreted,
};

struct SmtSort {
    SmtSortKind kind = SmtSortKind::kBool;
    std::string name;                ///< for kUninterpreted
    uint32_t bv_width = 0;           ///< for kBitVec
    std::shared_ptr<SmtSort> domain; ///< for kArray
    std::shared_ptr<SmtSort> range;  ///< for kArray
};

/// Вид обязательства проверки (источник цели). Источники — только те, что реально порождают
/// цели в плане; рантайм-проверки операций появятся отдельными видами вместе с backlog D1/D2.
/// Единый словарь для плана (`VcGoal`), скрипта (`SmtCommand`) и отчёта (`VcCheckResult`).
#define SOLVER_OBLIGATION_LIST(M)                 \
    M(kPost, "постусловие")                       \
    M(kAssert, "утверждение")                     \
    M(kInvariantEstablishment, "инвариант цикла (вход)")     \
    M(kInvariantPreservation, "инвариант цикла (сохранение)") \
    M(kCallPre, "предусловие вызова")             \
    M(kTypeAssert, "утверждение типа")

#define SOLVER_OBLIGATION_ENUM(name, label) name,
enum class VcObligationKind { SOLVER_OBLIGATION_LIST(SOLVER_OBLIGATION_ENUM) };
#undef SOLVER_OBLIGATION_ENUM

#define SOLVER_OBLIGATION_LABEL(name, label) label,
inline constexpr std::string_view kVcObligationLabels[] = {SOLVER_OBLIGATION_LIST(SOLVER_OBLIGATION_LABEL)};
#undef SOLVER_OBLIGATION_LABEL
#undef SOLVER_OBLIGATION_LIST

/// Человекочитаемая метка вида обязательства (для отчёта). Известное значение обязательно.
[[nodiscard]] inline constexpr std::string_view vcObligationLabel(VcObligationKind kind) noexcept {
    const int idx = static_cast<int>(kind);
    return (idx >= 0 && idx < static_cast<int>(std::size(kVcObligationLabels))) ? kVcObligationLabels[idx] : "обязательство";
}

/// Command kind in SMT-LIB 2
enum class SmtCommandKind {
    kDeclareSort,
    kDeclareFun,
    kDefineFun,
    kAssert,
    kCheckSat,
    kGetModel,
    kGetValue,
    kExit,
    kSetLogic,
    kPush,
    kPop,
};

/// Term (expression) nodes
enum class SmtTermKind {
    kConst,    ///< constant value
    kVar,      ///< variable by index/de Bruijn
    kNamedVar, ///< variable by name
    kApp,      ///< function application
    kForall,   ///< forall quantifier
    kExists,   ///< exists quantifier
    kLet,      ///< let expression
};

struct SmtTerm {
    SmtTermKind kind = SmtTermKind::kConst;

    /// Сорт (тип) терма - ОБЯЗАТЕЛЬНЫЙ инвариант: каждый терм несёт корректный сорт.
    /// Заполняет построитель (TrustToSmt) через единый маппинг типов; бэкенды (SmtPrinter,
    /// SolverZ3) читают его, не выводя из синтаксиса. По умолчанию Bool (SmtSort).
    SmtSort sort;

    /// Встроенный оператор (из X-макроса SOLVER_OPERATOR_LIST). nullopt - пользовательская
    /// функция (произвольное имя в fun_name). Заполняется построителем через parseSmtOp(fun).
    std::optional<SmtOp> op;

    // Constant
    std::string const_value;

    // Variable (named)
    std::string var_name;
    int var_index = -1;

    // Function application
    std::string fun_name;
    std::vector<std::shared_ptr<SmtTerm>> args;

    // SignExt/ZeroExt: количество бит расширения (unary-оператор SignExt/ZeroExt).
    uint32_t ext_amount = 0;

    // Quantifier
    std::vector<std::string> quant_vars;
    /// Сорта bound-переменных квантора - ОБЯЗАТЕЛЬНЫЙ инвариант для kForall/kExists:
    /// параллелен quant_vars (по одному сорту на каждую переменную). Заполняет построитель.
    std::vector<SmtSort> quant_var_sorts;
    std::shared_ptr<SmtTerm> quant_body;

    // Let
    std::vector<std::pair<std::string, std::shared_ptr<SmtTerm>>> let_bindings;
    std::shared_ptr<SmtTerm> let_body;

    /// Диапазон trust-узла, из которого построен терм (для source-привязки к .smt2.map/LSP).
    /// Наследуется во всех kApp/kConst/kNamedVar; заполняет построитель (TrustToSmt).
    MapperRange srcRange;
};

/// Символьный маппинг SMT-имени на trust-источник (для отчёта о контрпримере/LSP).
struct SmtSymbolRef {
    std::string trustName; ///< Исходное имя в trust-коде (напр. x, add).
    MapperRange srcRange;  ///< Диапазон trust-узла, породившего символ.
};

/// SMT-LIB 2 command
struct SmtCommand {
    SmtCommandKind kind = SmtCommandKind::kCheckSat;

    // Declare-sort
    std::string sort_name;
    uint32_t sort_arity = 0;

    // Declare-fun / define-fun
    std::string fun_name;
    std::vector<SmtSort> fun_arg_sorts;
    std::shared_ptr<SmtSort> fun_result_sort; ///< for kUninterpreted sorts we need heap
    std::shared_ptr<SmtTerm> fun_body;        ///< for define-fun

    // Assert
    std::shared_ptr<SmtTerm> assert_term;

    // Get-value
    std::vector<std::shared_ptr<SmtTerm>> get_value_terms;

    // Push/Pop
    uint32_t stack_depth = 1;

    // Set-logic
    std::string logic_name;

    /// Для изолированной цели: вид обязательства (для отчёта `file:line: <вид>`). nullopt —
    /// команда не является целью.
    std::optional<VcObligationKind> obligation;

    /// Диапазон trust-конструкции, из которой построена команда (для kAssert - что породило VC;
    /// для kDeclareFun - объявление параметра/функции). Заполняет построитель (TrustToSmt).
    MapperRange srcRange;
};

/// A complete SMT-LIB 2 script
struct SmtScript {
    std::string logic;
    std::vector<SmtCommand> commands;

    /// Символьный маппинг SMT-имя → {trust-имя, диапазон} (параметры func_param, функции,
    /// глобалы). Собирается построителем (TrustToSmt); используется для .smt2.map и LSP.
    std::vector<std::pair<std::string, SmtSymbolRef>> symbolMap;

    /// Сколько условий проверки НЕ проверялось, потому что их код вне области проверки
    /// (диагностика `-Wverify-envelope` уже выдана). Такие VC не считаются доказанными.
    std::size_t outsideEnvelopeCount = 0;
};

} // namespace solver
} // namespace trust