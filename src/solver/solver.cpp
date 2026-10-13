#include "solver/solver.hpp"

#ifdef TRUST_HAS_Z3
#include "solver/impl/solver_z3.hpp"
#else
#include "solver/impl/solver_stub.hpp"
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

SolverResult runScript(SolverInterface& solver, const SmtScript& script, std::vector<std::pair<std::string, std::string>>* out_model,
                       VerificationReport* report) {
    if (!script.logic.empty()) {
        solver.setLogic(script.logic);
    }
    // 0-арные объявленные константы (параметры func_param, массивы __arrN) - для отчёта о
    // контрпримере (их значения из модели на SAT). Собираются из declare-fun по ходу скрипта.
    std::vector<std::pair<std::string, SmtSort>> paramConsts;
    VerificationReport local;
    VerificationReport& checks = report != nullptr ? *report : local;
    checks = VerificationReport{};
    MapperRange goalRange;                    ///< диапазон источника текущей (изолированной) цели
    std::optional<VcObligationKind> goalKind; ///< вид текущей цели (из плана через скрипт)
    bool sawCheck = false;
    bool sawSat = false;
    bool sawUnknown = false;
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
        case SmtCommandKind::kDefineFun:
            // define-fun ОБЯЗАН исполняться: иначе export и calculate расходятся (помощники
            // преамбулы молча теряли бы семантику).
            EXPECT(cmd.fun_body && cmd.fun_result_sort && "define-fun command must carry a body and a result sort");
            solver.defineFun(cmd.fun_name, cmd.fun_arg_sorts, *cmd.fun_result_sort, *cmd.fun_body);
            break;
        case SmtCommandKind::kAssert:
            if (cmd.assert_term) {
                solver.assertFormula(*cmd.assert_term);
            }
            if (cmd.obligation.has_value()) {
                goalRange = cmd.srcRange;
                goalKind = cmd.obligation;
            }
            break;
        case SmtCommandKind::kPush:
            solver.push(cmd.stack_depth > 0 ? cmd.stack_depth : 1);
            break;
        case SmtCommandKind::kPop:
            solver.pop(cmd.stack_depth > 0 ? cmd.stack_depth : 1);
            break;
        case SmtCommandKind::kCheckSat: {
            sawCheck = true;
            VcCheckResult result;
            result.srcRange = goalRange;
            result.kind = goalKind;
            switch (solver.checkSat()) {
            case SolverResult::kSat:
                // Нарушение: значения 0-арных констант из модели — пример нарушения.
                for (const auto& [nm, srt] : paramConsts) {
                    if (auto v = solver.getModelValue(nm, srt)) {
                        result.model.emplace_back(nm, std::move(*v));
                    }
                }
                if (out_model != nullptr && out_model->empty()) {
                    *out_model = result.model; // CLI печатает пример первого нарушения
                }
                result.result = SolverResult::kSat;
                checks.checks.push_back(std::move(result));
                sawSat = true;
                break; // продолжаем: отчёт содержит ВСЕ нарушения
            case SolverResult::kError:
                result.result = SolverResult::kError;
                checks.checks.push_back(std::move(result));
                checks.overall = SolverResult::kError;
                return SolverResult::kError;
            case SolverResult::kUnknown:
                result.result = SolverResult::kUnknown;
                checks.checks.push_back(std::move(result));
                sawUnknown = true;
                break;
            case SolverResult::kUnsupported:
                // Бэкенд недоступен (stub при WITH_SOLVER=OFF): результат всей проверки не может
                // быть определён - НЕ считать unsat (иначе ложноположительный «все контракты
                // выполняются»).
                result.result = SolverResult::kUnsupported;
                checks.checks.push_back(std::move(result));
                checks.overall = SolverResult::kUnsupported;
                return SolverResult::kUnsupported;
            case SolverResult::kUnsat:
                result.result = SolverResult::kUnsat;
                checks.checks.push_back(std::move(result));
                break;
            }
            break;
        }
        default:
            break; // set-logic обработан выше, get-model и прочее игнорируем
        }
    }
    if (!sawCheck) {
        checks.overall = SolverResult::kError; // условий проверки не было
        return SolverResult::kError;
    }
    checks.overall = sawSat ? SolverResult::kSat : (sawUnknown ? SolverResult::kUnknown : SolverResult::kUnsat);
    return checks.overall;
}

} // namespace solver
} // namespace trust
