#pragma once

// Нейтральные идентификаторы (cli-имена) поведенческих флагов, которые применяет драйвер.
// Владельцы флагов (semantic, transpiler, verify) объявляют СВОИ списки
// (TRUST_FLAG_SET) и берут cli-имя из этого заголовка: строка определяется ОДИН раз здесь,
// а метаданные (значение по умолчанию, группа, help) и регистрация остаются у владельца.
// Благодаря этому `driver` применяет флаги по имени, не включая заголовки владельцев
// (иначе появляется ребро driver->semantic / driver->transpiler).

#include <string_view>

namespace trust {
namespace diag {

namespace flag_id {

inline constexpr std::string_view kSolverMode = "solver-mode";
inline constexpr std::string_view kSolverLoopUnroll = "solver-loop-unroll";
inline constexpr std::string_view kKeywords = "keywords";
inline constexpr std::string_view kStackCheck = "stack-check";
inline constexpr std::string_view kStackCheckReserve = "stack-check-reserve";
inline constexpr std::string_view kStackCheckFunctions = "stack-check-functions";
inline constexpr std::string_view kComments = "comments";
inline constexpr std::string_view kOverflowCheck = "overflow-check";
inline constexpr std::string_view kVerify = "verify";

} // namespace flag_id

} // namespace diag
} // namespace trust
