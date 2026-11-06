# MEMORY.md

> scope: include/semantic/ref
> role: persistent-memory
> last_reviewed: 2026-11-13
> review_period: 30

## Architecture

Ссылочные виды и потоковые проверки: `ref_kind`/`ref_qualifiers` (маркеры вида, `QualType`), `nativeref`
(`@[borrowed]`), `ref_cycle` (рекурсивные ссылки), `borrow_flow` (`BorrowFlowPass`), `definite_assignment`
(DA/became-const), `stack_check_infer`. Потоковые проходы идут по CFG/MIR после ядра семантики. Модель
инвалидации — `ref/BORROW_CHECKER.md`.

## Facts and invariants

- **⚠ Definite-assignment / became-const:** CFG по MIR + forward must-dataflow (`ref/definite_assignment`),
  места — `flow::PlaceId`, ссылки несут `IdentName::resolvedDecl`. ⚠ self-reference в инициализаторе при
  shadowing резолвится на НОВОЕ объявление, и DA считает VarDecl инициализированным ДО чтения инициализатора —
  это поддерживает легальный shadowing-паттерн. `m :=: _` — move-discard, не сброс.
- **⚠ Анализ инвалидации представлений — ВСЕГДА подключён** (`BorrowFlowPass`; настраиваются только уровни
  `-Wborrow-*`): CFG/dataflow пост-проход ПОСЛЕ DA, по MIR; место — узел объявления; may-живость per-path,
  освобождение на всех путях выхода, last-use для weak, escape, мутация источника не-const методом,
  согласованность `@[lifetime]`. ⚠ **модель — НЕ заём Rust, а ИНВАЛИДАЦИЯ производного представления при
  мутации/перемещении источника:** нет эксклюзивных займов, aliasing-XOR-mutability и loan-liveness; `unique`
  наблюдателя не имеет, мутация `shared` при живом weak разрешена.
- **⚠ `@[borrowed]` — запрет мутации источника при живых зависимых:** атрибут на классе/методе/переменной
  делает переменную зависимой; БЕЗ атрибута ничего не отслеживается. Атрибут КОПИРУЕТСЯ при
  создании/присваивании. Диагностика — В ТОЧКЕ МУТАЦИИ. ⚠ имена — «bare» (без `$`). Это ТОТ ЖЕ механизм, что
  `-Wborrow-*`.
- **Нативная ось ссылок — только явный `@[reftype("ptr"|"ref")]`:** маркеров вида в нативной позиции нет;
  `*` — разыменование, `&` — address-of.
- **⚠ маркеры ссылки — НЕ «сиглы», позиция определяет смысл:** `&&`=unique, `&*`=shared, `&?`=weak. Вид
  задаётся РОВНО В ОДНОЙ позиции: у ТИПА или у ПЕРЕМЕННОЙ только при ОПУЩЕННОМ типе. Один вид в ОБЕИХ позициях →
  `-Wref-kind-dup`; разные → mismatch; вид у переменной при ЯВНОМ типе → ошибка. Вид у типа учитывать ОБОИМИ
  способами (сигл + attr) — иначе рассинхрон.
- **⚠ unique — move-only:** копирование — ошибка; перенос — только `a :=: b` / `a :=: _`; swap исключён.
- **⚠ Рекурсивные ссылки в полях:** имя record-типа видно в собственном теле (`declareRecord` ДО анализа
  полей); forward доопределяется позже. `RefCycleHook` строит граф полей/баз и делает ТРИ анализа по
  ПОДМНОЖЕСТВАМ рёбер: shared в SCC полного графа → `-Wrecursive-shared`; только inline-рёбра →
  `-Wrecursive-value`; SCC≥2 только unique → `-Wrecursive-unique`. `weak/ptr/ref/locker` невладеющие; `:Array`
  heap-inline; `:Range` — view.
- **⚠ Стек:** `@__OPTION__("stack-check","off")` сбрасывает enabled и значение (режим обязан быть kOff при
  отключённом флаге). `StackCheckInferHook` (recursion/auto): граф вызовов; auto → `add_attr` на рекурсивный
  FuncDecl. Диагностики выдаются в порядке диапазонов объявлений (begin→end), а не в порядке pointer-keyed
  `m_calls` — иначе порядок зависел бы от адресов (R42).
- **`swap :=:` — интринсик:** типы СОВМЕСТИМЫ (не обязательно ссылки); `var :=: _` → std::move; нужна явная
  ветка до общей типизации.

## Relations

- `semantic/core` (обход), `semantic/typer` (типы операндов), `ir` (MIR/CFG), `types` (`RefTypeData`),
  `transpiler` (`ref_emit` читает материализованные факты).
