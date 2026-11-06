# MEMORY.md

> scope: include/semantic/verify
> role: persistent-memory
> last_reviewed: 2026-11-11
> review_period: 30

## Architecture

Пост-проверки после ядра: `verifyTypedAst` (полнота фактов, ICE-политика), `lint` (неиспользуемые
переменные), `format_check` (`@[format("printf", …)]`), `trust_analyzer` (обработка trust-контрактов).
`LintHook` включается `FlagKind::Lint`; `format_check` и `trust_analyzer` вызываются из обхода.

## Facts and invariants

- **⚠ `verifyTypedAst` — гейт полноты типизированной AST:** вызывается только на УСПЕШНОЙ семантике;
  нарушение — ICE-политика `reportInternalError` (Fatal в CLI, Error в LSP), не Warning.
- **⚠ `verifyTypedAst` не видит тел импортированных модулей:** `ModuleNode` объявлен `AST_NO_SLOTS`,
  обход идёт по `children()`; инварианты формы импортируемых record проверяются в точке записи (R13).
- **⚠ Provider-gate — НЕ ICE и НЕ в `SemanticPassRunner`:** `verifySemanticProviders` (semantic/verify/
  provider_check) вызывается Pipeline ПОСЛЕ успешной семантики и ДО HIR; ошибки — пользовательские `Error`
  (missing implementation / зависимая реализация), после них кодоген не запускается.
- **`@[format("printf", strIdx, firstToCheck)]`** — индексы 1-based (GCC); `%s` ожидает `const char*`,
  StrChar требует `.c_str()`.
- **⚠ trust-контракты:** `collectChildren()` их не отдают — ядро обрабатывает явно; severity `-Wsolver`
  действует только когда `--solver-mode` НЕ задан. В Pre имя самой функции — ошибка; в Post имя функции
  = возврат. **Защита от авто-вывода:** тип с trust-условиями не выводится автоматически — ошибка с
  требованием явной аннотации.
- **Lint:** `-Wlint` / `-Wlint=aggressive` (значение — строковая политика флага, не severity-набор).

## Relations

- `semantic/core` (обход), `semantic/typer` (факты типов), `diag` (опции/severity), `analysis`
  (`RuntimeCheckKind` — не здесь).
