#pragma once

// include/semantic/solver.hpp
// Режимы trust-конструкций разделены на два ортогональных механизма (конвенция GCC/Clang):
//   - `-Wsolver=ignore|warning|error` (DiagId::Solver) - severity-диагностика «присутствуют
//     trust-условия» (default warning; глушится -Wsolver=ignore). Применяется в
//     NameResolutionPass::processTrustConditions (presence-warning гасится, когда активен
//     --solver-mode).
//   - `--solver-mode=assert|export|calculate` (FlagKind::SolverMode) - ПОВЕДЕНЧЕСКИЙ флаг:
//     assert - рантайм-проверки (trust__abort__), export - генерация SMT-LIB 2 файла,
//     calculate - генерация + запуск Z3. Значение хранится в diag::Options как
//     feature-flag со строковым значением. По умолчанию НЕ задан (никакое поведение).

#include "diag/options.hpp"
#include "semantic/diag.hpp"

#include <optional>
#include <string_view>

namespace trust {
namespace semantic {

// -- Единый источник значений поведенческого флага `--solver-mode` (X-макрос) -------------
// Один список значений порождает enum SolverMode, имя режима и обратный разбор
// (по образцу X-макросов diag_set.hpp). Только поведенческие режимы: assert/export/calculate.
#define SOLVER_MODE_LIST(M) \
    M(kAssert, "assert")    \
    M(kExport, "export")    \
    M(kCalculate, "calculate")

#define SOLVER_MODE_ENUM(name, cli) name,
enum class SolverMode { SOLVER_MODE_LIST(SOLVER_MODE_ENUM) };
#undef SOLVER_MODE_ENUM

#define SOLVER_MODE_NAME(name, cli) cli,
inline constexpr std::string_view kSolverModeNames[] = {SOLVER_MODE_LIST(SOLVER_MODE_NAME)};
#undef SOLVER_MODE_NAME

inline constexpr std::size_t kSolverModeCount = sizeof(kSolverModeNames) / sizeof(kSolverModeNames[0]);

/// Имя режима (для диагностик/справки). Известное значение обязательно.
[[nodiscard]] inline std::string_view solverModeName(SolverMode m) noexcept {
    const int idx = static_cast<int>(m);
    return (idx >= 0 && idx < static_cast<int>(kSolverModeCount)) ? kSolverModeNames[idx] : "unknown";
}

/// Разбор строкового значения опции `--solver-mode`. Неизвестное значение - ошибка (вызывающий
/// обязан выдать диагностику, AGENTS п.5: без тихого fallback).
[[nodiscard]] inline std::optional<SolverMode> parseSolverMode(std::string_view v) noexcept {
    for (std::size_t i = 0; i < kSolverModeCount; ++i) {
        if (kSolverModeNames[i] == v) {
            return static_cast<SolverMode>(i);
        }
    }
    return std::nullopt;
}

#undef SOLVER_MODE_LIST

/// Поведенческий режим из diag::Options (значение флага SolverMode). Если флаг не имеет
/// значения (опция --solver-mode не задана) - nullopt (никакое поведение).
[[nodiscard]] inline std::optional<SolverMode> solverModeFromOptions(const Options& opts) noexcept {
    if (auto val = opts.flag_value(semantic::FlagKind::SolverMode)) {
        return parseSolverMode(*val);
    }
    return std::nullopt;
}

/// Включены ли рантайм-проверки trust-условий (`--solver-mode=assert`).
[[nodiscard]] inline bool solverAssertEnabled(const Options& opts) noexcept {
    return solverModeFromOptions(opts) == SolverMode::kAssert;
}

// -- Единый источник значений опции `-Wsolver-loop` (диагностика циклов без инварианта) ------
#define SOLVER_LOOP_MODE_LIST(M) \
    M(kIgnore, "ignore")         \
    M(kWarning, "warning")       \
    M(kError, "error")

#define SOLVER_LOOP_MODE_ENUM(name, cli) name,
enum class SolverLoopMode { SOLVER_LOOP_MODE_LIST(SOLVER_LOOP_MODE_ENUM) };
#undef SOLVER_LOOP_MODE_ENUM

#define SOLVER_LOOP_MODE_NAME(name, cli) cli,
inline constexpr std::string_view kSolverLoopModeNames[] = {SOLVER_LOOP_MODE_LIST(SOLVER_LOOP_MODE_NAME)};
#undef SOLVER_LOOP_MODE_NAME

inline constexpr std::size_t kSolverLoopModeCount = sizeof(kSolverLoopModeNames) / sizeof(kSolverLoopModeNames[0]);

/// Имя режима диагностики циклов (для диагностик/справки).
[[nodiscard]] inline std::string_view solverLoopModeName(SolverLoopMode m) noexcept {
    const int idx = static_cast<int>(m);
    return (idx >= 0 && idx < static_cast<int>(kSolverLoopModeCount)) ? kSolverLoopModeNames[idx] : "unknown";
}

/// Разбор строкового значения `-Wsolver-loop`. Неизвестное значение - nullopt (без тихого fallback).
[[nodiscard]] inline std::optional<SolverLoopMode> parseSolverLoopMode(std::string_view v) noexcept {
    for (std::size_t i = 0; i < kSolverLoopModeCount; ++i) {
        if (kSolverLoopModeNames[i] == v) {
            return static_cast<SolverLoopMode>(i);
        }
    }
    return std::nullopt;
}

#undef SOLVER_LOOP_MODE_LIST

/// Режим диагностики циклов без инварианта из diag::Options (значение флага SolverLoop).
/// По умолчанию - kWarning.
[[nodiscard]] inline SolverLoopMode solverLoopModeFromOptions(const Options& opts) noexcept {
    if (auto val = opts.flag_value(semantic::FlagKind::SolverLoop)) {
        if (auto m = parseSolverLoopMode(*val)) {
            return *m;
        }
    }
    return SolverLoopMode::kWarning;
}

/// Включён ли глобальный bounded-unrolling для циклов без инварианта (флаг SolverLoopUnroll).
/// Поведенческий флаг (не severity): включается `--solver-loop-unroll` / `@__OPTION_TRUE__`.
[[nodiscard]] inline bool solverLoopUnrollEnabled(const Options& opts) noexcept {
    return opts.is_enabled(semantic::FlagKind::SolverLoopUnroll);
}

} // namespace semantic
} // namespace trust