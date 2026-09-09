#pragma once

// include/analysis/modes.hpp
// Поведенческие режимы компилятора, разделяемые АНАЛИЗОМ и КОДОГЕНЕРАЦИЕЙ как ДАННЫЕ:
//   * режим trust-контрактов `--solver-mode` (assert/export/calculate);
//   * режим контроля переполнения стека `--stack-check` (off/explicit/recursion/auto);
//   * резерв и перечень функций контроля стека;
//   * имена нативных функций контроля стека (общие для распознавания семантикой и эмиссии кодогеном).
//
// Владелец ФЛАГОВ (`FlagKind`) — `semantic`: он разрешает значения из `diag::Options`
// (`semantic::behavioralModesFromOptions`) и передаёт их в кодоген структурой `BehavioralModes`.
// Кодоген НЕ обращается к `diag::Options`/`FlagKind` — это снимает зависимость transpiler → semantic.

#include <cstddef>
#include <optional>
#include <string>
#include <string_view>
#include <vector>

namespace trust {
namespace analysis {

// -- Единый источник значений поведенческого флага `--solver-mode` (X-macro) --
#define ANALYSIS_SOLVER_MODE_LIST(M) \
    M(kAssert, "assert")             \
    M(kExport, "export")             \
    M(kCalculate, "calculate")

#define ANALYSIS_SOLVER_MODE_ENUM(name, cli) name,
enum class SolverMode { ANALYSIS_SOLVER_MODE_LIST(ANALYSIS_SOLVER_MODE_ENUM) };
#undef ANALYSIS_SOLVER_MODE_ENUM

#define ANALYSIS_SOLVER_MODE_NAME(name, cli) cli,
inline constexpr std::string_view kSolverModeNames[] = {ANALYSIS_SOLVER_MODE_LIST(ANALYSIS_SOLVER_MODE_NAME)};
#undef ANALYSIS_SOLVER_MODE_NAME
#undef ANALYSIS_SOLVER_MODE_LIST

inline constexpr std::size_t kSolverModeCount = sizeof(kSolverModeNames) / sizeof(kSolverModeNames[0]);

/// Имя режима (для диагностик/справки). Известное значение обязательно.
[[nodiscard]] inline std::string_view solverModeName(SolverMode m) noexcept {
    const int idx = static_cast<int>(m);
    return (idx >= 0 && idx < static_cast<int>(kSolverModeCount)) ? kSolverModeNames[idx] : "unknown";
}

/// Разбор строкового значения опции `--solver-mode`. Неизвестное значение - nullopt (без тихого fallback).
[[nodiscard]] inline std::optional<SolverMode> parseSolverMode(std::string_view v) noexcept {
    for (std::size_t i = 0; i < kSolverModeCount; ++i) {
        if (kSolverModeNames[i] == v) {
            return static_cast<SolverMode>(i);
        }
    }
    return std::nullopt;
}

// -- Единый источник значений поведенческого флага `--stack-check` (X-macro) --
#define ANALYSIS_STACK_CHECK_MODE_LIST(M) \
    M(kOff, "off")                        \
    M(kExplicit, "explicit")              \
    M(kRecursion, "recursion")            \
    M(kAuto, "auto")

#define ANALYSIS_STACK_CHECK_MODE_ENUM(name, cli) name,
enum class StackCheckMode { ANALYSIS_STACK_CHECK_MODE_LIST(ANALYSIS_STACK_CHECK_MODE_ENUM) };
#undef ANALYSIS_STACK_CHECK_MODE_ENUM

#define ANALYSIS_STACK_CHECK_MODE_NAME(name, cli) cli,
inline constexpr std::string_view kStackCheckModeNames[] = {ANALYSIS_STACK_CHECK_MODE_LIST(ANALYSIS_STACK_CHECK_MODE_NAME)};
#undef ANALYSIS_STACK_CHECK_MODE_NAME
#undef ANALYSIS_STACK_CHECK_MODE_LIST

inline constexpr std::size_t kStackCheckModeCount = sizeof(kStackCheckModeNames) / sizeof(kStackCheckModeNames[0]);

/// Имя режима (для диагностик/справки). Известное значение обязательно.
[[nodiscard]] inline std::string_view stackCheckModeName(StackCheckMode m) noexcept {
    const int idx = static_cast<int>(m);
    return (idx >= 0 && idx < static_cast<int>(kStackCheckModeCount)) ? kStackCheckModeNames[idx] : "unknown";
}

/// Разбор строкового значения `--stack-check`. Неизвестное значение - nullopt (без тихого fallback).
[[nodiscard]] inline std::optional<StackCheckMode> parseStackCheckMode(std::string_view v) noexcept {
    for (std::size_t i = 0; i < kStackCheckModeCount; ++i) {
        if (kStackCheckModeNames[i] == v) {
            return static_cast<StackCheckMode>(i);
        }
    }
    return std::nullopt;
}

// -- Единый источник имён нативных функций контроля стека ---------------------
// Имена сопоставляются ДОСЛОВНО и семантикой (распознавание известного имени), и транспилятором
// (эмиссия вызова), поэтому хранятся в одном месте - опечатка в одном из двух мест невозможна.
namespace stack_check_fn {

inline constexpr std::string_view kCheck = "%trust_stack_check";
inline constexpr std::string_view kSetReserve = "%trust_stack_check_set_reserve";
inline constexpr std::string_view kGetReserve = "%trust_stack_check_get_reserve";
inline constexpr std::string_view kGetLimit = "%trust_stack_check_get_limit";
inline constexpr std::string_view kSetLimit = "%trust_stack_check_set_limit";

inline constexpr std::string_view kAll[] = {kCheck, kSetReserve, kGetReserve, kGetLimit, kSetLimit};

/// true, если имя - нативная функция контроля стека из рантайма.
[[nodiscard]] inline bool isStackCheckNativeFunc(std::string_view name) noexcept {
    for (std::string_view n : kAll) {
        if (name == n) {
            return true;
        }
    }
    return false;
}

} // namespace stack_check_fn

// -- Разрешённые поведенческие режимы (данные для кодогена) -------------------
/// Значения, разрешённые владельцем флагов (`semantic`) из `diag::Options` и переданные в кодоген.
struct BehavioralModes {
    std::optional<SolverMode> solver;                      ///< `--solver-mode`; nullopt = опция не задана.
    StackCheckMode stackCheck = StackCheckMode::kExplicit; ///< `--stack-check`; default explicit.
    std::optional<std::size_t> stackCheckReserve;          ///< `--stack-check-reserve` (байты).
    std::vector<std::string> stackCheckFunctions;          ///< `--stack-check-functions` (trust-имена).
};

/// Включены ли рантайм-проверки trust-условий (`--solver-mode=assert`).
[[nodiscard]] inline bool solverAssertEnabled(const BehavioralModes& m) noexcept {
    return m.solver.has_value() && *m.solver == SolverMode::kAssert;
}

/// Активен ли контроль переполнения стека (режим != off).
[[nodiscard]] inline bool stackCheckActive(const BehavioralModes& m) noexcept {
    return m.stackCheck != StackCheckMode::kOff;
}

} // namespace analysis
} // namespace trust
