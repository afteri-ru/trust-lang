# MEMORY.md

> scope: include/types/registry
> role: persistent-memory
> last_reviewed: 2026-11-13
> review_period: 30

## Architecture

`TypeSystem` — композиционный корень (lifecycle, кеш кодов, общее ядро). Зоны: `TypeStore` (дескрипторы,
`registry_index`, identity/видовые проекции), `TypeNameIndex`, `NamedTypeRegistrar` (reserve→push→bind),
`MethodTable`, `BuiltinRegistry` (предикат абстрактного структурного контейнера), `StructuralInterner`,
`TypePrinter`, `TypeCodeCache`, `FunctionIdentity`, `BuiltinCoreBuilder` (seed ядра). Записи — только через
зону-владельца; чтение — `TypeReadModel` (view над зонами).

## Facts and invariants

- **Мутирующие примитивы зон — `private` + `friend`:** `TypeStore` ← {NamedTypeRegistrar, MethodTable,
  StructuralInterner}; `TypeNameIndex` ← {NamedTypeRegistrar, StructuralInterner}.
- **Зоны не хранят `TypeSystem&`;** `types/subst` принимает `TypeWriteContext{read, interner}` (чтение —
  модель, интернирование — только интернер, иначе цикл через агрегат).
- **⚠ `TypeDescriptor*` валиден до `reset()`** (дескрипторы в `std::deque`, адреса стабильны); `reset()` —
  к builtin-состоянию. **`reset()` инкрементирует `generation()`** — единственную точку инвалидации
  производных снимков (`SemanticModel` сверяет поколение и падает при устаревшем, R7).
- **⚠ Дескриптор вне зоны `types` читается ТОЛЬКО именованными аксессорами `TypeStore` (E1/R49):**
  `nameOf`, `methodsOf`, `methodAliasesOf`, `baseTypeOf`, `hasDescriptor`, `userTypeCount` (+ форма/базы/имя
  namespace уже существующими аксессорами). `descriptorOf`/`userDescriptors` **приватны**: адрес дескриптора
  наружу не выдаётся, поэтому прямая адресация полей вне компонента невозможна по типу, а не по соглашению.
- **`TypeCodeCache`** — кеш кодов типов (ключ `(структурный TypeId, TypeIdentityScope)`), только для
  binder-free, очищается в `reset()`; владелец — корень, `mutable` (memo).
- **⚠ C++-рендеринг живёт в кодогене, не в `types` (R37):** `TypePrinter` печатает только trust/diagnostic
  имена; номинальное C++-имя строит `CppTypeRenderer` (`transpiler/type`); порт чтения целевых фактов —
  `TypeCppFactsView` (`transpiler/type`).
- **⚠ Целевые C++-факты типа ВЫНЕСЕНЫ из `types` в `target` (R37):** `types` НЕ хранит и НЕ
  выдаёт C++-имя/include/libs/cppTemplate (`CppTypeFacts`), делегаты нативных методов (`MethodDelegates`)
  и рантайм-символы; владелец — per-run `TargetContract` (порт `Context::target()`), читатели — порт кодогена
  `TypeCppFactsView` и путь семантики через `Context`. `NativeTemplateTypeData` хранит ЯЗЫКОВОЙ ключ
  `templateOf` (C++-имя шаблона — целевой факт). **⚠ Ключ фактов (`TypeStore::cppFactsKeyOf`) — единственный
  чистый предикат адресации:** канонический для ВСТРОЕННЫХ (алиасы `String→StrChar` прозрачны), номинальный
  для пользовательских (`MyInt` — не `int32_t`); FAST-PATH ref-биты снимаются.
  `BuiltinRegistry::abstractBuiltinOf` — языковой предикат «структурный контейнер → абстрактный builtin»
  (fallback применяет читатель).
- **⚠ `TypeKey::tag` — ТИПИЗИРОВАННЫЙ дискриминатор идентичности, ВЫЧИСЛЯЕМЫЙ из `TypeData`:** одного
  списка детей (`TypeKeyChild`: TypeId + имя) недостаточно (layout слотов сигнатуры, ЯЗЫКОВОЙ ключ нативного шаблона `NativeTemplateTag`
  = абстрактный шаблон, размерности, форма тензора, маркер шаблона). Целевые C++-имена в identity НЕ входят
  (R37). Не-типовые поля НИКОГДА не сериализуются в строки; вывод — в `FunctionIdentity::slotLayout`,
  параметром API не является. Новый вариант `TypeData` не компилируется без описания не-типовых полей.
- **⚠ Интернирование НЕ прерывается на builtin-ядре:** `BuiltinTypeCore` хранит TypeKey-таблицу seed-типов,
  каждый инстанс продолжает её — иначе та же структура получила бы ВТОРОЙ TypeId.
- **preprocIncludes — ЕДИНЫЙ список; первый элемент — основной заголовок.** Ведущий `@` — quoted include.
  **⚠ Структурные контейнеры (`Array<T>`, `Range<T>`, `Tensor<T,shape>`) своей реализации НЕ несут:**
  include, `libs` и привязки методов принадлежат абстрактному builtin. Маппинг «контейнер → абстрактный
  builtin» — ЕДИНАЯ точка `BuiltinRegistry::abstractBuiltinOf` (fallback целевых фактов и methodDelegate —
  применяет читатель; provider-gate); не дублировать.
- **⚠ Целевые C++-факты типа живут в компоненте `target` (`CppTypeFacts`: `cppName`, `preprocIncludes`,
  `libs`, `cppTemplate`), не в `types`.** Пишут: seed интринсиков (`Pipeline` из `builtinCppSeed()`),
  реализация builtin (`=`-тело — семантика), регистрация нативного класса/шаблона (семантика). Чтение —
  порт кодогена `transpiler/type/TypeCppFactsView` (эффективные факты: exact, иначе нативный шаблон по
  `templateOf`, иначе abstract для контейнера) и путь семантики через `Context`. Одна реализация на TypeId:
  повтор `=` — EXPECT (ошибку выдаёт семантика; предикат `TypeCppFacts::hasUser`). Имена stdlib-модулей
  компилятору НЕ известны (`.src` — обычный trust-исходник), поэтому provider-gate их не называет.
  **⚠ Гейт кодоген-инъектируемых типов:** ref-виды читаются из `RefType`-битов `TypeId`
  (`typeRefOf(TypeId)`), а `try` (TryKind, не TypeId) — отдельным конструкт-чеком `TryCatchStmt`.
- **⚠ Общее иммутабельное ядро встроенных** строит `BuiltinCoreBuilder`; корень разделяет результат
  (`builtinCore()`), инстанс хранит только пользовательские типы. `addMethod` — только на пользовательских;
  встроенные дескрипторы иммутабельны. **⚠ Методы builtin, объявленные ШИРЕ ядрового интерфейса
  (`=`-тело модуля), идут в per-instance overlay `MethodTable` (`addBuiltinExtraMethod`;
  findMethodInfo/hasOwnMethodSignature/hasOwnMemberName учитывают overlay, `reset()` чистит).**
- **⚠ trap (встроенность/предобъявление):** `BuiltinFlag` — признак ПРЕДОБЪЯВЛЕНИЯ, ставится
  seed-регистрацией; `NamedTypeRegistrar::registerType` его СБРАСЫВАЕТ. Классификация — `isBuiltinType`
  (`index <= builtinCount`), НЕ бит и НЕ `index==0`; число встроенных — только `builtinCount`.
- **⚠ Ортогональные квалификаторы:** `getOrCreateStructuralType` требует `isUnqualifiedType(child)`;
  `getOrCreateFunctionType` нормализует TypeId-поля (FunctionTypeData — identity, не носитель state);
  константность параметров НЕ входит в структурный тип сигнатуры. `kTrustFlag` — СЕМАНТИЧЕСКИЙ
  дифференциатор (входит в ключ и TypeKind); `kHasAttrsFlag` — fast-path сравнения (оба без флага → `a==b`,
  иначе полное), на интернирование не влияет.
- **`getDesugaredTypeId` — O(1):** `canonicalType` — кеш ПРОИЗВОДНОГО от `baseType`, не второй источник
  identity; для не-алиаса — сам id, `baseType` остаётся непосредственной целью (рендер `using`).
- **`isCompleteType` = `data.has_value()`; `isForwardDecl` = `!data.has_value()`;** `Data=0` → абстрактный
  (группа), `Data≠0` → конкретный (смысл `Data` задаёт `Group`). `isBuiltinConcrete(k) == (typeDataOf(k)!=0)` —
  «есть конкретные данные», НЕ «встроенный».
- **Инициализация — `TypeSystem(ctx)` один раз при старте;** владение разделяемое: `Pipeline` и
  `SemanticModel` держат один объект (TypeId валиден после Pipeline); read-view — `read()`.
- **Структурные виды (интернируются по `TypeKey`):** `Range<Elem>` — методы объявлены ОДИН раз на абстрактном
  `:Range` (`T→Elem`); `Enum` — члены одного типа; `Variant` — гетерогенный, доступ по имени типа;
  `Array<Elem>` — абстрактный `:Array` + структурный по (elementType, dims), константность — `kConstFlag`;
  `Tensor<Elem,shape>` — по elementType+shape, sparse — отдельный вид, без провайдера/`with-torch` —
  диагностика; native-шаблон — абстрактный (`templateOf == INVALID`, C++-имя — целевой факт) + инстанциация
  (языковой ключ — `templateOf` + args); record-шаблон —
  `templateOf/templateArgs`, self-ссылка — через forward/указатели.
  ⚠ Специализация record-шаблона — **явно `TemplateSpecializationKind`** (значение НЕ выводится из
  `TypeId`/`TypeKey`): неявная инстанциация = `kImplicitInstantiation`, primary/обычный = `kNone`.
  ⚠ Создание record-инстанциации — ТОЛЬКО `StructuralInterner::getOrCreateRecordTemplateInstance`
  (единственный вход, R26): форма строится с защитой от циклов (повторный вход с тем же ключом
  `(template,args)` до регистрации — цикл; Trust-диагностика, недостоверный тип не публикуется) и с
  лимитом глубины `kMaxRecordInstanceDepth`. ⚠ Диагностика ЦИКЛА выдаётся один раз на ключ: ключ
  запоминается (`m_failedInstantiationCycles`), повторные запросы других проходов возвращают
  `INVALID_TYPE_ID` без дубля. Материализация членов
  (`materializeRecordInstanceMembers`) приватна: единственный вызывающий — `MethodTable` при поиске метода.

- **⚠ `TypeDescriptor::baseClasses` — `std::vector<BaseClass>` (`{TypeId type, Visibility access}`):**
  access — часть КАНОНИЧЕСКОЙ записи базы (нужен кодогену и анализатору). Инстанциация сохраняет access
  (`substituteTypeParams` подставляет ТОЛЬКО `type`). `BaseClass` — `types/inheritance.hpp`; уровни/
  маркеры (`Visibility`, ключевое слово, `::`/`_::`/`__::`) — единый X-macro `types/visibility.hpp`;
  читатели, которым нужен лишь тип, берут `b.type`. `baseClasses` — ОТДЕЛЬНОЕ поле дескриптора, НЕ `baseType`.
- **⚠ Форма record: единственный писатель и единственный читатель (R1 корневого `MEMORY.md`):**
  `defineRecord` — писатель, аксессоры формы — читатель: `fieldsOf`/`basesOf`/`templateParamsOf`/
  `instanceOf` на `TypeStore` (единственный путь для потребителей вне `types`); прямые обходы полей в
  потребителях запрещены и невозможны — поля `RecordTypeData` приватны. Внутри зоны `types` доступ к
  хранилищу — через `RecordTypeAccess` (`types/type/data_record_access.hpp`, internal-подкаталог).

## Relations

- `types/…` публичные заголовки/`type_system.hpp`, `types/subst`, `transpiler/type` (рендеринг),
  `session`/`Context` (невладеющий доступ), `pipeline`+`semantic/SemanticModel` (разделяемое владение).
