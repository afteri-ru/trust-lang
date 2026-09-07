#pragma once

// include/semantic/diag.hpp
// Диагностики и feature-флаги компонента semantic (единый источник данных для них).
// TRUST_DIAG_SET/TRUST_FLAG_SET генерируют trust::semantic::DiagId/FlagKind + ADL-доступы.

#include "diag/diag_set.hpp"

#define SEMANTIC_DIAG_LIST(M)                                                                                                                              \
    M(UnusedVariable, "unused-variable", Warning, "Unused variable", WG_Wall | WG_Wextra | WG_Wunused)                                                     \
    M(UnusedParameter, "unused-parameter", Warning, "Unused function parameter", WG_Wall | WG_Wextra | WG_Wunused)                                         \
    M(Embed, "embed", Warning, "#embed / embed directive", WG_Wall | WG_Wpedantic)                                                                         \
    M(NoSigil, "sigil", Warning, "Missing sigil (@, $, ...)", WG_Wpedantic)                                                                                \
    M(Format, "format", Error, "Format string / argument mismatch", WG_Wall | WG_Wformat)                                                                  \
    M(StackCheckInfer, "stack-check-infer", Warning, "Recursive function not protected by stack guard (with --stack-check=recursion|auto)", WG_None)       \
    M(WidenAny, "widen-any", Warning, "Implicit widening numeric conversion", WG_Wextra | WG_Wconversion)                                                  \
    M(Solver, "solver", Warning, "Trust condition(s) present (severity of the presence diagnostic; silence with -Wsolver=ignore)", WG_None)                \
    M(Shadow, "shadow", Warning, "Declaration shadows an outer name (variable/parameter)", WG_Wextra)                                                      \
    M(WithRefWithoutCapture, "with-ref-without-capture", Warning, "with binding copies a reference without '*'; use '*ref' to lock access", WG_Wextra)     \
    M(ClassMemberDot, "class-member-dot", Warning, "Class field/method should be registered with a leading '.'", WG_Wextra)                                \
    M(StaticMemberAsField, "static-member-as-field", Warning, "Accessing a static member of a class as an instance field", WG_Wextra)                      \
    M(RefTrace, "reftrace", Warning,                                                                                                                       \
      "Using a reference/iterator obtained from an object after the object was mutated (severity; silence with -Wreftrace=ignore)", WG_Wextra)             \
    M(NativeRef, "native-ref", Warning,                                                                                                                    \
      "Use of a native (raw) C++ reference operator %& / %* (and dereference '*') (severity; silence with -Wnative-ref=ignore)", WG_Wextra)                \
    M(UnhandledAttr, "unhandled-attr", Warning,                                                                                                            \
      "Attribute is not handled by the analyzer or the C++ code generator (severity; silence with -Wunhandled-attr=ignore)", WG_None)                      \
    M(CheckArea, "check-area", Warning, "Macro used in a forbidden syntactic area (violates @__CHECK_AREA__; severity; silence with -Wcheck-area=ignore)", \
      WG_None)

TRUST_DIAG_SET(trust::semantic, DiagId, SEMANTIC_DIAG_LIST)

// SEMANTIC_DIAG_LIST намеренно НЕ #undef'ится: пер-компонентный регистратор диагностик
// (name_resolution.cpp) переиспользует его для авто-регистрации всех severity-опций.
// Каждый член списка → `opts.add(trust::semantic::DiagId::X);` (без ручного списка opts.add(...)).
#define SEMANTIC_DG_ADD_OPT(NAME, cli, sev, help, grp) opts.add(trust::semantic::DiagId::NAME);

// Формат строки SEMANTIC_FLAG_LIST: M(EnumName, "cli", "default_value", DiagGroup, "help").
// Значение по умолчанию value-флага идёт СРАЗУ ПОСЛЕ имени опции, описание - последним аргументом
// ("" = нет значения по умолчанию); валидаторы значений - функции, регистрируются отдельно
// в регистраторе (set_flag_validator).
#define SEMANTIC_FLAG_LIST(M)                                                                                                                           \
    M(Lint, "lint", "", DiagGroup::Analysis, "Lint analyzer (unused variables/parameters); =aggressive for errors")                                     \
    M(Effect, "effect", "", DiagGroup::Analysis, "Effect/effects analyzer")                                                                             \
    M(Trust, "trust", "", DiagGroup::Analysis, "Trust checker (memory safety)")                                                                         \
    M(Extended, "extended", "", DiagGroup::Analysis, "Extended analysis")                                                                               \
    M(Symbols, "symbols", "", DiagGroup::Analysis, "Collect symbol index in the result")                                                                \
    M(SolverLoop, "solver-loop", "warning", DiagGroup::Analysis, "Loop verification when no invariant/unroll: ignore|warning|error (default: warning)") \
    M(SolverLoopUnroll, "solver-loop-unroll", "", DiagGroup::Analysis, "Unroll loops without an invariant (behavioral; default: off)")                  \
    M(SolverMode, "solver-mode", "", DiagGroup::Analysis, "Solver behavior: assert|export|calculate (runtime checks / SMT export + run)")               \
    M(Keywords, "keywords", "", DiagGroup::Analysis,                                                                                                    \
      "Macro names that may be written without the '@' sigil (keyword-like); comma-separated, entries may keep a leading '@'")                          \
    M(StackCheck, "stack-check", "explicit", DiagGroup::Analysis, "Stack overflow protection mode: off|explicit|recursion|auto (default: explicit)")    \
    M(StackCheckReserve, "stack-check-reserve", "", DiagGroup::Analysis,                                                                                \
      "Minimum stack reserve (bytes) for exception handling, added to all stack checks; empty = runtime default (8192)")                                \
    M(StackCheckFunctions, "stack-check-functions", "", DiagGroup::Analysis,                                                                            \
      "Comma-separated function names limiting the automatic stack limit (m_stack_limit from .stack_sizes); empty = all functions")

TRUST_FLAG_SET(trust::semantic, FlagKind, SEMANTIC_FLAG_LIST)

// SEMANTIC_FLAG_LIST намеренно НЕ #undef'ится: регистратор диагностик переиспользует его для
// авто-регистрации ВСЕХ флагов. Каждый член списка → `opts.add_flag(FlagKind::X, defval)`; значение
// по умолчанию задаётся внутри add_flag (валидированная регистрация); валидаторы - функции, ручные.
#define SEMANTIC_FL_ADD_OPT(NAME, cli, defval, grp, help) opts.add_flag(trust::semantic::FlagKind::NAME, defval);
