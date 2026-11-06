# MEMORY.md

> scope: include/transpiler/emit
> role: persistent-memory
> last_reviewed: 2026-11-13
> review_period: 30

## Architecture

Кодогенерация: `CppEmitContext` разделён на именованные зоны состояния (вывод/области имён, режимы точки
эмиссии, факты объявлений `DeclFacts`, накопленные инклуды/типы/линковка, entry/single-file, привязка
результата); счётчики временных принадлежат эмиттерам. Эмиттеры — `DeclEmitter`/`StmtEmitter`/`ExprEmitter`/
`ContractEmitter` (по зонам выражений/объявлений). Внутренние диагностики кодогена — `emit/diag.hpp`.

Зависимости кодогена — порт `CodegenServices` (= единый порт прогона `RunServices`, `session/run_services.hpp`),
а НЕ `Context`: реестр типов
(`TypeReadModel`), целевой контракт/`DeclCppFacts`, пул атрибутов, опции, карта (`SourceMapWriter`) и
диагностика (`DiagnosticEngine`) передаются по ссылкам; `CppTranspiler`/`CppEmitContext` конструируются из
порта. Владелец портов — оркестратор (`Pipeline`); тесты собирают порт `codegenServices(ctx)`.

## Facts and invariants

- **⚠ Поведенческие флаги кодогена приходят ДАННЫМИ:** `-foverflow-check`/`-fcomments`/`-Wbacktrace`
  разрешает `transpiler::codegenModes` (единственная функция) в точке сборки прогона; `CppEmitContext`
  читает только `m_behavioral` и НЕ обращается к `diag::Options`/`FlagKind` (R39).
- **⚠ Порядок POD-проверок — порядок первого использования типа** (обход HIR = порядок объявлений):
  производные артефакты байт-стабильны и не зависят от индексной identity `TypeId` (R42).
- **⚠ Идентичность объявлений в `DeclFacts` — `DeclId` (R23), не адрес узла:** синтетические узлы без id
  факт не регистрируют (для объявления реального модуля отсутствие id — `EXPECT`). Публикация заголовка —
  множество `DeclId` (alias-`TypeDecl` несёт собственный id, у типа с телом id принадлежит
  `ClassDecl`/`RecordDecl`).
- **⚠ Переполнение (`-foverflow-check`, default ON):** классификацию решает СЕМАНТИКА
  (`Binary::m_overflowCheck`), кодоген читает и комбинирует с флагом. `+ - *` → лямбда с
  `__builtin_*_overflow`; `+= -= *=` → ternary-присваивание (без временных). Диапазон — тип ПРИЁМНИКА.
  Реакция — `throw trust::IntMinus(std::string(...))` (шаблонный ctor теряет литеральное сообщение),
  сообщение несёт `basename:line:`.
- **⚠ trap (диапазоны раскрытия макросов):** при МНОГОСТРОЧНОМ макросе call_range — ТОЛЬКО первому и
  последнему токенам, промежуточные invalid (НЕ в source-map), включая реальные аргументы (тело может
  переставлять их, begin > end). Одиночный макрос — call_range ВСЕМ токенам (иначе ломается source-map
  `print`). Подстановка `@$...` — КЛОНОМ токена (общий TermPtr ломал разрешение типов параметров).
- **⚠ trap (`@include`/`@link` на нативной функции):** нативные импорты/шаблон-типы НЕ эмитят C++-функцию
  (ранний return из `generateFuncDeclToFile`); `collectInclude`/`collectLinkLib` обязаны вызываться ДО этих
  return, иначе заголовок/`-l` молча теряются.
- **⚠ Вариант наследования эмитит `cppClassFrame`:** базы — `std::vector<CppBase>{name, access}`;
  печать `: <kw> Base` в порядке перечисления. Источник: Record — реестр (`BaseClass.access`),
  native/builtin — AST (`NativeBase`/`TypeBase.access`). Дефолт — Public для ВСЕХ типов;
  private/protected — только явным маркером (у builtin-реализаций с базисами `::` пишется явно).
- **⚠ Единая точка C++-имени — `cppName(trustName, CppEntityKind)`:** типы, функции и
  значения/статические члены квалифицируются namespace-путём, поля — без namespace, операторы — `operator<sym>`, нативные —
  C++-имя объявления из `DeclCppFacts::cppNameOf` (владелец `target`) без манглинга, entry — сырое имя. Модуль в C++-имя НЕ входит (изоляция — области
  имён программиста, см. root MEMORY). `nameToCpp` получает ОДИН сегмент (EXPECT на `::`);
  namespace-сегменты — `utils::cppNamespacePath`.
- **⚠ Имя сущности для `cppName` — от ОБЪЯВЛЕНИЯ, не от spelling ссылки:** `IdentName::text` может быть
  именем-алиасом, `resolvedDecl` — цель; identity-суффикс вызова — из выбранной перегрузки. Квалифицированный
  spelling не переписывается.
- **Две формы нативных функций:** импорт (`:= %cpp ...;` — внешний символ, C++-декларация не эмитится)
  vs экспорт (`@[export("name")]` — trust-реализация под C/C++-именем). `@[link]` — только на импорт.
- **Заголовки:** `@include("name")` эмитится ТОЛЬКО с объявлением. Для нативных типов-шаблонов — preprocIncludes
  (on-use); для простых алиасов `::=` этот путь НЕ работает (канонизация теряет инклуды).
- **⚠ Именованные блоки-метки** для break/continue — РУЧНЫМИ goto-метками в lowering, НЕ нативный C++26
  `break label` (clang++-22 не поддерживает).
- **⚠ trap (stack-check — инжекция из накопленного реестра объявлений):** на кодогенерации скоуп-стек
  сброшен; функция ищется по ИДЕНТИЧНОСТИ объявления в `CppEmitContext::m_declFacts.stackCheck` (заполняют
  DeclEmitter и сайт импорта для функций импортируемых модулей). Атрибутная вставка оборачивает callee в
  comma-выражение `(check(), call)`; `trust_stack_check*` перехватывает `handleStackCheckNative`.
  `@[stack_check(N)]` семантика проверяет лишь на «цифры» — величину за пределами `long` кодоген НАСЫЩАЕТ.
- **⚠ trap (имя-зависимые таблицы кодогена — только по идентичности объявления):**
  `m_declFacts.nativeCppNames`/`stackCheck`/`functionNames` ключуются `declIdentity` (AST-узел; HIR-клон
  нормализуется по `origin`), а не trust-текстом: одноимённые объявления разных модулей и разных `ns::`
  одного модуля не коллизируют. Факты импортируемых модулей регистрирует САЙТ ИМПОРТА из экспорт-набора
  КАЖДОЙ части (без эмиссии). Текст `@[matcher("fn")]`/`--stack-check-functions` — отдельный текстовый
  индекс: неоднозначный текст даёт диагностику, импортируемые функции в CLI-домене не адресуемы.
- **⚠ trap (типы на кодогенерации — ТОЛЬКО из полей узла):** скоуп-стек сброшен; `findRegisteredType` — только
  для имён из атрибутов. Тип обязан доставлять семантика на узле (тип match-scrutinee — через
  `MatchStmt::m_tempDecl`).
- **⚠ untyped-`_` — конкретный тип по записям, НЕ std::any:** `x := _; x = <v>;` эмитит `int8_t c_x;`;
  `_` без записей/чтение до записи — уже Error в семантике; сброс `x = _;` НЕ эмитит запись.
- **Record-типы — единый `emitRecordDecl`:** Struct vs Class код НЕ различает; `static_assert` тривиальности —
  только при `isStructType`. Struct-поля — БЕЗ инициализатора (NSDMI ломает POD); Class-поля — NSDMI из
  ЛЮБОГО значения по умолчанию (один путь `renderExprToString`, как default-аргумент функции; свёртки
  «только Int/Float» нет), `:= _` → без инициализатора. `!hasBody()` → incomplete `struct`, без полей/баз/assert.
  ⚠ C++-имя поля-типа — из РЕЕСТРА, не из текста; инстанциация шаблона — на типе БЕЗ ref-битов
  (`wrapRefKind` добавляет вид ссылки, иначе `Shared<Shared<...>>`).
  ⚠ Порядок/`MapperRange`/инициализатор/`visibility` — из AST-членов; типы и C++-имена полей — через
  аксессор формы реестра; связь «член AST ↔ поле формы» — по КЛЮЧУ `DeclId` (R14); параллельные
  векторы/склейка по индексу запрещены (R1/R14).
- **⚠ ОДИН генератор C++-класса — `emitCppClass(CppClassSpec)`:** Record, обёртка нативного типа и
  генерируемый builtin-класс различаются ТОЛЬКО данными спеки (keyword/namespaces/bases/preamble/
  поля-из-реестра/members/trailer). Члены эмитятся ЕДИНЫМ обходом в ПОРЯДКЕ ОБЪЯВЛЕНИЯ (метод может
  стоять до поля): поле — из формы реестра по КЛЮЧУ (`DeclId` поля), метод — forward/header-guard/
  `generateFuncDeclToFile`. При смене `AstNodeBase::visibility` печатается секция
  `public:/protected:/private:` (`emitAccessSection`; спецификатор — с начала строки, `outputAtLineStart`).
  `out` сливается в вывод перед каждым методом —
  offsets source-map считаются от `output_body(idx).size() − mapStackTop().outputBegin.offset()`.
  ⚠ offset имени для source-map выводится из фактически напечатанного текста-префикса: сигнатура функции —
  `prefix = <templateHeader><lead><ret> ` (name = `prefix.size()`, параметры — `+ name + 1 +` позиция в
  `paramsStr`); каркас класса/enum/variant — `cppClassFrame::nameOffset`; «extern »/«struct »/«using » —
  из того же текста, что эмитится.
  Вывод без маркеров не меняется (секция печатается только при смене уровня, старт — Public).
  ⚠ Тот же `cppClassFrame` печатает и Enum/Variant (их члены — не FuncDecl/VarDecl, поэтому общий только
  каркас `struct <name>[: base]`); рантайм-базис `trust::Enum<...>` — `CppBase.omitAccessKeyword` (без `public`).
- **Шаблон-классы:** методы INLINE внутри `struct`; активные типовые параметры — в `CppEmitContext`
  (`ActiveTemplateParamsGuard`, RAII — обязателен даже для record); конструктор — по `CallExpr::resultType`, else CTAD;
  POD-assert Struct-шаблона — только на инстанциации. ⚠ Шапка `template <...>` И критерий
  «объявление — шаблон» (функция/record/класс-обёртка) — ЕДИНЫЙ `cppTemplateParams(const
  std::optional<std::vector<AstNodePtr>>&)` из AST `m_templateParams`; шаблонность = НЕПУСТЫЕ
  `names` (НЕ `has_value`/`!empty()`: nullopt, пустой список и пустой/нулевой параметр-узел — не
  шаблон). Один критерий обязателен, иначе эмиссия шапки и out-of-line-проход расходятся (выбор
  «тело в заголовке»). Имена не манглируются (их видит `TypeEmitter` при рендере `:T`).
- **Параметр/возврат:** C++-тип параметра и возврата рендерятся ЕДИНЫМИ `resolveParamCppType`/
  `resolveReturnCppType` для функций И лямбд (аннотация → Void/None/Any/const/pack) — две политики
  расходились (лямбда молча подставляла Any). Плюс ЕДИНЫЕ хелперы: `splitCppName` (`ns::name`),
  `requiresCppLinkage`, `cppNamespaceOpen|Close`, `cppClassFrame` (каркас — Record/обёртка/Enum/Variant).
  ⚠ Объявление печатает ЛОКАЛЬНОЕ C++-имя из trust-текста (`cppDeclName`) либо из нативного C++-имени
  (`splitCppName`) — единый кодек C++-имени класса.
- **⚠ Лямбда-значение — отдельный `emitLambdaExpr`, НЕ `generateFuncDeclToFile`:** тип значения — из
  `FunctionTypeData` (`std::function<...>`); в `resolveCppTypeId` функциональный тип НЕ маппится как
  пользовательский алиас. Немедленный вызов `( lambda )(args)` — generic-путь `visit_CallExpr`.
- **⚠ Кодоген НЕ знает о многоточии:** семейство МАТЕРИАЛИЗУЕТ семантика — в AST готовый список;
  `visit_Filling`/`visit_Ellipsis` → FAULT.
- **⚠ Операторы — C++-имя `operator<sym>` — ЕДИНАЯ точка `cppName(name, CppEntityKind::kOperator)`/
  `CppEmitContext::operatorCppName`,** общая для кодогена и экспорта модуля; dunder-имя НИКОГДА не эмитится (`nameToCpp` к нему не применяется). `a[i]` с `__getitem__` →
  `(obj)[idx]`, не `.at()`. Перегруженный набор (>1 сигнатура) — explicit вызов со `static_cast` операндов.
  Свободные операторы ЭКСПОРТИРУЮТСЯ в интерфейс модуля.
- **Пользовательские `*`, `++`, `--`:** при user-типе эмитятся как `++obj`/`obj++` без `+= 1` и overflow;
  postfix dummy-параметр объявляется `operator++(int)`.
- **⚠ C++-defaults:** рендерятся в строку через временный output с подавленным mapping. В `m_inDefaultArg`
  overflow-проверки ОТКЛЮЧЕНЫ: default — не тело функции, lambda-обёртки с захватом недопустимы. Тот же путь
  (`renderExprToString`, НЕ `emitExpr`) — у default-аргумента лямбды (`emitLambdaExpr`).
  Forward-объявление с defaults запоминается (`m_declFacts.forwardFunctionDefaults`) по C++-имени,
  определение повторный default не эмитит. ⚠ Ключ — ИМЯ, не идентичность: это отношение ДВУХ разных
  объявлений (forward и определение), у которых нет общего узла.
- **⚠ Члены Enum/Variant:** единый `dictElements`, сверяется с реестром через `EXPECT`; декларация члена —
  единый `emitStaticConstMember`, out-of-class определение — единый `appendStaticConstDefinition`, разбор
  значения-узла (литеральность/безнарный член/диагностика) — единый `resolveEnumVariantMemberValue`.
  Значения Enum — из реестра (автоинкремент — семантика), Variant — из
  AST-литерала (реестр хранит только тип); имя — ВСЕГДА из реестра. POD-assert — `podAssertText` (общий с
  инстанциациями Struct-шаблонов).
- **⚠ Вызов: `CallExpr::boundArgs` приоритетнее `m_args`;** его эмитят `emitCallArgs`/`emitCastCallArgs`.
  native-объявления C++-defaults не получают.
- **⚠ Приведения аргументов/операндов НЕ пересчитываются из сигнатуры:** читаются `CallExpr::argCasts`/
  `Binary::argCasts` (материализованы семантикой). Для Binary слоты заполняются только на пути
  `emitOperatorDispatch`. INVALID в слоте CallExpr = без приведения.
- **⚠ C++-имя функции/метода — identity-суффикс объявления (безусловно):** к `<name>` добавляется
  `FunctionIdentity::cppSuffix` объявленной сигнатуры, вызов — по `resolvedCalleeSuffix`. Перегруженность имени
  в скоупе НЕ влияет (модули иначе рассинхронизируют имена). **Операторы, entry и НАТИВНЫЕ имена не
  манглируются;** нативная цель (`%lib::f`) перегружается, но аргументы приводятся к объявленным типам
  (иначе C++ выберет другую перегрузку). ⚠ Внешний C++ не различает `using`-алиасы: два нативных объявления
  с одной C++-сигнатурой — `Duplicate`.
- **⚠ Статический член нативного класса — проекция ВЛАДЕЛЬЦА, не манглинг trust-имени:** факт `m_nativeOwner`
  материализует семантика; кодоген печатает `<C++-имя владельца>::<member>` и подтягивает его `@[include]`
  on-use. Один путь для плоской ссылки через `using`, вызванной статики и `Cls.field`.

- **⚠ Реализация builtin ГЕНЕРИРУЕТ класс,** если объявлены БАЗИСЫ (`Name = %cpp, %base {...}`) либо есть член с
  телом (`analysis::builtinImplGeneratesCppClass`): `%member`-привязки объявлений НЕ эмитятся, C++-имя
  (`trust::X`) задаёт namespace, include типа — заголовок модуля-владельца, out-of-line тела — в его
  `.cppt`, forward-декларации — отдельный проход заголовка. **⚠ Собственные ПОЛЯ такой класс не
  объявляет** (состояние — из нативных базисов): поле в теле → `FAULT` в `emitNativeClassWrapper`
  (типы полей `emitCppClass` берёт из реестра, у обёртки их нет).

## Decisions

- **`auto` у служебных временных — ОСОЗНАННО, не fallback:** он буквально повторяет C++-дедукцию (снимает
  top-level cv/ref). Ручной тип обязан воспроизвести её.

## Relations

- `transpiler/type` (C++-имена типов, include/link), `transpiler/module` (заголовки модулей), `ast` (узлы),
  `analysis/runtime_checks.hpp`, `types` (реестр/identity), `package` (`@include`/`@link`).
