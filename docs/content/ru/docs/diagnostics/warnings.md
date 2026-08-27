---
title: Предупреждения и опции -W
tags: [diagnostics]
description: >
    Управление компиляторной диагностикой: severity-опции, группы, -Whelp
weight: 10
---

Severity-опции управляют диагностикой на этапе компиляции. Статус диагностики
задаётся одним из значений: `ignore`/`remark`/`note`/`warning`/`error`/`fatal`.

## Основные опции

- `-Whelp` - центральная справка по диагностикам (единый источник, сгруппированный
  по `DiagGroup` и агрегатам `WarnGroup`). Справка драйвера (`--help`) и справка по
  диагностикам (`-Whelp`) - две отдельные команды («двухсправочная модель»); обе
  генерируются централизованно из таблиц опций/диагностик. Диагностики
  регистрируются покомпонентно через единый API (`TRUST_DIAG_SET`/`TRUST_FLAG_SET`
  + `registerDiagnostics()`).
- `-Wembed=ignore` - отключить предупреждение о самом факте использования
  `{% ... %}` C++-блока вставки (независимо от имён внутри; по умолчанию `warning`).
- `-Wsigil=ignore` - отключить предупреждение о нормализации простой локальной
  переменной, объявленной без сигила `$`, в локальную `$x` (по умолчанию `warning`).
- `-Wformat=ignore|warning|error` - проверка на этапе компиляции типов аргументов
  против printf-строки формата для нативных функций, помеченных
  `@[format("printf", ...)]` (по умолчанию `error`).
- `-Wunused-variable` / `-Wunused-parameter` - отдельные диагностики неиспользуемых
  переменных и параметров (включаются через `-Wlint`, группы `Wall`/`Wextra`/`Wunused`).
- `-Werror` / `-Wno-error` - глобальный переключатель в стиле clang/gcc:
  `-Werror` повышает все предупреждения до ошибок, `-Wno-error` возвращает обратно.

## Группы диагностик

- **DiagGroup** - логические группы severity-диагностик:
  `Diagnostics`, `Analysis` (lint/effect/trust/extended/symbols), `Codegen`
  (comments/assert/backtrace).
- **WarnGroup** (агрегаты в стиле clang, X-macro `WARN_GROUPS`) - `Wall`,
  `Wextra`, `Wpedantic`, `Wunused`, `Wdeprecated`, `Wformat`, `Wconversion`.
  `-Wall`/`-Wextra`/`-W<group>` включают все диагностики группы, `-Wno-<group>`
  выключает. Группа обрабатывается только при отсутствии `=value` (т.к.
  «deprecated» - одновременно и группа, и диагностика).

Поддерживаются формы `-W<name>`, `-Wno-<name>`, `-W<name>=<severity>`, scoped
`push()/pop()`, а также агрегаты. `-Werror` повышает Warning→Error через `get()`.

## Ссылки

- [Trust-условия и верификация](trust-conditions/)
- [Компилятор (CLI)](../tooling/cli/)
