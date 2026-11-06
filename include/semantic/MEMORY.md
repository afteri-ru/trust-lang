# MEMORY.md

> scope: include/semantic (public API)
> role: persistent-memory
> last_reviewed: 2026-11-12
> review_period: 30

## Architecture

Семантика **однопроходная**: имя обязано быть объявлено до использования. Ядро — `NameResolutionPass`
(обход и скоуп-стек, события), анализаторы — через `InlineAnalysisHook`; `SemanticPassRunner` держит
упорядоченный реестр хуков, НЕ понижает AST и НЕ строит HIR. После прогона AST неизменяема (кроме
ErrorExpr-реcovery), факты — в ПОЛЯХ узлов, полноту проверяет `verifyTypedAst`. Производные представления
(buildHir→verifyHir→buildMir→DA/borrow) — у `Pipeline`. Единственность фактов проверяется в точке
записи: `verifyTypedAst` видит только корневой AST (`ModuleNode` — без слотов), поэтому инварианты
формы импортируемых модулей проверяет писатель (R13).

Публичные заголовки (корень): `analysis_api.hpp`, `semantic_model.hpp`, `diag.hpp`, `solver.hpp`.
Всё остальное (`pass_runner.hpp`, `borrow_flow.hpp`, `definite_assignment.hpp`, `stack_check.hpp` и др.) —
в internal-подкаталогах, снаружи недоступно.

| internal-подкаталог | MEMORY | ответственность |
|---|---|---|
| `semantic/core/` | есть | обход/скоуп-стек, `AnalysisContext`, resolution, импорт/видимость, макросы |
| `semantic/decl/` | есть | анализаторы объявлений, сигнатуры, полнота типов, record/class |
| `semantic/typer/` | есть | вывод типов, перегрузка/вызовы, операторы, `argCasts`, многоточие |
| `semantic/ref/` | есть | ссылочные виды, DA, borrow/инвалидация, рекурсивные ссылки, stack-check |
| `semantic/verify/` | есть | `verifyTypedAst`, lint, format-check, trust-анализ |

## Facts and invariants

- **⚠ Снимок реестра в `SemanticModel` действителен только до `TypeSystem::reset()`:** модель запоминает
  `generation()` при построении, `typeSystem()` сверяет и даёт `EXPECT` при расхождении (R7). Продление
  жизни модели через повторный прогон того же `Pipeline` недопустимо — `reset()` единственная точка инвалидации.

- **⚠ C++-доступ к члену класса проверяется ЕДИНОЙ точкой `checkMemberAccess`** (`typer/member_access.hpp`):
  поля — в `AccessResolver` (через `TypeStore::findFieldAccess`), методы — в `MethodCallTyper`;
  уровень — из реестра. «Текущий класс» — `NamespaceScope::currentTypeBodyId()` (TypeId, материализованный
  семантикой на узле-объявлении `RecordDecl`/`ClassDecl`). `protected` дополнительно требует, чтобы объект
  доступа был типа наследника (C++-правило), иначе диагностика, а не отложенная ошибка C++.

- **⚠ ICE — политика `reportInternalError`:** CLI (`allow_semantic_on_errors=false`) → `Fatal` + backtrace,
  код 1; LSP (`true`) → `Error` и продолжение с частичным AST. Тихий `Error` в CLI ЗАПРЕЩЁН (маскирует баг).
- **lowering НЕ мутирует AST:** решения (statement-выражения → `SemicolonStmt`, именованные break/continue →
  goto, break по имени функции → синтетический void-return) материализуются в HIR; синтетические узлы имеют
  невалидный range.
- **⚠ UNKNOWN-тип не должен доходить до кодогенерации:** при INVALID_TYPE_ID — диагностика + FAULT (тихий
  `auto` запрещён). Абстрактные контейнеры без конкретики — явная диагностика кодогена.
- **⚠ транспилятору локальные символы недоступны:** семантика сохраняет на узлах тип результата `& x`, вид
  операнда `*ref` и lowering — иначе кодоген не выберет `*(c.lock())` vs `checked_deref(...)`. `*` — БЕЗ UB на
  nullptr: unique/ptr — `trust::checked_deref` (бросает `IntMinus`), shared/weak — `*(ref.lock())`.
- **trust-контракты:** обработка — `verify/MEMORY.md` (Pre/Post, `-Wsolver` vs `--solver-mode`, защита от
  авто-вывода), обход — `core/MEMORY.md`.

## Decisions

- **`AnalysisContext` — агрегат без логики:** зона (данные + правила) — отдельный класс
  (`NamespaceScope`/`TypeResolution`/`ExprFacts`/`AnalysisState`).
- **Многопроходные оптимизации — отдельный MIR/CFG-этап, не semantic.** `QualType` — flow-sensitive type-state,
  но в AST мержится вручную.

## Relations

- `ast`, `symbols`/`ir`, `types`, `attrs`, `session`, `pipeline`, `module_loader`.
