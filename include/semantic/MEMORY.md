# MEMORY.md

> scope: include/semantic (public API)
> role: persistent-memory
> last_reviewed: 2026-11-06
> review_period: 30
> max_size: 8000

## Architecture

Семантика **однопроходная**: имя обязано быть объявлено до использования. Ядро — `NameResolutionPass`
(драйвер: обход и скоуп-стек, события), анализаторы — через `InlineAnalysisHook`; `SemanticPassRunner`
держит только упорядоченный реестр хуков и НЕ понижает AST и НЕ строит HIR. После прогона AST
неизменяема (кроме ErrorExpr-recovery), факты материализованы в ПОЛЯХ узлов, полноту проверяет
`verifyTypedAst`. Фазы производного представления (buildHir → verifyHir → buildMir → DA/borrow)
принадлежат `Pipeline`.

Публичные заголовки (корень, манифест `trust_component`): `analysis_api.hpp` (umbrella: драйвер
проходов, поведенческие режимы, DA/borrow/stack-check), `semantic_model.hpp` (продукт фазы:
persistent-таблица символов + проекции), `diag.hpp` (диагностики семантики), `solver.hpp`
(режимы `--solver-mode`). Всё остальное, включая `pass_runner.hpp`,
`behavioral_modes.hpp`, `borrow_flow.hpp`, `definite_assignment.hpp`, `stack_check.hpp`,
живёт в internal-подкаталогах и снаружи компонента недоступно.

Детали реализации — в internal-подкаталогах:

| internal-подкаталог | MEMORY | ответственность |
|---|---|---|
| `semantic/core/` | `core/MEMORY.md` | обход/скоуп-стек, `AnalysisContext`, name resolution, импорт/видимость, макросы |
| `semantic/decl/` | `decl/MEMORY.md` | анализаторы объявлений, сигнатуры, полнота типов, record/class |
| `semantic/typer/` | `typer/MEMORY.md` | вывод типов, перегрузка/вызовы, операторы, `argCasts`, многоточие |
| `semantic/ref/` | `ref/MEMORY.md` | ссылочные виды, DA, borrow/инвалидация, рекурсивные ссылки, stack-check |
| `semantic/verify/` | `verify/MEMORY.md` | `verifyTypedAst`, lint, format-check, trust-анализ |

## Facts and invariants

- **⚠ ICE — политика `reportInternalError`:** CLI (`allow_semantic_on_errors=false`) → `Fatal` +
  backtrace и код 1; LSP (`true`) → `Error` и продолжение с частичным AST. Тихий `Error` в CLI
  ЗАПРЕЩЁН (маскирует баг компилятора).
- **lowering НЕ мутирует AST:** решения (statement-выражения → `SemicolonStmt`, именованные break/continue
  → goto/метки, break по имени функции → синтетический void-return) материализуются в HIR; синтетические
  узлы имеют невалидный range.
- **⚠ UNKNOWN-тип не должен доходить до кодогенерации:** при INVALID_TYPE_ID — явная диагностика + FAULT
  (тихий `auto` запрещён). Абстрактные контейнеры без структурной конкретики — явная диагностика кодогена.
- **⚠ транспилятору локальные символы недоступны:** семантика сохраняет на узлах тип результата `& x`,
  вид операнда `*ref` и lowering — иначе кодоген не выберет `*(c.lock())` vs `checked_deref(...)`.
  `*` — БЕЗ UB на nullptr: unique/ptr — `trust::checked_deref` (бросает `IntMinus`), shared/weak —
  `*(ref.lock())`.
- **trust-контракты:** обработка — `verify/MEMORY.md` (Pre/Post, `-Wsolver` vs `--solver-mode`,
  защита от авто-вывода), обход — `core/MEMORY.md` (`collectChildren()` их не отдают).

## Decisions

- **`AnalysisContext` — агрегат без логики:** новых методов с вычислениями в нём не появляется; зона
  (данные + правила) выделяется отдельным классом (`NamespaceScope`/`TypeResolution`/`ExprFacts`/`AnalysisState`).
- **Многопроходные оптимизации — отдельный MIR/CFG-этап, не semantic.** Семантика однопроходна;
  flow-анализы — на CFG/MIR. `QualType` — flow-sensitive type-state, но в AST он мержится вручную.

## Relations

- `ast` (узлы/lowering), `symbols` (`SymbolTable`/`SymbolIndex`)/`ir` (`MIR`), `types` (identity, перегрузка,
  реестр), `attrs` (аргументы атрибутов), `session` (`Context`), `pipeline` (владелец фаз),
  `module_loader` (экспорт-наборы).
