#pragma once

// План проверки — ЧТО доказать и что допустить, без привязки к порядку печати SMT-команд.
// Контракт:
//   * `VcGoal` — изолированное условие проверки (нарушение ⇔ SAT); `srcRange` привязывает
//     результат к конструкции источника (для отчёта `file:line`).
//   * `VcAxiom` — глобальное допущение (аксиома контракта функции). Каждая цель получает
//     аксиомы ВСЕХ функций, КРОМЕ своей собственной (`owner`): иначе постусловие доказывалось бы
//     из собственной аксиомы (тривиальное «доказательство»).
//   * `declarations` — declare-sort/declare-fun, порядок печати не влияет на смысл.
// Скрипт из плана строит `SmtAssembler` в фиксированном порядке: декларации, затем цели
// (push → аксиомы чужих функций → assert цели → check-sat → pop).

#include "solver/smt_ast.hpp"

#include <cstddef>
#include <string>
#include <utility>
#include <vector>

namespace trust::solver {

/// Изолированное условие проверки. `owner` — функция, чья это VC (`""` — тип-утверждение).
struct VcGoal {
    SmtTerm condition;
    MapperRange srcRange;
    std::string owner;
    VcObligationKind kind = VcObligationKind::kPost;
};

/// Аксиома контракта функции (действует только для ЧУЖИХ целей).
struct VcAxiom {
    SmtTerm term;
    MapperRange srcRange;
    std::string owner;
};

/// План проверки модуля.
struct VcPlan {
    std::vector<SmtCommand> declarations; ///< declare-sort/declare-fun (без assert)
    std::vector<VcAxiom> axioms;          ///< глобальные допущения
    std::vector<VcGoal> goals;            ///< изолированные условия проверки
    /// Символьный маппинг SMT-имя → trust-источник (параметры/функции/глобалы).
    std::vector<std::pair<std::string, SmtSymbolRef>> symbolMap;
    /// Сколько целей НЕ построено: код вне области проверки (диагностика уже выдана).
    std::size_t outsideEnvelopeCount = 0;

    [[nodiscard]] bool empty() const { return declarations.empty() && axioms.empty() && goals.empty(); }
};

} // namespace trust::solver
