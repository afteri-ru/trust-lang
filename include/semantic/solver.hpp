#pragma once

// Режимы trust-конструкций разделены на два ортогональных механизма (конвенция GCC/Clang):
//   - `-Wsolver=ignore|warning|error` (DiagId::Solver) - severity-диагностика «присутствуют
//     trust-условия» (default warning; глушится -Wsolver=ignore). Применяется в
//     NameResolutionPass::processTrustConditions (presence-warning гасится, когда активен
//     --solver-mode).
//   - `--solver-mode=assert|export|calculate` (FlagKind::SolverMode) - ПОВЕДЕНЧЕСКИЙ флаг:
//     assert - рантайм-проверки (trust__abort__), export - генерация SMT-LIB 2 файла,
//     calculate - генерация + запуск Z3. Значение хранится в diag::Options как
//     feature-flag со строковым значением. По умолчанию НЕ задан (никакое поведение).
// Владелец ФЛАГА — semantic; VALUE-типы режимов и чистые хелперы живут в `analysis/modes.hpp`
// (разделяются анализом и кодогеном как данные).

#include "analysis/modes.hpp"
#include "diag/options.hpp"
#include "semantic/diag.hpp"

#include <optional>
#include <string_view>

namespace trust {
namespace semantic {

// Value-типы и хелперы поведенческих режимов — только из `analysis` (единый источник):
// здесь они используются с полной квалификацией, отдельных имён в `semantic` нет.

/// Поведенческий режим из diag::Options (значение флага SolverMode). Если флаг не имеет
/// значения (опция --solver-mode не задана) - nullopt (никакое поведение).
[[nodiscard]] inline std::optional<analysis::SolverMode> solverModeFromOptions(const Options& opts) noexcept {
    if (auto val = opts.flag_value(semantic::FlagKind::SolverMode)) {
        return analysis::parseSolverMode(*val);
    }
    return std::nullopt;
}

/// Включены ли рантайм-проверки trust-условий (`--solver-mode=assert`).
[[nodiscard]] inline bool solverAssertEnabled(const Options& opts) noexcept {
    return solverModeFromOptions(opts) == analysis::SolverMode::kAssert;
}

// Value-типы и имена — из `analysis/modes.hpp` (единый источник, разделяемый с верификатором,
// который получает режим ДАННЫМИ через `BehavioralModes`, а не из `diag::Options`).

/// Режим диагностики циклов без инварианта из diag::Options (значение флага SolverLoop).
/// По умолчанию - kWarning.
[[nodiscard]] inline analysis::SolverLoopMode solverLoopModeFromOptions(const Options& opts) noexcept {
    if (auto val = opts.flag_value(semantic::FlagKind::SolverLoop)) {
        if (auto m = analysis::parseSolverLoopMode(*val)) {
            return *m;
        }
    }
    return analysis::SolverLoopMode::kWarning;
}

/// Включён ли глобальный bounded-unrolling для циклов без инварианта (флаг SolverLoopUnroll).
/// Поведенческий флаг (не severity): включается `--solver-loop-unroll` / `@__OPTION_TRUE__`.
[[nodiscard]] inline bool solverLoopUnrollEnabled(const Options& opts) noexcept {
    return opts.is_enabled(semantic::FlagKind::SolverLoopUnroll);
}

} // namespace semantic
} // namespace trust
