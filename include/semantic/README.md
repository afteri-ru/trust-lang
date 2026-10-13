# Semantic - Семантический анализатор

## Назначение

Выполняет семантический анализ AST через **единое однопроходное ядро разрешения имён**
(`NameResolutionPass`) и **параллельные анализаторы** (`InlineAnalysisHook`), управляемые
feature-флагами `diag::Options`. Phase 1 охватывает объявления переменных с инициализацией,
литералы в выражениях, валидацию дубликатов имён и неопределённых типов.

## Особенности реализации

- **Единое ядро** - `NameResolutionPass` (`semantic/core/name_resolution.hpp`) за один обход AST
  строит единую таблицу символов `SymbolTable` (стек вложенных скоупов), регистрирует объявления
  в текущем скоупе и разрешает `Ident` поиском вверх. Семантика однопроходная: имя объявляется до
  использования. Ядро - **драйвер**; тяжёлая анализ-логика - в компонентах-анализаторах
  `DeclAnalyzer`/`ExprTyper`/`AccessResolver`/`TrustAnalyzer`,
  разделяющих `AnalysisContext`. `DeclAnalyzer` - фасад: объявления переменных/функций/
  деструктуризации делегируются `VarDeclAnalyzer`/`FuncDeclAnalyzer`/`DestructureAnalyzer`,
  типы (typedef/enum/variant/record/class, а также нативный шаблон-ТИП `<T> %Cpp() := ...;`) остаются
  в type-файлах фасада. Проверки сигнатуры функции - свободные функции
  `semantic/func_signature_check.{hpp,cpp}` (`validate*` - fatal, `check*` - report-only); порядок
  вызовов задаёт оркестратор `analyzeFuncDecl`. `ExprTyper` - фасад
  над профильными типизаторами
  (`VarTyper`/`CallTyper`/`MethodCallTyper`/`ContainerTyper`/`BinaryTyper`/`RefTyper`), у каждого -
  своя зона ответственности; общий доступ к сервисам драйвера и проверкам присваивания даёт
  `TyperBase`. `AccessResolver` отвечает только за доступ к элементам/полям, а типизация вызова
  метода делегируется `MethodCallTyper`. Общие хелперы — в зонах `semantic/core`
  (`ast_view.hpp`, `type_predicates.hpp`, `local_name.hpp`), `semantic/typer/literal_annotation.hpp`
  и `semantic/verify/format_check.hpp`.
- **Параллельные анализаторы** - `InlineAnalysisHook` (`semantic/core/inline_hook.hpp`):
  опциональные анализаторы подключаются к ядру и получают события в реальном времени обхода
  (`enterScope`/`exitScope`/`onDeclare`/`onResolve`/`onNode`/`finalize`), читая временные данные
  ядра (`SymbolTable`) через `AnalysisContext` - без повторного построения иерархии.
  Флаг включения проверяется **один раз** при сборке реестра в `SemanticPassRunner`; отключённый
  хук в список активных не попадает, его колбэки в узлах не вызываются.
- **Условие включения объявляет сам хук** (`InlineAnalysisHook::isEnabled`), имя зоны — `name()`;
  `SemanticPassRunner` содержит только упорядоченный реестр фабрик (порядок в списке = порядок
  колбэков, `activeHooks()` отдаёт его для тестов/трассировки), условий реализации в оркестраторе нет.
- **Анализатор Lint** - `LintHook` (`semantic/verify/lint.hpp`), включается `FlagKind::Lint`; сообщает о
  неиспользуемых переменных; режим задаётся строковым значением флага `-Wlint=aggressive`.
- **Анализ нативных ссылок + отслеживание инвалидации** - `NativeRefHook` (`semantic/nativeref.hpp`),
  атрибут
  `@[borrowed@]` на класс/тип, метод или переменную (авто-трекинга нет: значение-копии, в т.ч. `obj[i]`, и
  умные ссылки shared/weak/unique не отслеживает — их производные представления
  отслеживает `BorrowFlowPass` `-Wborrow-*`, тот же механизм инвалидации). Атрибут копируется
  при создании/присваивании. Любая мутация источника (присваивание/compound/incdec, запись в
  элемент/поле ИЛИ вызов не-const метода) при живых зависимых - диагностика severity
  `-Wborrowed=ignore|warning|error` В ТОЧКЕ МУТАЦИИ; снятие зависимости - `dep = _;` или
  привязка к другому источнику.
- **Анализ инвалидации производных представлений** - `BorrowFlowPass`
  (`semantic/analysis_api.hpp`), пост-проход по CFG/dataflow после основного обхода и DA, ДО
  lowering. **ВСЕГДА подключён** (гарантия языка/модели памяти); настраиваются только уровни
  диагностик `-Wborrow-<name>=<sev>` и `-Wborrowed=`. ⚠ Это **не** модель заимствования Rust:
  проверяется инвалидация view/наблюдателя при мутации/перемещении ИСТОЧНИКА (спецификация —
  `semantic/ref/BORROW_CHECKER.md`). Место — узел объявления (одноимённые переменные не
  смешиваются); живость per-path с освобождением на всех путях выхода, last-use для weak,
  escape (`borrow-escape`), мутация источника не-const методом, согласованность `@[lifetime]`.
  Диагностики: `borrow-region-mismatch`/`borrow-owner-moved`/`borrow-owner-mutated`/`borrow-escape`
  (R5 `borrow-static-unproven` не реализован).
- **SymbolTable (единая таблица символов)** - стек вложенных скоупов (`symbols/symbol_table.hpp`):
  каждый уровень - `Scope` с `std::map` имён и невладеющим `creator` (узел AST, открывший скоуп);
  глобальный скоуп (уровень 0) - плоская таблица глобальных имён. `push`/`pop`/`declare`/`resolve`/
  `lookup`; `pop` не удаляет глобальный скоуп. Владеется `AnalysisContext` и доступна через
  `runner.analysis().symbols()`.
- **AnalysisContext** - агрегат прогона БЕЗ собственной вычислительной логики: владеет `SymbolTable`
  и зонами `NamespaceScope`/`TypeResolution`/`ExprFacts`/`AnalysisState`; даёт доступ к `Context`
  (диагностики, типы, опции). `NamespaceScope` выводит позицию из скоуп-стека (кэш namespace-пути),
  `TypeResolution` резолвит типы/модульные ссылки/runtime-символы, `ExprFacts` — единственная точка
  чтения/записи type-state выражения, `AnalysisState` хранит LSP-проекцию, module-набор и счётчики временных.
- **Интеграция таблицы типов** - ядро связано с `TypeSystem` через общий сервис
  `AnalysisContext::types().resolveTypeRef` (скоуп → реестр); алиасы типов (`::=`) связываются в скоупе как
  `Symbol` (decl = узел `TypeDecl`); `FuncDecl` строит функциональную сигнатуру
  (`AnalysisContext::types().buildFuncType` → `getOrCreateFunctionType`) в `Symbol.type`;
  `SemanticPassRunner::run()` сбрасывает реестр к builtin-состоянию на каждый запуск
  (`TypeSystem::reset()`); разрешённый тип аннотации материализуется в узле
  (`IdentName::resolvedTypeId`), поэтому кодоген не нуждается в `SymbolTable`.
- **Валидация** - проверка дубликатов имён (в скоупе), неопределённых имён и типов,
  отсутствующих инициализаторов.
- **HIR и flow-анализы — НЕ в `run()`** - `SemanticPassRunner::run()` анализирует AST (она
  остаётся неизменяемой, кроме ErrorExpr-recovery) и завершает семантику. HIR-фазу выполняет
  `Pipeline` (`lowerAndAnalyze`): `buildHir` (клон AST + `hir::lowering`) → `verifyHir` (гейт контракта HIR) → `buildMir` → DA/borrow по MIR. Исходная AST не изменяется; транспилятор
  читает HIR и остаётся только кодогенератором. Имя текущей функции и стек имён живут в контексте
  lowering `LowerCtx` (`ir/lowering.hpp`; транспилятор их не хранит).
- **Модель фактов** - факты материализованы в ПОЛЯХ узлов AST (`ExprFacts` - единственная точка
  чтения/записи type-state); `AnalysisState` хранит только LSP-проекцию символов. Согласованность
  фактов с AST проверяет `semantic::verifyTypedAst` (ICE-политика, только на успешном прогоне).
- **Неявные конверсии аргументов/операндов** - после `core.run` (до построения HIR)
  `semantic::materializeArgCasts` записывает цели приведений в `CallExpr::argCasts` /
  `Binary::argCasts` по выбранной сигнатуре (`resolvedSignature`, для `operator()` -
  `resolvedOperatorSignature`). Эмиттеры печатают приведение по этим слотам и НЕ выводят его
  повторно из `FunctionTypeData` (решение принимается один раз, в семантике).
- **Общие продукты анализа — в `include/analysis`** - `SymbolTable`/`Symbol`/`SymbolIndex` (их использует
  и кодоген); в `semantic` - драйвер обхода, анализаторы и `AnalysisContext`.
  Поведенческие режимы (solver/stack-check) принадлежат semantic: value-типы — в `analysis/modes.hpp`,
  разрешение значений — `semantic::behavioralModesFromOptions` (передаётся в кодоген как данные).
- **Pipeline position** - расположен между Parser и CppTranspiler; при ошибках семантики
  генерация C++ не запускается.