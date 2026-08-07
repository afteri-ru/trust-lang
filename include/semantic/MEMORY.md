# MEMORY.md

> scope: include/semantic
> role: persistent-memory
> last_reviewed: 2026-08-19
> review_period: 30
> max_size: 60000

# Semantic Analyzer Architecture

## Purpose
Semantic analyzer walks the AST produced by the parser, builds a symbol table,
performs name binding and consistency checks. Implemented as a **pass manager**:
a mandatory core (symbol collection, name resolution) plus optional quality-control
passes gated by `diag::Options` feature flags. Reports errors via `diag`.

## Scope (Phase 1)
- Variable declarations with initialization (`x := 42;`, `x : Int32 := 42;`)
- Literals in expressions
- Validation: duplicate names, undefined names, undefined types, missing initializer

## Pipeline Position

`Parser → SemanticPassRunner → [on success] → CppTranspiler`.

## Единое ядро разрешения имён (NameResolutionPass)

`SemanticPassRunner` (`semantic/pass_runner.hpp`) создаёт единое **однопроходное** ядро
`NameResolutionPass` (`semantic/name_resolution.hpp`), которое за один обход AST:

- строит единую таблицу символов `SymbolTable` (`semantic/symbol_table.hpp`) — стек вложенных
  скоупов: вход в `ModuleDecl`/`ScopeBlock`/блок → `push`, выход → `pop`;
- регистрирует объявления (`VarDecl`/`FuncDecl`/`TypeDecl`) в текущем скоупе
  (`duplicate declaration` при коллизии в скоупе);
- разрешает `Ident` поиском вверх по стеку (`undefined name` при отсутствии).

Семантика **однопроходная**: имя должно быть объявлено до использования (forward references
исключены синтаксисом). Это объединяет `SymbolCollectorPass`+`NameResolverPass`, а также
раздельные `ScopeStack` и плоскую `SymbolTable` в одну структуру, которая служит
одновременно и реестром объявлений, и иерархией вложенности для разрешения имён.

## Валидация имён из C++-вставок (EmbedExpr)

`NameResolutionPass::handleNode` для узла `kind=EmbedExpr` (`{% ... %}`) извлекает trust-имена,
на которые ссылается вставка через маркеры `$name`/`@name` (`utils::extract_embed_names`, чтение
через `extract_name`), и проверяет каждое на доступность в таблице символов
(`SymbolTable::resolve`). Имя не найдено — `Severity::Warning`
(`embed references name '...' not declared in trust code`). Для квалифицированных имён
(`@ns::x`) таблица — плоский стек, поэтому проверяется последний сегмент. Саму конвертацию
`$`/`@` в C++-имена выполняет транспилятор (`transform_embed_cpp` → `name_to_cpp`).

## Параллельные анализаторы (InlineAnalysisHook)

Опциональные анализаторы подключаются **параллельно к ядру** через `InlineAnalysisHook`
(`semantic/inline_hook.hpp`): получают события в реальном времени обхода
(`enterScope`/`exitScope`/`onDeclare`/`onResolve`/`onNode`/`finalize`) и читают временные данные
ядра (`SymbolTable`, таблицы) через `AnalysisContext` — без повторного построения иерархии.
Мутирующий `onNode(AstNodePtr&)` позволяет хуку заменять узлы (возврат `true` = узел потреблён).

**`NameResolutionPass` НЕ является `InlineAnalysisHook` и намеренно не унифицируется с хуками**: это
**драйвер** (издатель событий) — он владеет обходом и скоуп-стеком и публикует события
подписчикам; хуки — **подписчики** (пассивные анализаторы). Такое разделение ролей исключает
дублирование без насильственной унификации интерфейсов: обход — единый `collectChildren`; контекст
и query-сервисы — единый `AnalysisContext`; раскрытие макросов — единый `ContextMacroExpander`.
Если новому анализатору понадобится полный обход — это сигнал вынести обход в переиспользуемый
хелпер (а не превращать ядро в хук).

Кроме опциональных есть **всегда-подключённый** `ContextMacroExpander` (`semantic/macro_expander.hpp`):
раскрывает контекст-макросы в том же обходе (см. ниже). Флаг включения опциональных хуков
проверяется **один раз** при подключении в `SemanticPassRunner`; отключённый хук в список активных
не попадает, и его колбэки в узлах не вызываются (ноль накладных расходов). Всегда подключается
`ContextMacroExpander`, опционально — `LintHook` (по флагу `FlagKind::Lint`).

`run()` возвращает `false` при блокирующих ошибках ядра (транспиляция не запускается);
`Lowering` — последним при отсутствии ошибок.

### Как написать анализатор (эталон: ContextMacroExpander)

`ContextMacroExpander` (`semantic/macro_expander.hpp`) — минимальный рабочий пример
анализатора-хука, по которому следует писать новые проверки (система эффектов, `@trust`,
линт и т.п.). Он демонстрирует полный цикл:

1. **Наследовать `InlineAnalysisHook`**; включение — через `gateFlag()` (опциональный хук,
   отключается по feature-флагу) либо `std::nullopt` (всегда, как макро-хук).
2. **Переопределить нужные события**: `onNode(AstNodePtr&)` — для узлов (может заменять узел;
   возврат `true` = узел потреблён, ядро пропускает `handleNode`), `onDeclare`/`onResolve` —
   для объявлений/резолва, `enterScope`/`exitScope` — для вложенности, `finalize()` — итоговый
   отчёт (как `LintHook::finalize` для неиспользуемых переменных).
3. **Читать состояние через общий `AnalysisContext`**: `symbols()` (таблица символов), `ctx()`
   (диагностика/типы) и query-сервисы `namespacePath()/currentFunc()/funcShortName()/
   qualifiedFuncName()/resolveType()/isRegisteredRuntimeSymbol()` — **без повторного разбора**
   скоуп-стека и реестра типов.
4. **Подключить в `SemanticPassRunner`**: `core.addHook(std::make_unique<MyHook>(*m_analysis));`
   до `core.run()`. Всегда-подключённый хук добавляется ПЕРВЫМ (как `ContextMacroExpander`),
   чтобы его `onNode` выполнялся до обработки ядра.

## AnalysisContext

Общий контекст семантики (`semantic/pass.hpp`): владеет единой таблицей символов `SymbolTable`
(стек вложенных скоупов); даёт доступ к `Context` (диагностики, типы, опции). Создаётся в
`SemanticPassRunner` заново на каждый `run()`; доступен через `runner.analysis().symbols()`.

Кроме данных `AnalysisContext` предоставляет **общие query-сервисы** (единый источник для ядра
и всех хуков — без повторного разбора скоуп-стека/реестра типов в каждом потребителе):

- **Контекст области имён/функции**: `namespacePath()` (путь текущей области имён),
  `namespaceFull()` (`"::ns::name::"`), `currentFunc()` (ближайшая функция), `funcShortName()`,
  `qualifiedFuncName()`, `requireFunction(node, macro)` (диагностика вне функции).
- **Резолв типов**: `resolveType(node)` — `TypeName` → `TypeId` (скоуп-стек алиасов с shadowing,
  затем реестр типов); `buildFuncType(func)` — `FunctionTypeId` по сигнатуре.
- **Runtime-символы**: `isRegisteredRuntimeSymbol(name)` — нативные функции из публичного
  runtime-заголовка (не дают «undefined name»).

## Components

### SymbolTable (единая таблица символов: стек вложенных скоупов)
Объединяет `SymbolTable` и `ScopeStack` в одну структуру (`semantic/symbol_table.hpp`),
которая служит одновременно и реестром объявлений, и иерархией вложенности для разрешения имён.
Каждый вложенный скоуп (модуль, блок, функция, будущий метод класса) — это `SymbolTable::Scope`:

- `std::map<std::string, Symbol> symbols` — имена уровня (детерминированный порядок → стабильные
  диагностики);
- `const AstNodeBase* creator` — невладеющий указатель на узел AST, открывший скоуп
  (nullptr для глобального), для диагностик и хуков;
- `Scope::lookup(name)` — поиск в пределах одного скоупа.

Глобальный скоуп (уровень 0) всегда присутствует (`depth() >= 1`, `pop` его не удаляет) и является
плоской таблицей глобальных/статических имён (`global()`/`globalSize()`). `SymbolTable::declare`
регистрирует символ в текущем скоупе (дубликат — в пределах скоупа, false; диагностику формирует
ядро, т.к. ему нужен range); `SymbolTable::resolve` ищет от текущего скоупа вверх по стеку
(учитывает вложенность и shadowing). `SymbolTable` не владеет `DiagnosticEngine`.

Владение символами — у `SymbolTable` (по значению в `Scope::symbols`). `SymbolTable` владеется
`AnalysisContext` (value-член) и доступен через `runner.analysis().symbols()`. Указатели на
`Symbol`, возвращаемые `lookup()`/`resolve()`, невладеющие и валидны, пока скоуп не удалён (`pop`)
или таблица не пересоздана. `Context::symbols()` НЕ существует. Генератор кода (`CppTranspiler`)
работает с типами через `ctx.types()`, а при передаче разрешённой таблицы символов
(`CppTranspiler(ctx, &runner.analysis().symbols())`) использует тот же TypeId, что и анализ.
Стек скоупов — внутренняя реализация `SymbolTable` и наружу не экспонируется.

### Месторасположение переменной (`Symbol::storage`) и нормализация имён без сигила
`Symbol` несёт `Storage storage` (Global/Local/Static/ThreadLocal) — физическую память переменной,
фиксируемую анализатором при объявлении (`analyzeVarDecl`; параметры — `Local`):
- `ThreadLocal` — атрибут `@[thread_local]`;
- `Static` — имя содержит область имён (`::`);
- `Local` — объявление внутри функции (в стеке скоупов есть `FuncDecl`);
- иначе — `Global`.

Опция `-Wsigil` (severity `OptKind::NoSigil`, default Warning) управляет нормализацией простого имени
без сигила, объявленного в **локальном** скоупе через `:=`: имя нормализуется к `$<name>` (символ
регистрируется как `$x`, текст узла VarDecl перезаписывается через `HasText::set_text`) и выдаётся
предупреждение «creating a local variable '$x'» с **быстрым фиксом** (замена bare-имени на `$<name>`,
`ctx.diag().fixit(...)` → LSP quickfix). Глобальный уровень и `::=` (типы) не трогаются.

**Единый алгоритм разрешения простого имени** (`resolveSimple`/`resolveSimpleRead`, используются в
`lookupOrError` и при резолве LHS присваивания/append) применяет **правила вывода сигилов**:
- bare-имя `x` ищется как `$x` (локальная; при попадании текст узла-ссылки нормализуется на `$x`),
  затем как `x` (глобал/параметр), затем как `%x` (нативная функция; текст узла нормализуется на
  `%x`) — так `%fib` можно вызывать как `fib`;
- `$`-имя `$x` — сначала `$x`; если нет, то bare `x` (параметр/локальная без сигила): `n` и `$n` —
  одно локальное имя;
- квалифицированные/сигилные/нативные имена, найденные напрямую, резолвятся как есть.
Не найдено ни одной формы → «undefined name».

### NameResolutionPass (ядро)
Однопроходный обход AST (см. «Единое ядро разрешения имён» выше). Обработка узла по kind
выполняется в `handleNode()`, а **полный обход всех детей — через единый `AstNodeBase::collectChildren`**
(ссылки на слоты; `children()` — его const-обёртка): `analyzeNode()` сначала вызывает хук
`ContextMacroExpander`, обрабатывает узел, затем гарантированно рекурсивно обходит каждого
ребёнка. Это обеспечивает посещение КАЖДОГО узла
AST (в т.ч. идентификаторов в выражениях/условиях/`return`), а не только достижимых через
контейнерные kinds. Проход использует собственный `switch (node.kind())`, а не наследует
строгий `KindVisitor` (`include/ast/kind_visitor.hpp`) — это осознанное отклонение, применимое
к проходам, обрабатывающим подмножество kinds. Корневой узел реального pipeline — `ModuleNode`;
ядро обходит его `m_body`. Имя функции регистрируется во внешнем скоупе, параметры — во
внутреннем (скоупе функции), чтобы имена в теле резолвились.

### Интеграция таблицы типов (TypeRegistry)
Ядро связано с `TypeRegistry` (`ctx.types()`) следующим образом:

- **Единый резолв типа** — `AnalysisContext::resolveType(node)` возвращает `TypeId` по
  аннотации (kind=TypeName): сначала по скоуп-стеку (пользовательские алиасы, с учётом shadowing),
  затем — в реестре (builtin). Используется для аннотаций переменных, параметров и возврата функции.
- **Алиасы типов** — `analyzeTypeDecl` (`y ::= Int`) регистрирует алиас в реестре (`registerType`)
  и **связывает имя в текущем скоупе** как `Symbol` (decl = узел `TypeDecl`, `Symbol.type = aliasId`).
  Это даёт shadowing и коллизии имени типа с переменной/функцией через скоуп-стек.
- **Функциональные типы** — `analyzeFuncDecl` строит сигнатуру через `AnalysisContext::buildFuncType` →
  `TypeRegistry::getOrCreateFunctionType(returnType, paramTypes)` и сохраняет `FunctionTypeId`
  в `Symbol.type` (параметры и возврат резолвятся через `resolveType`).
- **Forward-объявления** — синтаксис `<name> := ...;` (многоточие вместо тела функции /
  инициализатора переменной) регистрирует имя и тип без тела/инициализатора. `Symbol` хранит
  невладеющий указатель на узел объявления (`Symbol.decl`) — источник истины: kind, range, атрибуты
  и определение. Forward-признак определяется по узлу (`isForwardDecl`): `VarDecl.m_initializer ==
  nullptr` или `FuncDecl.m_body == nullopt`. Для **нативных имён** (`%...`,
  транслируются в C++ напрямую) в forward-объявлении тип обязателен: нативная переменная без
  `m_type` и нативная функция без типа возврата дают ошибку; для обычных имён тип опционален.
- **Завершение forward определением** — `SymbolTable::declareOrComplete(sym)` возвращает
  `DeclResult{Inserted, Completed, Duplicate}`. Завершение (`Completed`) допустимо, когда
  существующий символ в текущем скоупе — forward-объявление (`isForwardDecl`), новый — определение,
  и kinds совпадают (kind узла `decl`): определение заменяет forward-символ in-place. Два определения,
  два forward и конфликт kinds (func vs var) → `Duplicate`. `analyzeVarDecl`/`analyzeFuncDecl`
  используют `declareOrComplete` вместо `declare`. Тип завершающего определения не сверяется с
  forward (это future refinement); параметры объявляются через plain `declare` (без завершения).
- **Жизненный цикл** — `SemanticPassRunner::run()` вызывает `TypeRegistry::reset()` в начале,
  чтобы алиасы и функциональные типы не накапливались между run() (согласовано с пер-ран SymbolTable).
- **Проброс в кодогенерацию** — `CppTranspiler` при наличии разрешённой `SymbolTable`
  (`&runner.analysis().symbols()`) в `resolveCppType` сначала берёт TypeId из неё, затем — из реестра.


### InlineAnalysisHook / LintHook
`LintHook` (`semantic/lint.hpp`) — опциональный анализатор неиспользуемых переменных
(gate = `FlagKind::Lint`); режим `-Wlint=aggressive` → Error. Будущие анализаторы (Effect/Trust)
реализуются так же через `InlineAnalysisHook` + `gateFlag()` и подключаются по флагу в
`SemanticPassRunner`.

#### Раскрытие контекст-макросов (хук ContextMacroExpander)

Контекст-макросы (`@::`/`@__NAMESPACE__`, `@__FUNCTION__`, `@__FUNCSIG__`, `@__FUNCDNAME__`)
раскрываются **в том же однопроходном обходе** `NameResolutionPass`, но **отдельным
всегда-подключённым хуком** `ContextMacroExpander` (`semantic/macro_expander.hpp`), а не ядром:
ядро остаётся чистым разрешителем имён. Хук вызывается ядром в начале обработки каждого узла
(`analyzeNode`), заменяет узел `ContextMacro` (его создаёт парсер, см. `ast/MEMORY.md`) на
`Literal`/`IdentName` и раскрывает квалификатор `@::`. Контекст области имён и текущей функции
**не хранится отдельно** — это общие методы `AnalysisContext` (`semantic/pass.hpp`), выводимые из
скоуп-стека `SymbolTable` (итерация `forEachScope` по создателям скоупов, сравнение kind снизу
вверх): `namespacePath()` — сегменты namespace-`ScopeBlock`,
`currentFunc()` — ближайший `FuncDecl`:

- **value/стрингификация** — узел `ContextMacro` заменяется на `Literal(StrChar)`:
  - `@::`/`@__NAMESPACE__` → полная область имён `"::ns::name::"` (глобальная → `"::"`);
  - `@__FUNCTION__` → краткое имя функции;
  - `@__FUNCSIG__` → сигнатура `"ns::func(arg:Type):Ret"` (всегда строковый литерал);
  - `@__FUNCDNAME__` → `utils::name_to_cpp(<полное имя функции>)`;
- **имя-аналог** (без стрингификации) — `@__FUNCTION__`/`@__FUNCDNAME__` → `IdentName` с
  раскрытым именем; `@::`/`@__NAMESPACE__` — имя области;
- **метка** `++`/`--` — `@__FUNCTION__` в `JumpStmt::m_label` → имя функции;
- **квалификатор** `@:: foo` — маркер уже свёрнут в текст идентификатора (`@::foo`
  `FinalizeAndTest`); раскрывается текстовой заменой `@::` → текущая область имён (`ns::foo`),
  в т.ч. в имени объявления (`@:: x := 1` → `ns::x`).

Стрингификация помечается ведущими маркерами `@#`/`@#'`/`@#"` в `text()` узла (их может быть
несколько). Функциональные макросы (`@__FUNCTION__`, `@__FUNCSIG__`, `@__FUNCDNAME__`) вне
функции — диагностика `Severity::Error`. Транспилятор узлы `ContextMacro` не обрабатывает
(после прохода их нет; `visit_ContextMacro` = `FAULT`).

Обход мутирующий: `ContextMacroExpander::onNode(AstNodePtr&)` вызывается ядром в начале
обработки каждого узла и возвращает true, если узел заменён (ядро пропускает `handleNode`,
но продолжает обход детей). Сам обход детей — через единый `AstNodeBase::collectChildren`
(ссылки на слоты, чтобы можно было заменять `ContextMacro` на `Literal`/`IdentName`).

Логика раскрытия инкапсулирована в узлах: сигнатуру строит `FuncDecl::signature(ns)`,
раскрытие квалификатора `@::` — `IdentName::expandQualified(ns)`; `name_to_cpp` — утилита.

#### Lowering (вставка синтетических узлов для транспилятора)

`SemanticPassRunner::run()` после успешной семантики запускает проход **lowering**
(`lowerBody`), который переносит в AST всю «анализирующую» логику, чтобы транспилятор остался только кодогенератором. Понижение реализовано **в классах
узлов** (компонент `ast`, `ast/lowering.hpp` + `src/ast/lowering.cpp`): каждый класс
переопределяет `virtual AstNodeBase::lower(self, ctx)` согласно своему Kind и рекурсивно
понижает своих детей; runner только запускает `lowerBody` на корневом векторе операторов:

- **Точка с запятой для statement-выражений** — statement-позиции выражений (бинарные kinds,
  литералы, `CallExpr`) оборачиваются в `SemicolonStmt` (содержит `m_expr`; range делегируется ребёнку).
- **Метки goto (именованные break/continue)** — именованный `BreakStmt` переписывается в
  `GotoStmt(<имя>_break)`, `ContinueStmt` — в `GotoStmt(<имя>_continue)`; break по имени текущей
  функции — в синтетический void-`return;` (узел без Term, невалидный range — без source-map,
  как LabelRef/прочие узлы lowering). Безымянные break/continue остаются как есть.
- **Метки именованных блоков** — именованный `ScopeBlock` внутри функции получает
  `LabelStmt(<имя>_break)` после тела; continue-метка `LabelStmt(<имя>_continue)` ставится
  первому циклу в теле (перед `while`, в конец тела `do-while`). Вне функций метки не вставляются.

Контекст lowering `LowerCtx` несёт `inFunction`, имя текущей функции и pending continue-метку
именованного блока. Класс `LabelRef` (kinds `GotoStmt`/`LabelStmt`) — синтетические узлы без
исходного trust-текста: их `range()` возвращает НЕВАЛИДНЫЙ range (не маппятся), т.к. сопоставлять
сгенерированные `goto`/метку с исходником не нужно. `SemicolonStmt` делегирует `range()` обёрнутому
выражению (валиден) — `;` принадлежит реальному source-выражению.

Extended analysis (beyond Phase 1):
- `NameResolutionPass::analyzeVarDecl` — variable declarations (`:=`) with type/name validation
- `NameResolutionPass::analyzeTypeDecl` — type aliases (`::=`) with type resolution
- `NameResolutionPass::analyzeFuncDecl` — function declarations (имя + duplicate check)
- `NameResolutionPass::lookupOrError` — symbol lookup with diagnostic on failure (через ScopeStack)

## Инференс типов выражений (post-order)

`NameResolutionPass` дополнительно выполняет **типизацию выражений в пост-порядке**
(после обхода детей), см. `typeExpr` + `semantic/type_inference.hpp`. Тип узла
вычисляется единым query-сервисом `AnalysisContext::resolvedType(node)` (лист — литерал/
символ/каст, составное выражение — из кеша, заполняемого ядром через `setExprType`):

- **Литералы** (`literalType`): IntLiteral → минимальный конкретный знаковый Int,
  вмещающий значение (Int8/16/32/64); FloatLiteral → Float64; StrChar ('…') → StrChar;
  StrWide ("…") → StrWide; RationalLiteral (`num\den`, отдельная лексема RATIONAL) → Rational.
  Вычисленный `TypeId` кешируется на узле (`Literal::typeId`) — часть сознательной архитектуры
  «аннотации типов на узлах AST» (см. TYPE_INFERENCE.md §2.10), НЕ side-table.
- **Литерал словаря/кортежа/конструкции** `(1, two=2, name='3',)` / `:Tuple(...)` / `:Type(...)` →
  AST-узел `DictLiteralNode` (подкласс `Sequence`, поле `m_type` — аннотация типа; никаких строк/enum).
  Контракт: все элементы m_body — единый узел `ArgNode` (имя в text(), явный тип в m_type, значение
  в m_value), строятся из канонических пар грамматики `args` (см. `term_to_ast::visit_DICT`);
  (единая форма ArgNode, без raw/AssignOp/ParamDecl). `analyzeDictLiteral`: значение каждого элемента анализируется полностью; имя-метка
  НЕ резолвится как ссылка на переменную и НЕ регистрируется в таблице символов. Класс узла решает
  анализатор ПО ТИПУ из реестра: если `m_type` резолвится в `Tuple` — `setKind(Tuple)` и тип выражения —
  интернированный структурный кортеж (в C++ → `auto`, `std::tuple`); если аннотации нет — универсальный
  словарь `Dict`; если `m_type` резолвится в скаляр/класс — типизированная конструкция/каст
  (решение в кодогенерации). Тип значения элемента сохраняется на элементе
  (`Binary::resultType` из `resolvedType`) — единый источник для кодогенерации `TypedValue`
  (покрывает литералы, вложенные словари и выводимые выражения, не только `Literal::typeId`).
- **Аннотация структурного кортежа** `Tuple(:Rational, :Rational)` / `name:Type` в позиции типа
  (возвращаемый тип функции, тип переменной) → `resolveType` строит структурный Tuple-тип через
  `getOrCreateTupleType` (позиционные имя="", именованные — ArgNode(name, type)). Такой тип
  становится возвращаемым типом функции → `getOrCreateFunctionType` интернирует функции по нему.
- **Распространение return-типа вызова**: `p := f(...)` типизируется возвращаемым типом функции
  (typeExpr CallExpr → FunctionTypeData.returnType); результат вызова не сводится к std::any. Для
  кортежного возврата `p` получает структурный Tuple-тип, `p.0`/`p.name` резолвятся через std::get.
- **Доступ к элементу словаря** (`d.two` / `d.1` / `d[0]`) → узлы MemberAccess/ArrayAccess
  (объект в m_left, ключ/индекс в m_right). `analyzeAccess`: объект анализируется, поле-имя
  справа от '.' НЕ резолвится как переменная; статический индекс `d.1` проверяется по
  статической размерности (`Symbol::dims`, копируется из литерала-инициализатора) — размер
  неизвестен или индекс вне диапазона → ошибка компиляции. Оператор `[]=` (`AppendStmt`)
  увеличивает `Symbol::dims` известного словаря на 1 для одиночного элемента (см. `handleNode`);
  при spread-merge `d []= ... dict` (RHS — узел `Ellipsis`) размер растёт на число элементов
  распаковываемого словаря, а его типы полей переносятся в `Symbol::dictFieldTypes` цели (для
  литерала-операнда — по элементам, для переменной-словаря с известным размером — копированием
  `dictFieldTypes`). Поэтому статическая проверка `d.N` далее по тексту учитывает добавленные
  элементы (после двух append размер 3 → 5,
  `d.4` допустим, `d.5` — вне диапазона). Тип результата доступа — **тип
  поля**: выводится из литерала (`Symbol::dictFieldTypes`: имя/позиция → TypeId элемента,
  напр. `d.two` → Int8, `d[0]` → Bool) или `Any` (гетерогенный/неизвестный). `Symbol::dims` —
  компиляционное свойство размера, `Symbol::dictFieldTypes` — типы полей (см. модель Dims).
- **Деструктуризация `t1, ..., tN := [... ]source;`** → узел `DestructureDecl` (терм `:=` с
  многоимённым LHS; цели как `IdentName` из термов lval; `m_isSpread` — был ли RHS `...`; суффикс `...`
  у цели (`rest...`) — «остаток», `_...` — отброс остатка, `_` — skip одного). Семантика
  `analyzeDestructure`: источник анализируется; проверка типа источника (спред — только Dict, иначе
  Error «must be a dictionary») и статической арности (`dictSizeOf`). **Без маркера — точная привязка**
  (Python/Rust/Go/C++/Haskell): число целей == числу элементов для статически-известного размера;
  **кортеж** — связывание по индексу с типом элемента, арность проверяется; `rest...` — остаток
  (для кортежа — под-кортеж), `_...` — отброс остатка. Цель остатка, совпадающая с источником, —
  мутация `pop_front` (отдельного объявления нет). **Per-element типизация целей** (как кортеж):
  `dictElementTypes` даёт тип каждого элемента → `naturalRuntimeType` (Int8..Int64 → Int64, Float →
  Double, Bool, Str...), цель типизируется своим runtime-типом. **В цикле** (`isInLoop`, циклы создают
  скоуп) тип расширяется до МАКСИМАЛЬНОГО среди элементов (`joinElementTypes`: Bool+Int → Integer,
  float → Double) и присваивается runtime-конвертером; предупреждение `OptKind::WidenAny`. `Any` —
  только если тип не выводим. Кодген — `pop_front()` (Dict) / `std::get<N>` (кортеж). Вложенная
  деструктуризация (`a, (b, c) := t;`) не поддерживается — только отдельные переменные (грамматика
  `assign_item`: `lval` / `lval ELLIPSIS`). **Присваивание** (`t1, ..., tN = [... ]source;`,
  `DestructureDecl::m_isAssign`) — цели НЕ объявляются: `assignDestructureTarget` резолвит существующую
  переменную (несуществующая / константа `^` — Error), тип цели кладётся в `m_targetTypes` (для кодгена
  `any_cast<T>`); rest == источник — мутация pop_front. **Явная аннотация типа цели** (`a:Type`,
  `m_targetTypeNodes`, `explicitTargetType`) фиксирует тип ДЕКЛАРИРУЕМОЙ переменной
  (`m_targetDeclaredTypes`; кортеж: `int32_t c_a` вместо `auto`), а тип `any_cast` берётся из
  `m_targetTypes` = natural runtime тип элемента (Dict хранит int как int64_t), иначе тип выводится.
  **Валидация rest-цели** (`restTargetNameAllowed`/`canonicalTargetName` — сигил-нормализация без
  мутации узла): переиспользование имени именованной rest-цели допустимо ТОЛЬКО как мутация-идиома
  spread-словаря (`item, dict... := ... dict`, rest == источник); прочее переиспользование (в т.ч.
  кортеж `a, t... := t`) — Error (кодген без этого даёт C++-redefinition/UB). **Аннотация типа
  на rest-цели** (`rest:Type...`) — Error «rest type is inferred» (тип остатка всегда выводится:
  Dict / под-кортеж). **Вне цикла** невыводимый тип элемента словаря
  (naturalRuntimeType → INVALID, напр. словарь-параметр) → предупреждение OptKind::WidenAny + Any
  (симметрия с цикловым widening; без тихого fallback).
- **Тип переменной** (признак «выведен» — бит `kInferredFlag` в TypeId, см. `types/MEMORY.md`):
  нетипизированная `x := expr` получает тип из инициализатора с битом `withInferred`; по истории
  присвоений (`=`, `+=`, ...) тип **монотонно расширяется** (join: `x := 1; x = 1000;` → Int16).
  Инициализатор без выводимого типа (C++-вставка `{% %}`, вызов с неизвестным результатом,
  отрицательный литерал) **явно маркируется типом `std::any`** (`VarDecl::inferredType` и
  `Symbol::type`), чтобы транспилятор не угадывал тип тихим fallback — `INVALID` у переменной
  с инициализатором в кодогенерации трактуется как ошибка вывода (без fallback на `std::any`).
  Голый тип-имя в правой части `:=` (`x := :Int32`) — **ошибка** (в `:=` справа значение, тип
  объявляется через `::=`); диагностируется в `analyzeVarDecl`.
  Авто-выведенный `Bool` (`mult := 1`), используемый в составной числовой арифметике
  (`mult += 1`), расширяется до максимального Int (`Int64`); явный `:Bool` так НЕ расширяется —
  для него это ошибка. Явно-типизированная (`x:Type :=`) — фиксирована (без бита, присвоения не
  расширяют). Живой тип хранится на символе (`Symbol::type`), финальный структурный — на узле.
- **Результат бинарной операции** (`resultTypeBinary`) — по обычным арифметическим
  преобразованиям C++: `Int16 + Int16 → Int32`, `//`/`//=` → Int64, Compare/Logical → Bool,
  присутствие float-операнда → более широкая float-группа. Один операнд `std::any` + конкретный
  числовой → результат = продвинутый конкретный (для `std::any_cast` при кодогенерации).
  Типы операндов/результата сохраняются на узле `Binary` (`lhsType/rhsType/resultType/commonType`).
- **Продвижение auto-Bool в арифметике** (`typeBinaryResult`): если операнд имеет тип Bool и
  **выведен автоматически** (`typeIsInferred`, из литерала `0/1` или inferred-переменной) — он
  продвигается по общим правилам приведения (C++ `bool→int` → Int32), и результат вычисляется с
  продвинутым типом (`d := (1, 2); d[0] + d[1]` → Int32; `1 + 2` → Int32). **Явный Bool**
  (`:Bool`, результат сравнения/логики) в арифметике — ошибка компиляции (нельзя привести).
- **Константность переменной** (бит `kConstFlag` в TypeId, см. `types/MEMORY.md`): при `^` на имени
  или `@[readonly]@` (атрибут `attr::ReadOnly`) семантика ставит бит на тип переменной
  (`Symbol::type`) — «константность в типе» (`x^ := 42` → `const int8_t`). Для типизированных бит
  ставится в `analyzeVarDecl`, для нетипизированных — в `typeExpr` при выводе типа из инициализатора
  (структурный `VarDecl::inferredType` остаётся без бита). `resolveCppTypeId`/кодогенерация читают
  бит для префикса `const `; пер-переменная константность (ставится по мере анализа) и мост
  `const_cast<>` — см. `types/CONST.md`.
- **Вид ссылки** (`RefType`, биты 16–19 `TypeKind`, см. `types/REFType.md`): атрибут
  `@[reftype(имя)]` перед объявлением (типизированная переменная) ставит признак на
  `Symbol::type` в `analyzeVarDecl`. Первая ссылка на тип без признака — fast-path бит
  (пересборка `TypeId` с сохранением registry_index/флагов); ссылка на уже ссылочный тип —
  составной узел `TypeRegistry::getOrCreateRefType` (`RefTypeData`, группа `kReftype`).
  Неизвестное имя вида или отсутствие параметра — диагностика ошибки.
- **Become-const и защита от записи** (в `typeExpr` для присваиваний `=`/`+=`/…): LHS с `^`
  (`attr::ReadOnly` на узле `Ident`/`IdentName`) — финальная запись, помечающая переменную константной
  (`x := 42; x^ += 1;` → далее `x` неизменяема, бит `kConstFlag` на `Symbol::type`). Обычная запись
  (без `^`) в уже константную переменную — диагностика ошибки «cannot assign to constant variable».
  Декларация такой переменной остаётся не-const (кодогенерация берёт const объявления из атрибута узла).
- Выведенный конкретный тип сохраняется в **`VarDecl::inferredType`** на узле объявления —
  транспилятор читает его при кодогенерации, т.к. скоуп-стек к этому моменту уже сброшен
  (модульные имена из popped-скоупов недоступны через `SymbolTable::resolve`).

Типы результатов составных выражений кешируются в `AnalysisContext::m_exprTypes`
(карта `node → TypeId`) для рекурсивной типизации вложенных выражений: ядро пишет через
`setExprType`, единый `resolvedType` читает. Бинарные kinds типизируются одним хелпером
`NameResolutionPass::typeBinaryResult` (объединяет MathOp-группу и AssignOp:
вычисляет `lhsType/rhsType/resultType/commonType`, кладёт результат в кеш); сужение в явную
цель и расширение выводимой цели выполняет `typeExpr`. AppendStmt (`X []= v`) — исключение:
типизируется в `typeExpr` отдельной веткой (append не меняет тип цели; `lhsType`=тип контейнера,
`rhsType`/`resultType`=тип значения), чтобы оператор `[]=` не попадал в сужение/расширение
составного присваивания. Spread-merge `X []= ... dict` (RHS — узел `Ellipsis`) единичным
значением не является: `resultType` остаётся INVALID, и кодогенерация идёт в ветку `extend`;
проверяется, что контейнер-цель — словарь (иначе ошибка). Числовое продвижение — единый
TypeId-aware источник `types/promotion.hpp` (см. `types/MEMORY.md`). Классификация операторов
(целочисленное деление `//`/`//=`, составное и простое присваивание) — единый источник
`utils/operators.hpp` (`isIntDivOp`/`isCompoundAssignOp`/`isPlainAssignOp`), используемый
и семантикой, и транспилятором вместо дублирования операторных строк.

**Единые источники типизации (устранение дублирования):** набор «типизируемых бинарных kinds»
(`MathOp|BitwiseOp|CompareOp|LogicalOp|NameDecl|AssignOp|AppendStmt`) — единый предикат
`ast::is_binary_expr_kind` (используется в `resolvedType` и `typeExpr`); числовые группы —
`types::isArithmeticGroup`; проверка «std::any»-операнда — `types::isAnyType` (единый для семантики
и транспилятора). Диапазоны целых литералов (`literalType` и сужение `intFitsTarget`) —
единый `types/int_literal.hpp` (`fitsIntegerValue`/`intTypeForWidth`/`intTypeForLiteral`). Типы
литералов кешируются в `m_exprTypes` (как и составных выражений), чтобы `resolvedType` не
пересчитывал `literalType`.

## Сужение в типизированную цель (checkAssignmentNarrowing)

При присвоении/инициализации значения в **ЯВНО-типизированную** переменную
(`x:Type := expr`, `x = expr`, `x += expr`) анализатор проверяет сужение по ширине целого типа:
- литерал, влезающий в целевой тип → безопасно (без диагностики);
- литерал, не влезающий (`x:Int8 := 1000`) → ошибка;
- переменная/неизвестное шире цели (`b:Int8 := a` при `a:Int64`) → **ошибка по умолчанию**
  + fixit «use cast `:Type(expr)`» (через `DiagnosticEngine::fixit`).
Inferred-цели (без аннотации) не проверяются — их тип монотонно расширяется (join), см. выше.

## Компиляйт-тайм проверка printf-формата (@[format("printf", ...)])

Атрибут `@[format("printf", <string_index>, <first_to_check>)]` (GCC-аналог
`__attribute__((format(...)))`) включает проверку типов аргументов нативного вызова на
соответствие форматной строке printf. Индексы — 1-based (конвенция GCC).

- **Парсер и сверка** — `semantic/format_check.hpp/.cpp`: `parse_printf_format` разбирает
  printf-спецификаторы (`%[flags][width][.prec][length]conv`, `%%` — литерал) в список
  ожидаемых категорий; `arg_matches_expect` сверяет категорию с фактическим типом аргумента
  (integer/unsigned/float/string/pointer).
- **Точка проверки** — `NameResolutionPass::checkFormatArgs` вызывается из `typeExpr`
  (`case CallExpr`) **пост-порядково**, когда типы аргументов уже вычислены (`resolvedType`).
  Для callee-функции с атрибутом `format`: читаются `(string_index, first_to_check)`,
  формат-строка обязана быть **строковым литералом** (иначе «format string is not a string
  literal»), затем каждая конверсия сверяется с типом соответствующего аргумента.
- **Опция** — `OptKind::Format` (`-Wformat=error|warning|ignore`), default `Error`.

Связанные изменения: `emitTypeNameForNode`/`resolveCppTypeId` учитывают атрибуты узла типа
(`reftype`, `ReadOnly`), спецправило `StrChar + ptr + const → const char*`
(`TypeRegistry::resolveCppTypeId`); вариативный параметр `...` эмитится в C++ как `...`;
для `%s` ожидается C-строка `const char*` (тип `CString`), `StrChar`-литерал допустим,
`StrChar`-переменная требует явного `.c_str()`.

## Методы на встроенных типах (obj.method)

- **Реестр** — нативные методы на типах хранятся в `TypeRegistry` (`TypeDescriptor::methods`:
  `std::map<std::string, TypeId>` имя → функциональный тип, `addMethod`/`findMethod`). Метод и
  функция — одно и то же: сигнатура хранится как `FunctionTypeData` (через
  `getOrCreateFunctionType`, структурное интернирование). Нативность метода — по `%` в имени
  (`%c_str`): при генерации C++ вставляется идентификатор без `%`.
- **Инвариант «одна форма имени»** — `addMethod` отклоняет регистрацию имени, если уже
  присутствует его вторая форма (`c_str` vs `%c_str`) или точный дубль: EXPECT при инициализации
  реестра (в т.ч. в тестах).
- **Поиск** `findMethod` — ОДНОСТОРОННИЙ (как разрешение имён trust): обычное имя (`c_str`)
  находит и обычный, и нативный (`%c_str`) метод; нативное имя (`%c_str`) — только точное.
  Возвращает функциональный тип метода или `INVALID_TYPE_ID`.
- **Семантика** — `NameResolutionPass::handleMethodCall` (из `analyzeAccess`, при
  `MemberAccess` с `CallExpr`-справа): по типу объекта ищет метод, проверяет наличие
  (диагностика «type 'X' has no method 'Y'») и число аргументов по `FunctionTypeData::paramTypes`,
  типизирует результат возвращаемым типом — ВСЁ до генерации C++.
- **Кодогенерация** — `CppTranspiler::visit_MemberAccess`: `s.c_str()` → `(c_s).c_str()`.
- **Пример** — `StrChar.%c_str(): CString` (тип `CString` — невладеющий `const char*`,
  см. `types/REFType.md`).

## Сбор символов для LSP (SymbolCollectorHook)

`SymbolCollectorHook` (`semantic/symbol_collector.hpp`) — `InlineAnalysisHook`, включаемый флагом
`FlagKind::Symbols` (`-Wsymbols`). На `onDeclare` запоминает `{name, type, decl, scopeRange}`
(имя берётся из `sym.decl->text()`, т.к. `declareOrComplete` перемещает `Symbol` в таблицу и
`sym.name` на onDeclare уже moved-from), в `finalize` пишет в `AnalysisContext::symbolIndex()`
(`SymbolIndex` = `vector<SymbolInfo{name, type(TypeId), typeName, nameRange, scopeRange, isMacro, documentation}>`).
Документирующий комментарий (`///`/`##`/`/**`, т.ч. хвостовой `///<`/`##<`) копируется из
`decl->documentation` (`AstNodeBase`): его заполняет грамматика через `term->m_docs` (см.
`include/syntax/MEMORY.md`) и переносит в узел `TermToAstConverter::convert`. Для не-объявлений
док остаётся отдельным sibling-узлом `Document`, к символам не привязывается.

`SemanticPassRunner::takeSymbolIndex()` отдаёт собранное (перемещением). Pipeline помещает его
в `PipelineResult::symbols` **даже при ошибках** (на частичном AST), если флаг включён.

## Facts and invariants

- **Массивы (Array<Elem>)**: литерал `[1,2,3,]`/`[...]:Type` (kind=ArrayInit, node_type
  DictLiteralNode) и конструкция `:Array(...)`/`:Array^(...):Elem` интернируют структурный
  `Array<Elem>` через `getOrCreateArrayType`. Тип элемента: `]:Type` > аннотация элемента
  (`2:Int8`) > `arrayElementJoin` (узкая разрядность: `[1,2,3,]` → Int8, `[100,300,]` → Int16);
  const-контейнер (`^` → attr::ReadOnly) → `withConst(arr)` (kConstFlag-бит TypeId → std::array).
  Вложенные литералы/многомерные `:Elem[N,M]` строят многомерный Array-тип (анализ работает;
  «не реализовано» — только на кодогенерации, `isMultiDimArray`). Индексный доступ `a[i]`
  (left — Array-тип) → `resolveArrayAccess` (тип = elementType, статический индекс по размерности).
  Методы объявлены на абстрактном `:Array` (T→Elem через `instantiateArrayMethod`).


> ⚠ trap: enum-члены сравниваются **ПО ЗНАЧЕНИЮ**, но type-safe (оба операнда — enum; нельзя `Weight.ZERO == 0`).

- **Enum** — единый тип значений у всех членов (valueType выводится по общим правилам: явная аннотация члена / resolvedType значений / минимальный Int по числу членов); **Variant** — каждый член СВОЕГО типа (из аннотации / значения / Int по позиции).
- **Явный тип члена `name:Type` в enum/variant** хранится в `ArgNode.m_type` (отдельный слот) и читается НАПРЯМУЮ (подробнее см. `ast/MEMORY.md`).
- **НОРМАЛИЗАЦИЯ ГРАММАТИКИ (parser.y.in):** правила типизированных аргументов `name type_item named_rhs` и `ptr name type_item named_rhs` кладут тип в ЕДИНЫЙ слот `m_type` ARGUMENT-терма (а НЕ в `m_right->SetType` / `$4->SetType`). `Term::toString()` для ARGUMENT печатает тип из `m_type`. Безымянный `name:Type` (без =value) и голые значения остаются НЕ-ARGUMENT (нормализует в ArgNode конвертер, `appendDictElementsFromArgs`).

