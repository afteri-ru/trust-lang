# MEMORY.md

> scope: include/transpiler
> role: persistent-memory
> last_reviewed: 2026-09-18
> review_period: 30
> max_size: 26000

## Architecture

`CppTranspiler` — фасад-драйвер (наследник `KindVisitor`): публичный API, обход/диспетчеризация,
рекурсивные точки (`emitExpr`/`emitSequenceBody`/`emitBodyNode`). Mutable-состояние — в `CppEmitContext`;
тяжёлая кодогенерация — по связным эмиттерам (Type/Decl/Stmt/Expr/Contract), разделяющим контекст.
Транспилятор — ТОЛЬКО кодогенерация по «низкоуровневому» AST: всё, что требует анализа структуры
(метки goto, `;`, break/continue), зашито в AST проходом lowering (semantic/MEMORY.md).

## Facts and invariants

- **Наборы типов (`:A + :B`) — term→AST sugar, узлов не создаёт:** разворот в N `FuncDecl` делает
  `ast/type_set_expand.cpp`; `CppTranspiler::visit_TypeSet` — no-op (контракт KindVisitor). Эмиссия
  2..N развёрнутых определений идёт с подавлением source-map (`MappingSuppress` по
  `Term::m_noSourceMap` из `FuncDecl::term()`) — mapper поддерживает только 1:1 (FAULT на дубль trustKey).

- **⚠ Контроль переполнения (`-foverflow-check`, default ON):** классификацию «контролируемая
  знаковая машинная арифметика» решает СЕМАНТИКА и кладёт в `Binary::m_overflowCheck`
  (`optional<bool>`); кодоген его только читает и комбинирует с флагом. Признак `true` — знаковые
  машинные целые операнды/результат: `+ - *` (MathOp) → лямбда `[&]{ … __builtin_*_overflow … }()`;
  `+= -= *=` (AssignOp) → `__builtin_*_overflow(lhs, rhs, &lhs) ? throw : lhs` (без временных).
  Диапазон — тип ПРИЁМНИКА. Реакция — `throw trust::IntMinus(std::string(...))` (шаблонный ctor теряет
  литеральное сообщение — всегда явный `std::string`); заголовок `@trust/interrupt.hpp`. Признак `false`
  (проверка не ставится): unsigned wrap, BigInteger/Rational, не-целые, Bool-продвижение; `nullopt` —
  узел вне класса (сравнение/логика/битовые/swap/append). Сообщение несёт `basename:line:` через
  `trust::formatMessage`. Унарный минус → `__builtin_sub_overflow(cast<T>(0), x, &r)`; `//`/`//=`
  защищены от деления на ноль и `INT64_MIN / -1` → IntMinus.
- **⚠ trap (макросы: диапазоны раскрытия):** при МНОГОСТРОЧНОМ макросе (тело с `;`) call_range ставят
  ТОЛЬКО первый и последний вставляемые токены; промежуточные — invalid (НЕ регистрируются в
  source-map). Правило — ко ВСЕМ токенам, включая реальные аргументы: тело может переставлять
  аргументы (`@$cm = @$cd` при обратном порядке даёт begin > end). Одиночный макрос получает call_range
  ВСЕМ токенам (иначе ломается source-map `print`). Подстановка `@$...` — КЛОНОМ токена (общий TermPtr
  ломал разрешение типов параметров). Комментарий `## expanded macro ...` — на ПЕРВОМ термине через
  `m_docs`.
- **⚠ trap (`@include`/`@link` на нативной функции):** нативные импорты (`name := %native...;`) и
  нативные шаблон-типы НЕ эмитят C++-функцию и выходят из `generateFuncDeclToFile` ранним return;
  поэтому `collectInclude`/`collectLinkLib` обязаны вызываться ДО этих return, иначе заголовок/`-l`
  молча теряются.
- **Манглинг trust-имён — `utils::name_to_cpp`:** ASCII → префикс `c_`; юникод транслит/HEX; `::` →
  `cpp_std$$vector`. Нативные имена с ведущим `%` — уже C++-символы: `%` срезается без изменений
  (иначе ломается линковка); `cpp_to_name` восстанавливает `%` (round-trip).
- **Две формы нативных функций:** forward-decl (`%name(...):T := ...;` — линковщик ищет по имени; без
  `::` → extern "C", с `::` → C++-линковка) vs импорт (`name(...):T := %native...;` — алиас,
  C++-функция не эмитится, вызовы переписываются в `native(...)`). `@[link]` — на импорт, не на
  forward-decl.
- **Заголовки:** `@include("name")` — атрибут (без `;`); эмитится ТОЛЬКО с объявлением. Для нативных
  типов-шаблонов (structural) — путь preprocIncludes (on-use); для простых алиасов `::=` этот путь НЕ
  работает (канонизация теряет инклуды).
- **Source map для forward-decl подавляется** (`suppressMapping`): синтетическое объявление на сайте
  импорта маппится только в собственном `.cppt`. Сопоставление экспортов — по указателям термов.
- **Term-изоляция:** транспилятор не обращается к `m_term` напрямую — диапазоны/имена только через
  методы узлов (`range()`, `nameRange()`, `blockRange()`, `text()`).
- **Именованные блоки-метки** для break/continue — РУЧНЫМИ goto-метками в lowering, НЕ нативный C++26
  `break label`/`continue label` (P2552) — clang++-22 не поддерживает.
- **⚠ trap (stack-check — инжекция НЕ через `SymbolTable.resolve`):** на кодогенерации скоуп-стек
  сброшен. Функция ищется в `CppEmitContext::m_stackCheck` (заполняет DeclEmitter при эмиссии
  объявления; declare-before-use). Атрибут `@[stack_check]` — ПЕРЕД вызовом; отдельные
  `%trust_stack_check*` перехватывает `handleStackCheckNative` в visit_CallExpr. Атрибутная вставка
  оборачивает callee в comma-выражение `(check(), call)`.
- **⚠ trap (типы на кодогенерации — ТОЛЬКО через поля узлов):** скоуп-стек сброшен,
  `resolveTypeIdByName` переменные НЕ находит. Тип обязан доставлять семантика на узле
  (`Binary::resultType`, `Literal::typeId`, `VarDecl::inferredType`; тип match-scrutinee — через
  синтезированный семантикой `MatchStmt::m_tempDecl`).
- **Нативные шаблоны-типы:** `resolveCppTypeId` ветка `kNativeTemplate` → `cppTemplate<argsCpp>`
  (рекурсивно) + инклуд on-use из preprocIncludes; vector/array (объединены с `:Array`) — Array-ветка.
  `emitTypeNameForNode` для isTemplate резолвит ПОЛНУЮ инстанциацию; тип-конструктор НЕ эмитится как
  C++-функция.
- **Forward-объявление нативных классов/шаблон-классов:** C++-struct НЕ эмитится (класс определён в
  заголовке); `resolveCppTypeId` ветка `kNativeClass` → `cppName`, шаблон-класс — через
  kNativeTemplate. Доступ `obj.%method` → `obj.method`; поле `obj.%field` → `(obj).field`;
  конструктор `MyStr(...)` → `std::string(...)`; статический член `Cls.field`/`Cls.st(...)` →
  `cppName::name`.
- **⚠ untyped-`_` кодоген — конкретный тип по записям, НЕ std::any:** `x := _; x = <v>;` эмитит
  `int8_t c_x;` (`VarDecl::inferredType` заполняет widenInferredTarget); декларация `_` без записей
  либо чтение до записи — уже Error в семантике. Сброс `x = _;` НЕ эмитит запись значения.
  Типизированный `x:Type := _;` → `T c_x;`.
- **Record-типы (Struct/Class) — единый `emitRecordDecl`:** `struct c_Name [: public c_Base...]
  { поля; методы };`. Struct vs Class НЕ различаются кодом: `static_assert(is_trivial &&
  is_standard_layout)` эмитится только при `isStructType`. Struct-поля — БЕЗ инициализатора (NSDMI
  ломает POD); Class-поля — `:= <int/float-литерал>` → NSDMI `{value}`, `:= _` → без инициализатора.
  **Предварительное объявление** (`!m_body`) → `struct c_Name;` (incomplete), без полей/баз/assert.
  ⚠ C++-имя поля-типа берётся из РЕЕСТРА (дескриптор типа), а НЕ из displayName: `resolveCppTypeId`
  для record/enum/variant использует `lookup(getPointeeType(id))->name` (иначе поле-тип другого класса
  рендерилось бы как `c_<Class>.<field>`). Инстанциация шаблона рендерится `getCppTypeName` на типе
  БЕЗ ref-битов (вид ссылки добавляет `wrapRefKind` — иначе двойная обёртка `Shared<Shared<...>>`).
  Методы — через `generateFuncDeclToFile`; возможные записи в `m_exports` откатываются. Доступ
  `obj.field` → `(obj).c_field`, `obj.m(args)` → `(obj).c_m(args)`.
  - **Шаблон-классы:** `emitRecordDecl` эмитит `template <typename T, ...>` перед `struct` (методы —
    INLINE внутри struct). Аннотации `: T` рендерятся именем параметра (активные параметры — в
    `CppEmitContext`; в `resolveCppTypeId` типовой параметр обязан миновать ветку пользовательского
    алиаса). Инстанциация `Box<Int32>` → `c_Box<int32_t>` через `getCppTypeName`. Конструктор
    `Box(...)` — по `CallExpr::resultType`, else CTAD. Шаблонные базы → `: public c_Base<T>`.
    POD-assert Struct-шаблона — только на инстанциации (не на шаблоне).
- **Лямбда-значение — отдельный эмиттер `emitLambdaExpr`, НЕ `generateFuncDeclToFile`:** `visit_FuncDecl`
  при `isLambda()` эмитит C++-лямбду `[c_x](params)[ -> Ret] { body }` (захваты — `name_to_cpp`, только
  копия). Тип переменной-значения — из `FunctionTypeData` (`std::function<Ret(Args...)>`, инклуд
  `<functional>`; в `resolveCppTypeId` функциональный тип НЕ маппится как пользовательский алиас).
  Немедленный вызов `( lambda )(args)` — generic-путь `visit_CallExpr`.
- **Кодоген НЕ знает о многоточии:** семейство МАТЕРИАЛИЗУЕТ семантика — в AST приходит готовый
  список. Аргументы вызова эмитит единый `emitCallArgs(call)`, используемый и в `visit_CallExpr`, и в
  вызовах МЕТОДОВ (чтобы цикл по аргументам не дублировался). `visit_Filling`/`visit_Ellipsis` — FAULT.
- **Операторы — C++-имя `operator<sym>` — ЕДИНАЯ точка `cppFuncName` (`emit_common.hpp`),**
  используется и кодогеном (`decl_func_emit`), и экспортом модуля: `==` → `operator==`, `()` →
  `operator()`, `[]` → `operator[]`; `name_to_cpp` к символу НЕ применяется. Member — inline внутри
  `struct` (ветка `emitRecordDecl`), free — top-level. Контракты оператора (`trust_pre/trust_post`) -
  общий механизм FuncDecl. Использование: `a op b` (сравнения) и `a(5)` (вызов) — существующие пути;
  индексация `a[i]` — ветка `visit_ArrayAccess` по `lhsType`-record с `[]` эмитит `(obj)[idx]` (не
  `.at()`). Свободные операторы ЭКСПОРТИРУЮТСЯ (интерфейс модуля, `count` в `--module-info`); Trust-
  forward-decl оператора — символ в обратных кавычках (`buildTrustForwardDecl`).
- **⚠ Перегруженные функции — УНИКАЛЬНОЕ C++-имя (нельзя опираться на C++-разрешение):** при
  `FuncDecl::m_isOverloaded` кодоген добавляет к `c_<name>` детерминированный по сигнатуре суффикс
  (`semantic/overload_resolve.hpp::overloadCppSuffix`), а вызов эмитит по `CallExpr::resolvedCalleeSuffix`.
  Причина-ловушка: без уникальных имён C++ выбирает перегрузку по литералам/своим конверсиям иначе,
  чем TrustLang (`f(7:Int64)` уходил в `int32_t`-перегрузку) → тихо неверный вызов. Операторы
  (`operator<sym>`) не манглируются. Перегруженные пользовательские МЕТОДЫ — тот же суффикс у
  члена struct при объявлении и у вызова (`Binary::resolvedMethodSuffix`, ставит семантика);
  нативные методы не манглируются (имя задано внешней библиотекой). Экспорт-таблица: каждая
  перегрузка — отдельная запись `ExportEntry` с уникальным `trustName` (`f#<idx>`); C++-имя
  перегрузки УЖЕ уникально (суффикс сигнатуры), поэтому адрес `&::c_...` однозначен (без `static_cast`).

- **Сравнение типов (`<~`/`~~`/`~~~`):** `visit_CompareOp` → ветка `isTypeCheckOp` → `emitTypeCheckOp`
  эмитит константу `true`/`false` из `Binary::m_typeCheckConst` (результат свёрнут СЕМАНТИКОЙ — кодоген
  не обращается к реестру типов для проверки). При ошибке семантики узел заменён на `ErrorExpr`
  (`analyzeNodeTail`) — сюда не доходит; кодоген ничего не эмитит и НЕ дублирует диагностику.

## Decisions

- **Временные переменные создаёт ТОЛЬКО анализатор (проход lowering)** — как реальные узлы AST
  (`const VarDecl`) с уникальным именем и выведенным типом. Транспилятор — буквальный перевод AST в C++
  и НЕ синтезирует временные сам. Самостоятельное создание временной в транспиляторе — грубое
  архитектурное нарушение.
- **Исключение — синтез entry в однофайловом «скрипт»-режиме (`-fsingle-file` без `__main__`):**
  оборачивает УЖЕ существующие, семантически размеченные top-level операторы модуля в синтезируемую
  `int <модуль>__main__()` (python-подобно). Это НЕ создание временных и НЕ новый вывод. Режим активен
  только при `CppEmitContext::m_singleFileMode`.
- **Исключение — интринсик `x :=: _` (move-to-discard):** эмитит блок-локальное материализующее
  временное `{ auto __trust_discard_N = std::move(<lhs>); (void)__trust_discard_N; }` (иначе
  `std::move(x);` — no-op). Это НЕ языковая временная — низкоуровневое выражение. Имена —
  `m_discardCounter`.
- **C++-имя ссылки/backend — единый источник:** `refTypeCppName(kind, pointee[, deleter])` +
  `refTypeRuntimeIncludes(kind, sync)`; sync композируется в `getCppTypeName` по РЕАЛЬНЫМ типам реестра,
  строки C++-имён не передаются.

- **Кодоген НЕ зависит от анализатора (`transpiler → semantic` разорвана):** общие продукты анализа
  (`SymbolTable`/`SymbolIndex`) и value-типы режимов/`BehavioralModes` живут в `include/analysis`;
  поведенческие режимы (solver/stack-check) передаются в кодоген ДАННЫМИ (поле
  `CppEmitContext::m_behavioral`), а не читаются из `diag::Options`. Владелец флагов (`FlagKind`) и
  разрешение значений (`semantic::behavioralModesFromOptions`) — в `semantic`.

## Relations
