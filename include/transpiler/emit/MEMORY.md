# MEMORY.md

> scope: include/transpiler/emit
> role: persistent-memory
> last_reviewed: 2026-11-04
> review_period: 30
> max_size: 14000

## Architecture

Общий контекст кодогенерации `CppEmitContext` (`emit/emit_ctx.hpp`) разделён на именованные зоны состояния
(`OutputState` — вывод/области имён, `EmitModeState` — режимы точки эмиссии, `DeclFacts` — факты
объявлений, `EmissionAccumulators` — инклуды/типы/линковка, `UnitState` — entry/single-file,
`ResultBinding`); счётчики временных принадлежат эмиттерам. Эмиттеры: `DeclEmitter` (`decl_emit.cpp`,
`decl_var_emit.cpp`, `decl_func_emit.cpp`, `type_decl_emit.cpp`, `destructure_emit.cpp`), `StmtEmitter`
(`stmt_emit.cpp` — блоки/области/with/try/jump, `stmt_control_emit.cpp` — if/while/do/match/switch),
`ExprEmitter` (`expr_emit.cpp` + `expr_*_emit.cpp`, `member_emit.cpp`, `ref_emit.cpp`), `ContractEmitter`.
Общие помощники — `emit/emit_common.hpp`, `emit/decl_emit_common.hpp`.

Внутренний заголовок диагностик кодогена — `emit/diag.hpp`.

## Facts and invariants

- **⚠ Переполнение (`-foverflow-check`, default ON):** классификацию решает СЕМАНТИКА
  (`Binary::m_overflowCheck`), кодоген читает и комбинирует с флагом. `true` — `+ - *` → лямбда с
  `__builtin_*_overflow`; `+= -= *=` → ternary-присваивание (без временных). Диапазон — тип ПРИЁМНИКА.
  Реакция — `throw trust::IntMinus(std::string(...))` (шаблонный ctor теряет литеральное сообщение —
  всегда явный `std::string`), заголовок `@trust/interrupt.hpp`; сообщение несёт `basename:line:`.
  `//`/`//=` защищены от деления на ноль и `INT64_MIN / -1`.
- **⚠ trap (диапазоны раскрытия макросов):** при МНОГОСТРОЧНОМ макросе call_range ставят ТОЛЬКО первый и
  последний вставляемые токены, промежуточные — invalid (НЕ в source-map); правило — ко ВСЕМ токенам,
  включая реальные аргументы (тело может переставлять аргументы, и begin > end). Одиночный макрос
  получает call_range ВСЕМ токенам (иначе ломается source-map `print`). Подстановка `@$...` — КЛОНОМ
  токена (общий TermPtr ломал разрешение типов параметров).
- **⚠ trap (`@include`/`@link` на нативной функции):** нативные импорты и нативные шаблон-типы НЕ эмитят
  C++-функцию и выходят из `generateFuncDeclToFile` ранним return; `collectInclude`/`collectLinkLib`
  обязаны вызываться ДО этих return, иначе заголовок/`-l` молча теряются.
- **⚠ Единая точка C++-имени — `cppName(trustName, CppEntityKind, moduleNamespace)`:** типы, функции и
  значения/статические члены квалифицируются, поля — без namespace, операторы — `operator<sym>`,
  нативные — C++-символ без `%`, entry — сырое имя. Модуль — ПЕРВЫЙ сегмент (`<enc>::...`) и параметр
  генератора (`CppEmitContext::cppName`), а не данные в строке; объявления эмитятся КОРОТКИМ именем
  внутри автоматического `namespace <enc>`, ссылки — `<enc>::...`. ВНЕ конверта: entry, top-level `{% %}`,
  НАТИВНЫЕ декларации и сайты импорта. `nameToCpp` получает ОДИН сегмент (EXPECT на `::`);
  namespace-сегменты — только `utils::cppNamespacePath`.
- **⚠ Имя сущности для `cppName` — от ОБЪЯВЛЕНИЯ, не от spelling ссылки:** `IdentName::text` может быть
  именем-алиасом, тогда как `resolvedDecl` — цель; identity-суффикс вызова — из выбранной перегрузки.
  Квалифицированный spelling не переписывается (его разбирает `cppName`).
- **Две формы нативных функций:** forward-decl (линковщик ищет по имени; без `::` → extern "C", с `::`
  → C++-линковка) vs импорт (`:= %native...;` — алиас, C++-функция не эмитится). `@[link]` — на импорт,
  не на forward-decl.
- **Заголовки:** `@include("name")` эмитится ТОЛЬКО с объявлением. Для нативных типов-шаблонов — путь
  preprocIncludes (on-use); для простых алиасов `::=` этот путь НЕ работает (канонизация теряет инкуды).
- **⚠ Именованные блоки-метки** для break/continue — РУЧНЫМИ goto-метками в lowering, НЕ нативный C++26
  `break label` (clang++-22 не поддерживает).
- **⚠ trap (stack-check — инжекция из накопленного реестра объявлений):** на кодогенерации скоуп-стек
  сброшен; функция ищется по ИДЕНТИЧНОСТИ объявления в `CppEmitContext::m_declFacts.stackCheck` (заполняют
  DeclEmitter и сайт импорта для функций импортируемых модулей). Атрибутная вставка оборачивает callee в
  comma-выражение `(check(), call)`; `%trust_stack_check*` перехватывает `handleStackCheckNative`.
- **⚠ trap (имя-зависимые таблицы кодогена — только по идентичности объявления):**
  `m_declFacts.nativeImports`/`m_declFacts.stackCheck`/`m_declFacts.functionNames` ключуются `declIdentity`
  (AST-узел объявления; HIR-клон нормализуется по `origin`), а не trust-текстом: одноимённые объявления
  разных модулей и разных `ns::` одного модуля не коллизируют. Факты импортируемых модулей регистрирует
  САЙТ ИМПОРТА из экспорт-набора КАЖДОЙ части (без эмиссии). Текст `@[matcher("fn")]`/
  `--stack-check-functions` — отдельный текстовый индекс: неоднозначный текст даёт диагностику,
  импортируемые функции в CLI-домене не адресуемы.
- **⚠ trap (типы на кодогенерации — ТОЛЬКО из полей узлов):** скоуп-стек сброшен; `findRegisteredType` —
  только для имён из атрибутов. Тип обязан доставлять семантика на узле (тип match-scrutinee — через
  `MatchStmt::m_tempDecl`).
- **⚠ untyped-`_` — конкретный тип по записям, НЕ std::any:** `x := _; x = <v>;` эмитит `int8_t c_x;`;
  `_` без записей/чтение до записи — уже Error в семантике; сброс `x = _;` НЕ эмитит запись.
- **Record-типы — единый `emitRecordDecl`:** Struct vs Class код НЕ различает; `static_assert`
  тривиальности — только при `isStructType`. Struct-поля — БЕЗ инициализатора (NSDMI ломает POD);
  Class-поля — NSDMI из литерала, `:= _` → без инициализатора. `!m_body` → incomplete `struct`, без
  полей/баз/assert. ⚠ C++-имя поля-типа — из РЕЕСТРА (`getCppTypeName`), имя из исходного текста
  C++-именем не является; инстанциация шаблона — на типе БЕЗ ref-битов (вид ссылки добавляет
  `wrapRefKind`, иначе `Shared<Shared<...>>`).
- **Шаблон-классы:** методы INLINE внутри `struct`; активные типовые параметры — в `CppEmitContext`
  (`ActiveTemplateParamsGuard`); конструктор — по `CallExpr::resultType`, else CTAD; POD-assert
  Struct-шаблона — только на инстанциации.
- **⚠ Лямбда-значение — отдельный `emitLambdaExpr`, НЕ `generateFuncDeclToFile`:** тип значения — из
  `FunctionTypeData` (`std::function<...>`); в `resolveCppTypeId` функциональный тип НЕ маппится как
  пользовательский алиас. Немедленный вызов `( lambda )(args)` — generic-путь `visit_CallExpr`.
- **⚠ Кодоген НЕ знает о многоточии:** семейство МАТЕРИАЛИЗУЕТ семантика — в AST готовый список;
  `visit_Filling`/`visit_Ellipsis` → FAULT.
- **⚠ Операторы — C++-имя `operator<sym>` — ЕДИНАЯ точка `cppFuncName`,** общая для кодогена и экспорта
  модуля; dunder-имя НИКОГДА не эмитится (`nameToCpp` к нему не применяется). `a[i]` с `__getitem__` →
  `(obj)[idx]` (не `.at()`). Перегруженный набор (>1 сигнатура) — explicit вызов со `static_cast`
  операндов. Свободные операторы ЭКСПОРТИРУЮТСЯ в интерфейс модуля.
- **Пользовательские `*`, `++`, `--`:** при user-типе эмитятся как `++obj`/`obj++` без `+= 1` и overflow;
  postfix dummy-параметр объявляется `operator++(int)`.
- **⚠ C++-defaults:** рендерятся в строку через временный output с подавленным mapping. В
  `m_inDefaultArg` overflow-проверки ОТКЛЮЧЕНЫ: default — не тело функции, lambda-обёртки с захватом
  недопустимы. Forward-объявление с defaults запоминается (`m_forwardFunctionDefaults`), определение
  повторный default не эмитит.
- **⚠ Вызов: `CallExpr::boundArgs` имеет приоритет над `m_args`;** `emitCallArgs` и `emitCastCallArgs`
  эмитят именно его. native-объявления C++-defaults не получают.
- **⚠ Приведения аргументов/операндов НЕ пересчитываются из сигнатуры:** читаются
  `CallExpr::argCasts`/`Binary::argCasts` (материализованы семантикой). Для Binary слоты заполняются
  только на пути `emitOperatorDispatch`. INVALID в слоте CallExpr = без приведения.
- **⚠ C++-имя функции/метода — identity-суффикс объявления (безусловно):** к `c_<name>` добавляется
  `FunctionIdentity::cppSuffix` объявленной сигнатуры, вызов — по `resolvedCalleeSuffix`/копии из
  выбранного объявления. Перегруженность имени в скоупе НЕ влияет (модули иначе рассинхронизируют
  имена). **Операторы, entry и НАТИВНЫЕ имена не манглируются;** для нативных C-функций перегрузка
  запрещена диагностикой семантики, нативное C++-имя (`%lib::f`) перегружается, но аргументы вызова
  приводятся к объявленным типам (иначе C++ выберет другую перегрузку, чем анализатор). ⚠ Внешний C++ не
  различает `using`-алиасы: два нативных объявления с одной C++-сигнатурой — `Duplicate`.
- **⚠ Статический член нативного класса — проекция ВЛАДЕЛЬЦА, не манглинг trust-имени:** факт
  `m_nativeOwner` материализует семантика; кодоген печатает `<C++-имя владельца>::<member>` и подтягивает
  его `@[include]` on-use. Один путь для плоской ссылки через `using`, вызванной статики и `Cls.field`.

## Decisions

- **`auto` у служебных временных — ОСОЗНАННО, не fallback:** он буквально повторяет C++-дедукцию
  (снимает top-level cv/ref). Ручной тип обязан воспроизвести её.

## Relations

- `transpiler/type` (C++-имена типов, include/link), `transpiler/module` (заголовки модулей),
  `ast` (узлы), `analysis/runtime_checks.hpp`, `types` (реестр/identity), `package` (`@include`/`@link`).

