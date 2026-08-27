#include "solver/solver.hpp"

#ifdef TRUST_HAS_Z3
#include "solver/solver_z3.hpp"
#else
#include "solver/solver_stub.hpp"
#endif

#include <memory>

namespace trust {
namespace solver {

std::unique_ptr<SolverInterface> createSolver() {
#ifdef TRUST_HAS_Z3
    return std::make_unique<SolverZ3>();
#else
    return std::make_unique<SolverStub>();
#endif
}

SolverResult runScript(SolverInterface& solver, const SmtScript& script, std::vector<std::pair<std::string, std::string>>* out_model) {
    if (!script.logic.empty()) {
        solver.setLogic(script.logic);
    }
    SolverResult last = SolverResult::kError;
    bool sawCheck = false;
    // 0-арные объявленные константы (параметры func_param, массивы __arrN) - для отчёта о
    // контрпримере (их значения из модели на SAT). Собираются из declare-fun по ходу скрипта.
    std::vector<std::pair<std::string, SmtSort>> paramConsts;
    for (const auto& cmd : script.commands) {
        switch (cmd.kind) {
        case SmtCommandKind::kDeclareFun:
            if (cmd.fun_result_sort) {
                solver.declareFun(cmd.fun_name, cmd.fun_arg_sorts, *cmd.fun_result_sort);
                if (cmd.fun_arg_sorts.empty()) {
                    paramConsts.emplace_back(cmd.fun_name, *cmd.fun_result_sort);
                }
            }
            break;
        case SmtCommandKind::kAssert:
            if (cmd.assert_term) {
                solver.assertFormula(*cmd.assert_term);
            }
            break;
        case SmtCommandKind::kPush:
            solver.push(cmd.stack_depth > 0 ? cmd.stack_depth : 1);
            break;
        case SmtCommandKind::kPop:
            solver.pop(cmd.stack_depth > 0 ? cmd.stack_depth : 1);
            break;
        case SmtCommandKind::kCheckSat:
            sawCheck = true;
            switch (solver.checkSat()) {
            case SolverResult::kSat:
                // Найден контрпример (нарушение какого-то контракта): собираем значения констант
                // из модели для отчёта, затем возвращаем SAT.
                if (out_model) {
                    for (const auto& [nm, srt] : paramConsts) {
                        if (auto v = solver.getModelValue(nm, srt)) {
                            out_model->emplace_back(nm, std::move(*v));
                        }
                    }
                }
                return SolverResult::kSat;
            case SolverResult::kError:
                return SolverResult::kError;
            case SolverResult::kUnknown:
                return SolverResult::kUnknown;
            case SolverResult::kUnsupported:
                // Бэкенд недоступен (stub при WITH_SOLVER=OFF): результат всей проверки не может
                // быть определён - НЕ считать unsat (иначе ложноположительный «все контракты
                // выполняются»). Возвращаем kUnsupported сразу.
                return SolverResult::kUnsupported;
            case SolverResult::kUnsat:
            default:
                break; // unsat этой VC - продолжаем проверять остальные
            }
            break;
        default:
            break; // set-logic обработан выше, get-model и прочее игнорируем
        }
    }
    return sawCheck ? SolverResult::kUnsat : last;
}

} // namespace solver
} // namespace trust
