# MEMORY.md

> scope: include/syntax/macro
> role: persistent-memory
> last_reviewed: 2026-11-04
> review_period: 30
> max_size: 7000

## Architecture

Макропроцессор: `Macro` — реестр скоупов, алгоритмика — свободные функции (`macro_split`,
`macro_expander`, `macro_matcher`, `macro_validator`, `macro_arg_parser`); прагмы/предопределённые —
`pragma_evaluator`, `predef_macro`. Списки — x-macro `predef_macro_x.hpp`/`pragma_macro_x.hpp`.

## Facts and invariants

- **⚠ Базовый скоуп DSL неизменяем из файла:** скоуп 0 — DSL (`--dsl`/stdlib), скоупы 1+ — файлы.
  `EvalOpMacros`/`RemoveMacro` ОТКЛОНЯЮТ переопределение/расширение арности/удаление группы из скоупа 0
  (`isDslScopeGroup`, `-Wdsl-macro-redefined`, default Error; действует базовое тело). На этой
  неизменяемости держится точность интерфейсной проверки (`isBaseScopeGroup`).
- **⚠ В интерфейсе файла допустимы макросы базового скоупа DSL и предопределённые;** модульные — ошибка
  (`forbidFileInterfaceMacro`), причём диагностика ОТЛОЖЕНА до успешного разбора блока.
- **⚠ контекст-макросы С аргументами (`@__CHECK_AREA__`, `@__DEBUG__`, `@__DEBUG_SCOPE__`)** разбираются
  в `GetNextToken` ДО прагм: вызов стирается, вставляется ОДИН терм `MACRO_CONTEXT`. Guard
  `buf[0]->m_id == MACRO_CONTEXT` ОБЯЗАТЕЛЕН — иначе маркер снова во главе буфера и обрабатывается
  повторно. При `TRUST_TRACE_ENABLED == 0` debug-вызов стирается БЕЗ маркера + `Severity::Warning`.
  Валидация опций дампа — ЗДЕСЬ (реестр `utils/trace/trace_options.hpp`) с полным списком при ошибке.
- **Атрибуты `@[...]`: макро-имена внутри НЕ раскрываются** (`m_in_attr`) — иначе бесконечная рекурсия
  на keyword-макросах; явный `@`-макрос внутри атрибута — ошибка. Предохранители
  `kMacroExpansionLimit`/`kMacroNestingLimit` (самовоспроизведение идёт через границы чтений парсера).

## Relations

- `syntax/parser` (`GetNextToken`), `syntax/term/term.h`, `session` (`Context::macro`), `utils/trace_options`.
