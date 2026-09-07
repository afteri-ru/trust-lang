#pragma once

// include/semantic/stack_check.hpp
// Режим контроля переполнения стека (`--stack-check=<mode>`, FlagKind::StackCheck).
// Поведенческий value-флаг по аналогии с `--solver-mode`: монотонная шкала
//   off < explicit < recursion < auto
//   - off       - контроль выключен (атрибуты/макросы - no-op, рекурсия не анализируется);
//   - explicit  - проверки только явно размеченных функций (@[stack_check@]/@[stack_check(N)@])
//                 и явных вызовов %trust_stack_check / %trust_stack_check_set_limit;
//   - recursion - как explicit + диагностика (-Wstack-check-infer) незащищённых рекурсивных функций;
//   - auto      - как explicit + авто-маркировка рекурсивных функций stack_guard-маркером.
// Значение по умолчанию - explicit. Владелец флага - semantic (как solver-mode): его читают и
// семантика (анализатор рекурсии), и транспилятор (вставка проверок) через stackCheckModeFromOptions.
//
// Защитный запас (reserve) задаётся value-флагом FlagKind::StackCheckReserve (`--stack-check-reserve=<bytes>`);
// значение - целое число байт; по умолчанию (пусто) используется рантайм-дефолт STACK_RESERVE (8192).

#include "diag/options.hpp"
#include "semantic/diag.hpp"

#include <cstddef>
#include <optional>
#include <string>
#include <string_view>
#include <vector>

namespace trust {
namespace semantic {

// -- Единый источник значений поведенческого флага `--stack-check` (X-макрос) -------------
#define STACK_CHECK_MODE_LIST(M) \
    M(kOff, "off")               \
    M(kExplicit, "explicit")     \
    M(kRecursion, "recursion")   \
    M(kAuto, "auto")

#define STACK_CHECK_MODE_ENUM(name, cli) name,
enum class StackCheckMode { STACK_CHECK_MODE_LIST(STACK_CHECK_MODE_ENUM) };
#undef STACK_CHECK_MODE_ENUM

#define STACK_CHECK_MODE_NAME(name, cli) cli,
inline constexpr std::string_view kStackCheckModeNames[] = {STACK_CHECK_MODE_LIST(STACK_CHECK_MODE_NAME)};
#undef STACK_CHECK_MODE_NAME

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

#undef STACK_CHECK_MODE_LIST

/// Режим контроля стека из diag::Options (значение флага StackCheck). По умолчанию - kExplicit.
/// Если флаг отключён (`-Wno-stack-check` / `@__OPTION__("stack-check","off")`, значение сброшено) -
/// режим kOff (контроль выключен).
[[nodiscard]] inline StackCheckMode stackCheckModeFromOptions(const Options& opts) noexcept {
    if (opts.is_enabled(semantic::FlagKind::StackCheck)) {
        if (auto val = opts.flag_value(semantic::FlagKind::StackCheck)) {
            if (auto m = parseStackCheckMode(*val)) {
                return *m;
            }
        }
    }
    return StackCheckMode::kOff;
}

/// Контроль стека активен (режим != off).
[[nodiscard]] inline bool stackCheckActive(const Options& opts) noexcept {
    return stackCheckModeFromOptions(opts) != StackCheckMode::kOff;
}

/// Минимальный резерв стека (reserve) из diag::Options (значение флага StackCheckReserve, байты).
/// nullopt - не задан (использовать рантайм-дефолт STACK_RESERVE = 8192).
[[nodiscard]] inline std::optional<std::size_t> stackCheckReserveFromOptions(const Options& opts) noexcept {
    if (auto val = opts.flag_value(semantic::FlagKind::StackCheckReserve)) {
        std::size_t n = 0;
        const std::string_view s = *val;
        for (const char c : s) {
            if (c < '0' || c > '9') {
                return std::nullopt; // невалидное число - отсутствие значения (не тихий fallback,
                                     // а невыполнимость; валидатор должен был отсечь раньше)
            }
            n = n * 10 + static_cast<std::size_t>(c - '0');
        }
        return n;
    }
    return std::nullopt;
}

/// Список имён функций, ограничивающих m_stack_limit для limit-проверок (флаг StackCheckFunctions,
/// comma-separated). Пусто - используются все функции .stack_sizes.
[[nodiscard]] inline std::vector<std::string> stackCheckFunctionsFromOptions(const Options& opts) {
    std::vector<std::string> result;
    if (auto val = opts.flag_value(semantic::FlagKind::StackCheckFunctions)) {
        std::string cur;
        for (const char c : *val) {
            if (c == ',') {
                if (!cur.empty()) {
                    result.push_back(std::move(cur));
                }
                cur.clear();
            } else if (c != ' ' && c != '\t') {
                cur.push_back(c);
            }
        }
        if (!cur.empty()) {
            result.push_back(std::move(cur));
        }
    }
    return result;
}

} // namespace semantic
} // namespace trust
