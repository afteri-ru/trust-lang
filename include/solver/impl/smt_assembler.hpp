#pragma once

// include/solver/impl/smt_assembler.hpp
// Сборка SMT-LIB 2 скрипта из `VcPlan` в ФИКСИРОВАННОМ порядке:
//   1) декларации; 2) для каждой цели — push, аксиомы ДРУГИХ функций, assert цели,
//   check-sat, pop. Порядок не зависит от порядка обхода функций; собственная аксиома
//   функции в её цель не попадает (см. `vc_plan.hpp`).

#include "solver/smt_ast.hpp"
#include "solver/vc/vc_plan.hpp"

namespace trust::solver {

/// Собрать скрипт из плана (план ПОТРЕБЛЯЕТСЯ: символьная карта и счётчик envelope переходят
/// в скрипт без копирования). Логика выбирается по фактическому содержимому плана.
[[nodiscard]] SmtScript assemble(VcPlan&& plan);

} // namespace trust::solver
