# MEMORY.md

> scope: include/ast/convert
> role: persistent-memory
> last_reviewed: 2026-11-10
> review_period: 30

## Architecture

`TermToAstConverter` (`term_to_ast.*`, `term_to_ast_decl.*`, `term_to_ast_expr.*`) — loader-free
конвертация Term→AST; внутренние детали — `term_to_ast_internal.hpp`, обход — `term_visitor.hpp`.
`jump_targets.hpp` — `JumpTargetsT<Target>`: единое правило целей break/continue для HIR-lowering и
CFG (реэкспорт через публичный `ast/clone.hpp`).

## Facts and invariants

- **`CheckAreaStmt`/`DebugStmt` из маркеров** строятся `term_to_ast` как ЛИСТ (не generic dispatch, не
  ContextMacro); атрибуты-требования CheckArea добавляются как manual=false (их обрабатывает семантика);
  Debug-узел семантика УДАЛЯЕТ, транспилятор на нём — `FAULT`.
- **MODULE-конвертация — только рекурсивная конвертация данных:** загрузку и конверсию тела выполняет
  `ModuleLoader`, конвертер loader-free, индекс модуля берётся из `Term::m_moduleIndex`.
- **⚠ Trust-контракты:** `collectChildren()` их пуст — обходят явно (`m_expr`); маркеры с ВЕДУЩИМ `@`
  (`}@` конфликтует с `@@`). ⚠ trust-контракт-термы после имени кладутся в `m_sequence` терма-имени →
  `hasConvertibleChildren` обязан их ПРОПУСКАТЬ, иначе имя с контрактами (`x @{...@} := ...`) станет
  `CallExpr`.
- **⚠ `visit_ASSIGN` → `TYPE_DEFINE` в ДВУХ случаях:** RHS — `CLASS` (реализация типа) и LHS — терм ТИПА
  (`:A ::- T;` — ВТОРОЕ имя существующего типа; вызов `:Name(...)` исключён по `m_args`). Остальные `=` —
  присваивание/деструктуризация.

## Relations

- `syntax` (Term), `ast/nodes` (классы), `module_loader` (модульные тела), `semantic`.
