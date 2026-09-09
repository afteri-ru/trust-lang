# MEMORY.md

> scope: include/analysis
> role: persistent-memory
> last_reviewed: 2026-09-17
> review_period: 30
> max_size: 7000

## Architecture

Продукты анализа, общие для анализатора (`semantic`) и кодогенерации (`transpiler`):
- `symbol_table.hpp` — `Symbol`/`DeclResult`/`Storage`/`Scope`/`SymbolTable` (стек лексических скоупов);
- `symbol_index.hpp` — `SymbolInfo`/`SymbolIndex` (плоский индекс для LSP);
- `modes.hpp` — value-типы поведенческих режимов (`SolverMode`/`StackCheckMode`), имена нативных функций
  контроля стека и структура `BehavioralModes` (разрешённые значения, передаваемые в кодоген как данные).

Компонент НИЖЕ semantic/transpiler: опирается только на `ast`/`types`/`location`.

## Facts and invariants

- **`SymbolTable` владеет `Symbol`;** `lookup`/`resolve` возвращают НЕвладеющие указатели (валидны до
  `pop` скоупа/пересоздания таблицы).
- **`declareOrComplete` НЕ перемещает `sym`** (кладёт копию): хук `onDeclare(sym)` видит полноценный
  Symbol (имя/тип/decl), а не moved-from.
- **⚠ Перегрузка функций — на ПРИВЯЗКЕ ИМЕНИ, не отдельный тип:** `Symbol::overloads` (непусто) —
  набор сигнатур (интернированные FunctionTypeData TypeId); тогда `Symbol.type` — лишь
  ПРЕДСТАВИТЕЛЬ (первая сигнатура) и НЕ является «сигнатурой имени» — единственный корректный путь
  к выбору — резолвер (`semantic/overload_resolve.hpp`). `declareOrComplete` для двух функций с
  одним именем и РАЗНЫМИ сигнатурами → `DeclResult::Overloaded` (набор расширяется), точный дубль
  сигнатуры → `Duplicate`. Набор помечает ВСЕ объявления `FuncDecl::m_isOverloaded` (кодоген:
  уникальные C++-имена/экспорт-записи).
- **Скоуп-стек — часть драйвера обхода:** класс-скоуп открывается ТОЛЬКО через
  `NameResolutionPass::enterScope/exitScope` (прямой `symbols().push/pop` рассинхронизирует хуки).
- **`modes.hpp`: владелец ФЛАГОВ (`FlagKind`) — `semantic`;** этот компонент держит только value-типы,
  чистые parse/format-хелперы и `BehavioralModes`. Имена нативных функций контроля стека — единый
  источник (опечатка в распознавании/эмиссии невозможна).
- `BehavioralModes{ solver (nullopt=не задан), stackCheck=explicit, reserve, functions }`; чистые
  предикаты `solverAssertEnabled`/`stackCheckActive` принимают `BehavioralModes`, а не `Options`.

## Decisions

- Продукты анализа вынесены из `semantic`, чтобы `transpiler` не зависел от анализатора (P5): кодогену
  нужен `SymbolTable` только как источник типов/сигнатур, а не сам анализатор.
- Поведенческие режимы передаются в кодоген ДАННЫМИ (`BehavioralModes`), а не читаются из
  `diag::Options` в эмиттерах — снята последняя связь `transpiler → semantic`.

## Relations

- Зависит от `ast`, `types`, `location`.
- `semantic_lib` и `transpiler_lib` линкуют `analysis_lib`; `pipeline_lib`/`lsp_lib` — тоже (SymbolIndex).
