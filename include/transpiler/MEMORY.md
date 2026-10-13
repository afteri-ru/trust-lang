# MEMORY.md

> scope: include/transpiler (public API)
> role: persistent-memory
> last_reviewed: 2026-11-04
> review_period: 30
> max_size: 8000

## Architecture

`CppTranspiler` — единственный публичный API (`transpiler/transpiler.hpp`, флаги — `transpiler/emit/diag.hpp`):
фасад-драйвер, exhaustive consumer (`ast/kind_dispatch.hpp`), обход и диспетчеризация (statement-only kind
в `emitExpr` → FAULT). Вход — HIR; данные анализа — только из полей узлов. Транспилятор — ТОЛЬКО
кодогенерация: всё, что требует анализа структуры (goto-метки, `;`, break/continue), зашито lowering'ом.
Детали реализации — в internal-подкаталогах:

| internal-подкаталог | MEMORY | ответственность |
|---|---|---|
| `transpiler/emit/` | `emit/MEMORY.md` | `CppEmitContext` и эмиттеры Decl/Stmt/Expr/Contract, декларации/операторы/выражения |
| `transpiler/type/` | `type/MEMORY.md` | `TypeEmitter`/`TypeNameResolver`/`TypeIncludeCollector`: C++-имя типа, include/link/POD, ABI `TypedValue` |
| `transpiler/module/` | `module/MEMORY.md` | модульные юниты: заголовок модуля, out-of-line, umbrella-фасад |

## Facts and invariants

- **⚠ Структурный вход кодогенерации — HIR** (клон AST + lowering): исходная AST не читается и не
  мутируется; `SemicolonStmt`, метки/goto и временные анализатора уже материализованы. Данные анализа
  кодоген читает ТОЛЬКО из полей узлов (`IdentName::resolvedTypeId`, `CallExpr::resolvedSignature`,
  `VarDecl::inferredType`); `SymbolTable` в бэкенд не передаётся. Финальная проверка `-Wunhandled-attr`
  идёт по HIR.
- **Term-изоляция:** транспилятор не обращается к Term — только `MapperRange`/текст узлов-носителей.
- **Наборы типов (`:A + :B`) кодоген не эмитит:** при использовании КАК ЗНАЧЕНИЯ — явная ошибка
  (без молчаливой пустой инициализации).

## Decisions

- **Временные переменные создаёт ТОЛЬКО анализатор (lowering)** — как реальные узлы AST с уникальным
  именем и выведенным типом; транспилятор НЕ синтезирует временные. Самостоятельное создание временной —
  грубое архитектурное нарушение. Исключения: синтез entry в однофайловом «скрипт»-режиме, интринсик
  `x :=: _` (move-to-discard) и постфиксный `x++`/`x--` (лямбда, возвращающая старое значение).
- **Кодоген НЕ зависит от анализатора (`transpiler → semantic` разорвана):** общие продукты — в
  `include/analysis`; поведенческие режимы передаются ДАННЫМИ, а не читаются из `diag::Options`.
  Владелец флагов (`FlagKind`) и разрешение значений — `semantic`.

## Relations

- `ast` (узлы/lowering), `semantic` (классификация, суффиксы), `types` (`TypeId`, шаблоны, ref-виды),
  `analysis` (`BehavioralModes`), `utils` (манглинг, ELF, source map), `package` (`@include`/`@link`),
  `pipeline` (вызывает `CppTranspiler`).
