// test/unit/solver/smt_assembler_test.cpp
// Порядок сборки скрипта из плана проверки: декларации → (push, аксиомы ЧУЖИХ функций,
// assert цели, check-sat, pop). Собственная аксиома функции в её цель не попадает.
#include "solver/impl/smt_assembler.hpp"
#include "solver/vc/smt_term_builder.hpp"
#include "solver/solver.hpp"

#include <gtest/gtest.h>

#include <memory>
#include <string>

namespace trust::solver {
namespace {

using namespace SmtTermBuilder;

SmtTerm namedBool(const char* name) {
    return makeNamedVar(name, boolSort(), MapperRange{});
}

/// Записывающий бэкенд: проверяет, что `runScript` ИСПОЛНЯЕТ define-fun (а не пропускает его).
class RecordingSolver : public SolverInterface {
  public:
    void setLogic(const std::string&) override {}
    void declareSort(const std::string&, uint32_t) override {}
    void declareFun(const std::string&, const std::vector<SmtSort>&, const SmtSort&) override {}
    void defineFun(const std::string& name, const std::vector<SmtSort>&, const SmtSort&, const SmtTerm&) override { defined.push_back(name); }
    void assertFormula(const SmtTerm&) override {}
    SolverResult checkSat() override { return SolverResult::kUnsat; }
    std::optional<std::string> getModelValue(const std::string&, const SmtSort&) override { return std::nullopt; }
    void push(uint32_t) override {}
    void pop(uint32_t) override {}
    std::string getSmtLibText() const override { return {}; }
    void reset() override {}

    std::vector<std::string> defined;
};

} // namespace

TEST(SolverScript, ExecutesDefineFun) {
    SmtScript script;
    SmtCommand definition;
    definition.kind = SmtCommandKind::kDefineFun;
    definition.fun_name = "helper";
    definition.fun_arg_sorts = {boolSort()};
    definition.fun_result_sort = std::make_shared<SmtSort>(boolSort());
    definition.fun_body = std::make_shared<SmtTerm>(makeConst("true", boolSort()));
    script.commands.push_back(definition);
    SmtCommand check;
    check.kind = SmtCommandKind::kCheckSat;
    script.commands.push_back(check);

    RecordingSolver solver;
    VerificationReport report;
    EXPECT_EQ(runScript(solver, script, nullptr, &report), SolverResult::kUnsat);
    ASSERT_EQ(solver.defined.size(), 1u);
    EXPECT_EQ(solver.defined[0], "helper");
}

TEST(SmtAssembler, EmitsDeclarationsThenGoalsWithForeignAxiomsOnly) {
    VcPlan plan;
    SmtCommand declaration;
    declaration.kind = SmtCommandKind::kDeclareFun;
    declaration.fun_name = "f_x";
    declaration.fun_result_sort = std::make_shared<SmtSort>(boolSort());
    plan.declarations.push_back(declaration);
    plan.axioms.push_back(VcAxiom{namedBool("axiom_a"), MapperRange{}, "A"});
    plan.axioms.push_back(VcAxiom{namedBool("axiom_b"), MapperRange{}, "B"});
    plan.goals.push_back(VcGoal{namedBool("goal_a"), MapperRange{}, "A"});
    plan.goals.push_back(VcGoal{namedBool("goal_b"), MapperRange{}, "B"});

    const SmtScript script = assemble(std::move(plan));
    // decl, [push, axiom_b, goal_a, check, pop], [push, axiom_a, goal_b, check, pop]
    ASSERT_EQ(script.commands.size(), 11u);
    EXPECT_EQ(script.commands[0].kind, SmtCommandKind::kDeclareFun);
    EXPECT_EQ(script.commands[1].kind, SmtCommandKind::kPush);
    ASSERT_TRUE(script.commands[2].assert_term != nullptr);
    EXPECT_EQ(script.commands[2].assert_term->var_name, "axiom_b"); // аксиома ДРУГОЙ функции
    EXPECT_FALSE(script.commands[2].obligation.has_value()); // аксиома — не цель
    ASSERT_TRUE(script.commands[3].assert_term != nullptr);
    EXPECT_EQ(script.commands[3].assert_term->var_name, "goal_a");
    EXPECT_TRUE(script.commands[3].obligation.has_value()); // цель
    EXPECT_EQ(script.commands[4].kind, SmtCommandKind::kCheckSat);
    EXPECT_EQ(script.commands[5].kind, SmtCommandKind::kPop);
    ASSERT_TRUE(script.commands[7].assert_term != nullptr);
    EXPECT_EQ(script.commands[7].assert_term->var_name, "axiom_a");
    EXPECT_EQ(script.logic, "QF_UFBV");
}

TEST(SmtAssembler, TypeAssertionGoalReceivesAllAxioms) {
    VcPlan plan;
    plan.axioms.push_back(VcAxiom{namedBool("axiom_a"), MapperRange{}, "A"});
    plan.goals.push_back(VcGoal{namedBool("type_goal"), MapperRange{}, ""});

    const SmtScript script = assemble(std::move(plan));
    ASSERT_EQ(script.commands.size(), 5u);
    EXPECT_EQ(script.commands[0].kind, SmtCommandKind::kPush);
    ASSERT_TRUE(script.commands[1].assert_term != nullptr);
    EXPECT_EQ(script.commands[1].assert_term->var_name, "axiom_a");
    EXPECT_EQ(script.commands[2].kind, SmtCommandKind::kAssert);
    EXPECT_TRUE(script.commands[2].obligation.has_value()); // цель
}

} // namespace trust::solver
