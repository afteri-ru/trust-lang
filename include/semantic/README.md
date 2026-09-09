# Semantic - Семантический анализатор

## Назначение

Выполняет семантический анализ AST через **единое однопроходное ядро разрешения имён**
(`NameResolutionPass`) и **параллельные анализаторы** (`InlineAnalysisHook`), управляемые
feature-флагами `diag::Options`. Phase 1 охватывает объявления переменных с инициализацией,
литералы в выражениях, валидацию дубликатов имён и неопределённых типов.

## Особенности реализации

- **Единое ядро** - `NameResolutionPass` (`semantic/name_resolution.hpp`) за один обход AST
  строит единую таблицу символов `SymbolTable` (стек вложенных скоупов), регистрирует объявления
  в текущем скоупе и разрешает `Ident` поиском вверх. Семантика однопроходная: имя объявляется до
  использования. Объединяет бывшие `SymbolCollectorPass`+`NameResolverPass`, а также бывшие
  раздельные `ScopeStack` и плоскую `SymbolTable`. Ядро - **драйвер**; тяжёлая анализ-логика
  вынесена в компоненты-анализаторы `DeclAnalyzer`/`ExprTyper`/`AccessResolver`/`TrustAnalyzer`
  (`semantic/*_analyzer.hpp/.cpp`), разделяющие `AnalysisContext`; общие хелперы -
  в `semantic/analysis_common.hpp`.
- **Параллельные анализаторы** - `InlineAnalysisHook` (`semantic/inline_hook.hpp`):
  опциональные анализаторы подключаются к ядру и получают события в реальном времени обхода
  (`enterScope`/`exitScope`/`onDeclare`/`onResolve`/`onNode`/`finalize`), читая временные данные
  ядра (`SymbolTable`) через `AnalysisContext` - без повторного построения иерархии.
  Флаг включения проверяется **один раз** при подключении в `SemanticPassRunner`; отключённый
  хук в список активных не попадает, его колбэки в узлах не вызываются.
- **Анализатор Lint** - `LintHook` (`semantic/lint.hpp`), gate = `FlagKind::Lint`; сообщает о
  неиспользуемых переменных; режим задаётся строковым значением флага `-Wlint=aggressive`.
- **Анализ нативных ссылок + отслеживание инвалидации** - `NativeRefHook` (`semantic/nativeref.hpp`),
  атрибут
  `@[borrowed@]` на класс/тип, метод или переменную (авто-трекинга нет: значение-копии, в т.ч. `obj[i]`, и
  умные ссылки shared/weak/unique borrowed НЕ отслеживает — для умных ссылок мутация под займом
  это зона borrow-checker `-Wborrow-*`). Атрибут копируется при создании/присваивании. Любая
  мутация источника (присваивание ИЛИ вызов не-const метода) инвалидирует зависимые, рождённые до
  неё; использование зависимой после мутации - диагностика severity `-Wborrowed=ignore|warning|error`.
- **Статический borrow-checker** - `BorrowCheckHook` (`semantic/borrow_check.hpp`), **ВСЕГДА подключён**
  (гарантия языка/модели памяти; флага включения/выключения нет, настраиваются только уровни
  диагностик `-Wborrow-<name>=<sev>`):
  per-frame регионы умных ссылок (`unique`/`shared`/`weak`) из места хранения (`Storage`) и правила
  заимствования R1/R4/R5/R6; диагностики `borrow-region-mismatch`/`borrow-owner-moved`/
  `borrow-owner-mutated`/`borrow-static-unproven`. Общий механизм per-frame эпохи мутаций -
  `semantic/frame_epoch.hpp` (`FrameEpoch`), используется и `NativeRefHook`; R6 — ОСНОВНАЯ диагностика
  для умных ссылок (borrowed их не отслеживает).
- **SymbolTable (единая таблица символов)** - стек вложенных скоупов (`analysis/symbol_table.hpp`):
  каждый уровень - `Scope` с `std::map` имён и невладеющим `creator` (узел AST, открывший скоуп);
  глобальный скоуп (уровень 0) - плоская таблица глобальных имён. `push`/`pop`/`declare`/`resolve`/
  `lookup`; `pop` не удаляет глобальный скоуп. Владеется `AnalysisContext` и доступна через
  `runner.analysis().symbols()`.
- **AnalysisContext** - общий контекст семантики: владеет единой таблицей символов `SymbolTable`
  (стек вложенных скоупов); даёт доступ к `Context` (диагностики, типы, опции).
- **Интеграция таблицы типов** - ядро связано с `TypeRegistry` через общий сервис
  `AnalysisContext::resolveType` (скоуп → реестр); алиасы типов (`::=`) связываются в скоупе как
  `Symbol` (decl = узел `TypeDecl`); `FuncDecl` строит функциональную сигнатуру
  (`AnalysisContext::buildFuncType` → `getOrCreateFunctionType`) в `Symbol.type`;
  `SemanticPassRunner::run()` сбрасывает реестр к builtin-состоянию на каждый запуск
  (`TypeRegistry::reset()`); транспилятор при наличии разрешённой `SymbolTable` использует тот же
  TypeId (`CppTranspiler(ctx, &runner.analysis().symbols())`).
- **Валидация** - проверка дубликатов имён (в скоупе), неопределённых имён и типов,
  отсутствующих инициализаторов.
- **Lowering** - после успешной семантики `SemanticPassRunner::run()` вставляет синтетические
  узлы, чтобы транспилятор был только кодогенератором: `SemicolonStmt` (точка с запятой для
  statement-выражений), `GotoStmt`/`LabelStmt` (метки goto именованных break/continue и
  именованных блоков). Имя текущей функции и стек имён живут в контексте lowering `LowerCtx`
  (в транспиляторе удалены).
- **Общие продукты анализа — в `include/analysis`** - `SymbolTable`/`Symbol`/`SymbolIndex` вынесены из
  semantic (их использует и кодоген); здесь остаются драйвер обхода, анализаторы и `AnalysisContext`.
  Поведенческие режимы (solver/stack-check) принадлежат semantic: value-типы — в `analysis/modes.hpp`,
  разрешение значений — `semantic::behavioralModesFromOptions` (передаётся в кодоген как данные).
- **Pipeline position** - расположен между Parser и CppTranspiler; при ошибках семантики
  генерация C++ не запускается.