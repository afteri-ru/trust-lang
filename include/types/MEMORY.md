# MEMORY.md

> scope: include/types
> role: persistent-memory
> last_reviewed: 2026-09-17
> review_period: 30
> max_size: 36400

## Architecture

Закрытая система типов: `TypeKind` (uint32) — быстрая идентификация без обращения к реестру, +
`TypeRegistry` для метаданных пользовательских/параметризованных типов. `TypeId = uint64
{TypeKind(upper32) | registry_index(lower32)}`. Параметризованные типы интернируются по структуре
через `TypeKey` (FoldingSet). `TypeData` — variant; `data = nullopt` → forward declaration,
`SimpleTypeData{}` → полностью определённый без структурных данных.

## Facts and invariants

- **Краткий инвариант:** `Data=0` → абстрактный тип (группа); `Data≠0` → конкретный встроенный;
  `isBuiltinConcrete(k) == (getData(k) != 0)`.
- **⚠ TypeId — контейнер (тип | поведенческие флаги):** TypeKind (старшие 32) + registry_index
  (младшие) + модифицируемые пер-Symbol флаги в резервных битах младшей половины
  (`kSymbolFlagsMask`: kInferredFlag/kConstFlag/kUninitFlag). Флаги — НЕ семантика типа. Тип наружу
  выдаётся только через `structuralType`/канонизатор (снимает маску); флаги читаются/пишутся только
  слоем флагов на конкретном Symbol. Ручных масок/хелперов нет.
- **⚠ trap (встроенность):** признак «встроенный» — бит `BuiltinFlag`, НЕ `registry_index==0`;
  встроенные регистрируются первыми. `isUserDefinedType = index!=0 && index > m_builtinCount` —
  BuiltinFlag для этого не подходит: `registerType()` копирует kind из base, поэтому и алиас
  `Integer←Int64`, и пользовательский `MyInt←Int32` имеют BuiltinFlag.
- **Ортогональные квалификаторы-флаги (НЕ часть ключа интернирования):** kInferredFlag (выведен) и
  kConstFlag (константность) — биты младшей половины; `const T` и `T` разделяют один дескриптор;
  `getOrCreateStructuralType` делает `EXPECT(!inferred/!const)`. Различает auto-Bool и явный Bool.
- **kTrustFlag — СЕМАНТИЧЕСКИЙ дифференциатор:** входит в TypeKind и в ключи структурного
  интернирования (типы интернируются раздельно).
- **kHasAttrsFlag (bit 25) — признак «тип несёт атрибуты» (fast-path при сравнении):** выставляется
  `registerType` при непустых атрибутах и у встроенных с признаком. `typesEqual(a,b)`: если у ОБОИХ
  флага нет → `a==b` (без реестра); иначе — полное сравнение через реестр. Аналог kTrustFlag, НО на
  интернирование НЕ влияет (набор атрибутов в TypeKey НЕ входит).
- **Встроенные политики доступа — группа `Group::kAccessPolicy`**, cppName `trust::Access*`. Политики
  — ТОЛЬКО для `shared`/`weak`: Data 1..3 (`AccessMutex`/`AccessRwMutex`/`AccessSingleThread`); Data=4
  — обёртка `AccessShared` (НЕ политика). Пользовательская политика задаётся лишь для
  shared/weak; `unique` монополен — политика ФИКСИРОВАНА (эксклюзивный доступ), не параметризуется. Имя в реестре = РЕАЛЬНОЕ имя класса.
- **⚠ trap (deleter = часть типа для unique):** `@[deleter(D)]` допустим только на владеющих видах;
  для `unique` D входит в ТИП (`RefTypeData::deleterType`; `TypeKey.children={pointee,deleter}` ⇒
  `unique<T,D1> != unique<T,D2>`); для `shared` D СТИРАЕТСЯ (применяется только кодогеном при
  `adopt`). Встроенные deleter-типы — `Group::kDeleterPolicy`. ⚠ `getCppTypeName` для нативного класса
  берёт cppName из `NativeClassTypeData`.
- **RefType — X-macro единый источник:** enum + имена/строки/сиглы генерируются из X-macro
  (`TRUST_REF_TYPE_TYPES`/`SIGILS`): `&&`=unique, `&*`=shared, `&?`=weak (нативная ось `ptr`/`ref` —
  только явный `@[reftype]`). ⚠ Короткие маркеры — ТОЛЬКО локальная зона; на границе API допустим
  лишь явный `@[reftype]`. C++-имя вида — единообразно через `refTypeCppName`. Fast-path бит — ТОЛЬКО
  для не-алиасов; **⚠ над АЛИАСОМ ref-вид — составной узел `getOrCreateRefType`:** иначе
  `getCanonicalTypeId` развернул бы `baseType` и потерял вид, а C++-имя pointee обязано быть «как
  написано» (`shared<MyInt>`→`trust::Shared<c_MyInt>`, `shared<Integer>`→`trust::Shared<int64_t>`).
- **⚠ trap (двухосевая модель):** RefType смешивает ось ВЛАДЕНИЯ (value/shared/weak/unique) и ось
  ДОСТУПА (сырой `&`/`*` vs охраняемый `kLocker`). `kLocker` — охраняемый доступ ТОЛЬКО к
  reference-wrapper (Shared/Weak, результат lock()); сырые ссылки/unique локера НЕ имеют.
  Null-безопасность — отдельная ось (контракт типа).
- **⚠ trap (пересечение в одном операторе):** многоуровневая ссылочность как ТИП разрешена, НО
  пересечение уровней в ОДНОМ операторе запрещено (каждый уровень — отдельная операция; цепочечного
  `**`/`ref.lock().lock()` нет). Единый «дескриптор доступа», если понадобится — ОТДЕЛЬНЫЙ тип.
- **preprocIncludes — ЕДИНЫЙ список без «зависимостей»; первый элемент — основной заголовок.**
  Элемент с ведущим `@` — рантайм-заголовок (путь = имя ELF-секции внутри trust-runtime);
  извлекается on-use.
- **Методы — полный ключ-имя → НАБОР интернированных сигнатур** (`TypeDescriptor::methods`,
  `std::vector<TypeId>`; перегрузки по типам аргументов): ключ кодирует нативность (ведущий `%`) и
  константность (хвостовой `^`). `addMethod` ДОБАВЛЯЕТ сигнатуру (точный дубль сигнатуры — EXPECT;
  ДРУГОЙ ключ с тем же bare-именем+константностью — EXPECT «одна форма имени»); алиас повторяет
  семантику цели; const/не-const — разные записи. `findMethodInfo` → `MethodRef{key, signatures}`;
  отдельного `findMethod` (single) НЕТ — выбор перегрузки делает семантика (`types/overload_resolve.hpp`).
- **Инвариант (рантайм-символ):** `RuntimeSymbol` — ТОЛЬКО не-типовые функции; единый источник —
  X-macro. ЕДИНСТВЕННЫЙ способ записи заголовков символьного механизма —
  `recordRuntimeSymbolHeaders(RuntimeSymbolId)` (строковой перегрузки нет).
- **Range:** методы объявлены ОДИН раз на абстрактном `:Range`; при резолве `Range<Elem>` подставляется
  `T→Elem`. `:Range` (Data=0) → `auto`.
- **BigInteger — часть Rational:** `trust::BigInteger` (GMP, pimpl); группа `kArbitraryPrecision`;
  `isArithmeticGroup` их НЕ включает; числ./знам. Rational — BigInteger.
- **Единый конвертер типа целого литерала:** 0/1(+)→Int8 (НЕ Bool), влезающий → минимальный знаковый
  Int, сверхразрядный (>2^63)→BigInteger; знак-учёт. Bool из числа 0/1 — ТОЛЬКО явная аннотация
  (`0 :Bool`); в `x:Int64 := <huge>` — ошибка «exceeds range».
- **Статическая типизация литералов (`literal :Type`):** постфиксная аннотация; `:Rational`/
  `:BigInteger` — в т.ч. сверхразрядный; `:Int8..:Int64`/`:UInt` — с проверкой влезания (Int64 —
  точная граница INT64_MAX). `num\den` всегда Rational.
- **Арифметика BigInteger/Rational — строгая, отдельная ветка:** Big op Big→Big, Rat op Rat→Rat,
  Big op Rat→Rat, X op машинное целое→X; X op float → ОШИБКА (нужен явный каст); `//` на Big/Rat →
  ОШИБКА.
- **Разделители разрядов `_`** срезаются единым хелпером (и в кодогенерации — целевой clang не
  принимает `10_000`).
- **Нативные шаблоны-типы:** объявление регистрирует АБСТРАКТНЫЙ шаблон (`kNativeTemplate`);
  использование `vector<Int32>` интернирует конкретную инстанциацию. Совпадение C++-имени со
  встроенным (`std::vector`/`std::array` от `:Array`) — ОБЪЕДИНЯЕТСЯ с `:Array`, а не создаёт
  отдельный тип (мягкая диагностика).
- **Forward-объявление нативных классов** (`String ::= %std::string {...};`): `Group::kNativeClass` +
  `NativeClassTypeData{cppName}`; C++-struct не генерируется; cppName — в данных типа (манглинг НЕ
  используется). Шаблон-класс переиспользует kNativeTemplate.
- **Array-литерал в типизированную Array-цель:** коэрция элемента (только расширение; сужение —
  диагностика).
- **Классификация вида ссылки — единые хелперы:** `RefAxis` + `refAxisOf`, `isSupportedRefKind`
  (`rref`/`ptrptr` зарезервированы, но НЕ реализованы — отвергаются диагностикой), `isNativeRefKind`/
  `isSmartRefKind`/`refKindCompatible`.
- **`kBorrow` (вид заёма) УДАЛЁН:** оператор `&` у монопольного `unique` ЗАПРЕЩЁН (`& u` → ошибка);
  `&` определён только для `shared` (→ weak). Зависимости данных (view-объекты) отслеживает атрибут
  `@[borrowed]`, НЕ вид ссылки.
- **`refTypeCppName`/`refTypeRuntimeIncludes` — ЕДИНЫЙ источник C++-имени и рантайм-заголовков
  ссылки:** sync-backend (`AccessShared<T,Policy[,Impl]>` / `Weak<...>`) композируется в
  `TypeRegistry::getCppTypeName` по РЕАЛЬНЫМ типам реестра; строки C++-имён через API не передаются.
- **`RefTypeData{pointeeType, deleterType, accessPolicyType, implType}`:** `deleterType` (unique),
  `accessPolicyType` (ТОЛЬКО shared/weak) и `implType` (shared/weak; 3-й аргумент `reftype`) — ЧАСТЬ
  типа, входят в `TypeKey.children`; при них вид ВСЕГДА структурный узел. Политика/реализация
  резолвятся из аргументов `@[reftype]` и сохраняются в типе. Валидация impl по интерфейсу —
  C++-концепт.
- **`replaceKind(TypeId, TypeKind)`** — перенос TypeKind с сохранением сырых нижних 32 бит; заменяет
  ручные маски `0xFFFFFFFF` (fast-path applyRefType, withTrusted, спец-случай StrChar+ptr+const).
- **Вид эксклюзивного владения — `unique`** (`RefType::kUnique`). МОНОПОЛЕН: без guard'а и без
  заёма/alias (только move/swap). ⚠ C++-имя: без deleter'а → `trust::StaticUnique<T>` (inline,
  move-only, zero-cost); `@[deleter(D)]` → `trust::Unique<T,D>`. Модель жизненного цикла (§14
  `types/REFType.md`): владение × доступ × регион × deleter × readonly × pin. `@[pin]`/`@[lifetime]`
  — built-in, analyzer-only; контракт `@[lifetime(<area>[,<name>])]` (area ∈ self/param/var/named/
  program/thread/scope/_; name обязателен ⟺ param/var/named; `_` = элизия) и применимость (только
  Native-ось) валидирует `validateLifetimeArgs`; правила применимости — единая матрица
  `checkQualifierApplicable`. `@[pin]` не реализован → явная ошибка; квалификаторы не поддерживаются
  на возвращаемом типе функции (кроме `reftype`).
- **⚠ Record-типы (Struct/Class) — единая модель `RecordTypeData`:** НЕ алиасы (`baseType=INVALID`,
  canonical = сам тип). Struct и Class различаются ТОЛЬКО группой TypeKind (Data=1); признак POD
  кодогенератора = `isStructType` (группа, а не бит-флаг). Наследование — `TypeDescriptor::baseClasses`
  (ОТДЕЛЬНО от `baseType`!); абстрактные маркеры `:Any`/`:Struct`/`:Class` туда НЕ попадают.
  `findField`/`findMethodInfo` обходят `baseClasses` (с защитой от циклов).
- **⚠ Пользовательские шаблон-классы (`<T> :Box ::= :Class{...}`) — надстройка над
  `RecordTypeData`:** абстрактный шаблон — Data=0 + `templateParams` (тот же `Group::kTemplateParam`,
  но Data=1 и различаются ИМЕНЕМ в `TypeKey.names`); инстанциация — Data=1, `templateOf` +
  `templateArgs`, `children={templateId}+args` (templateId в детях ОБЯЗАТЕЛЕН — иначе `Box<Int32>` и
  `Pair<Int32>` совпали бы). Поля/базы/методы подставлены `substituteTypeParams`. `getCppTypeName`
  (инстанциации) → `c_Box<int32_t>`. Struct-шаблоны поддержаны; POD static_assert — на инстанциации.
- **⚠ trap (реестр мутирует при интернировании):** `getOrCreate*` пушат в `m_descriptors` →
  реаллокация. Указатели/ссылки (`descriptorOf`, `baseClasses`, `recordData->fields`) НЕЛЬЗЯ держать
  между вызовами `getOrCreate*`/`substituteTypeParams` — снимать нужные данные в локальные копии ДО
  рекурсии (иначе висячие указатели/segfault).
- **⚠ Record-типы объявляются и доопределяются раздельно:** `declareRecord` создаёт НЕПОЛНЫЙ тип
  (`descriptor.data == std::nullopt`; типовые параметры объявленного шаблона — во временной карте
  `m_declaredRecords`), `defineRecord` заполняет data (поля/базы/`templateParams`) и удаляет запись;
  повторное *определение* — ошибка. Признак «определён» ВЫВОДИТСЯ как отсутствие записи в карте
  (отдельного bool-поля/предиката нет); `isRecordTemplate`/`recordTemplateParams` — из data либо карты.
  Так имя record видно в собственном теле (self-ссылки) и работает forward-доопределение;
  self-инстанциация шаблона до его определения даёт инстанс с пустыми полями.
- **⚠ trap (`substituteTypeParams` и fast-path ref):** подстановка обязана разбирать тип с ref-БИТОМ
  (первая ссылка без узла `RefTypeData`) ДО веток по `TypeData` — иначе `shared<T>` в шаблон-поле НЕ
  подставляется, а ref-бит на record-инстанциации маскирует ветку (теряется вид ссылки).
- **Функциональный тип (`FunctionTypeData`) → C++ `std::function<Ret(Args...)>`:** INVALID returnType =
  `void`, INVALID тип параметра = `std::any`; инклуд `<functional>` выдаётся `getPreprocInclude(s)`
  (проверка — по данным типа, не по дескриптору). Кодоген обязан отличать функциональный тип от
  пользовательского алиаса (иначе манглинг displayName).
- **Реестр операторов (`types/operator_registry.hpp`) — ЕДИНЫЙ источник `символ ↔ C++ operator<sym>`:**
  X-macro `TRUST_OPERATOR_IMPLEMENTED` (`OperatorInfo` = spelling + арность + allow_member/allow_free).
  Лексическая поверхность оператора — backtick-символ (лексема REFLECTION); реестр используют и семантика
  (валидация), и кодоген (эмиссия `operator<sym>`). Символ вне реестра — «not implemented» (объявлять
  нельзя); `()`/`[]` — member-only (требование C++).

## Decisions

## Relations
