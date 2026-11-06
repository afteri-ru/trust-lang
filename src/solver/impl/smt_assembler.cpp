// Сборка SMT-LIB 2 скрипта из плана проверки (см. include/solver/impl/smt_assembler.hpp).

#include "solver/impl/smt_assembler.hpp"

#include "solver/vc/smt_term_builder.hpp"

#include <memory>
#include <utility>

namespace trust::solver {

namespace sb = SmtTermBuilder;

namespace {

/// Логика по фактическим фичам плана: массивы → AUFBV, кванторы → UFBV, иначе QF_UFBV.
/// `QF_*` требует отсутствия кванторов, иначе решатель отклонит скрипт.
[[nodiscard]] std::string logicOf(const VcPlan& plan) {
    bool usesQuant = false;
    bool usesArray = false;
    const auto scanSort = [&](const std::shared_ptr<SmtSort>& sort) {
        if (sort && sb::sortUsesArray(*sort)) {
            usesArray = true;
        }
    };
    const auto scanTerm = [&](const SmtTerm& term) {
        usesQuant = usesQuant || sb::termUsesQuantifier(term);
        usesArray = usesArray || sb::termUsesArray(term);
    };
    for (const SmtCommand& cmd : plan.declarations) {
        for (const SmtSort& s : cmd.fun_arg_sorts) {
            if (sb::sortUsesArray(s)) {
                usesArray = true;
            }
        }
        scanSort(cmd.fun_result_sort);
        if (cmd.kind == SmtCommandKind::kDefineFun && cmd.fun_body) {
            scanTerm(*cmd.fun_body);
        }
    }
    for (const VcAxiom& axiom : plan.axioms) {
        scanTerm(axiom.term);
    }
    for (const VcGoal& goal : plan.goals) {
        scanTerm(goal.condition);
    }
    return usesArray ? "AUFBV" : (usesQuant ? "UFBV" : "QF_UFBV");
}

} // namespace

SmtScript assemble(VcPlan&& plan) {
    SmtScript script;
    script.logic = logicOf(plan);
    script.commands.reserve(plan.declarations.size() + plan.axioms.size() + plan.goals.size() * 4);
    // 1) Декларации: печатаются ДО всех условий.
    for (const SmtCommand& declaration : plan.declarations) {
        script.commands.push_back(declaration);
    }
    // 2) Цели: каждая проверяется отдельно (иначе unsat одной маскирует sat другой) и получает
    // аксиомы всех функций, КРОМЕ своей собственной.
    for (const VcGoal& goal : plan.goals) {
        SmtCommand push;
        push.kind = SmtCommandKind::kPush;
        push.stack_depth = 1;
        script.commands.push_back(std::move(push));
        for (const VcAxiom& axiom : plan.axioms) {
            if (!goal.owner.empty() && axiom.owner == goal.owner) {
                continue; // собственная аксиома не доказывает собственный контракт
            }
            SmtCommand assumption;
            assumption.kind = SmtCommandKind::kAssert;
            assumption.assert_term = std::make_shared<SmtTerm>(axiom.term);
            assumption.srcRange = axiom.srcRange;
            script.commands.push_back(std::move(assumption));
        }
        SmtCommand assertCmd;
        assertCmd.kind = SmtCommandKind::kAssert;
        assertCmd.assert_term = std::make_shared<SmtTerm>(goal.condition);
        assertCmd.srcRange = goal.srcRange;
        assertCmd.obligation = goal.kind; // наличие вида = «это цель»
        SmtCommand check;
        check.kind = SmtCommandKind::kCheckSat;
        SmtCommand pop;
        pop.kind = SmtCommandKind::kPop;
        pop.stack_depth = 1;
        script.commands.push_back(std::move(assertCmd));
        script.commands.push_back(std::move(check));
        script.commands.push_back(std::move(pop));
    }
    script.symbolMap = std::move(plan.symbolMap);
    script.outsideEnvelopeCount = plan.outsideEnvelopeCount;
    return script;
}

} // namespace trust::solver
