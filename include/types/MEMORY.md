# MEMORY.md

> scope: include/types
> role: persistent-memory
> last_reviewed: 2026-08-28
> review_period: 30
> max_size: 13200

## Architecture

Закрытая система типов: `TypeKind` (uint32) — быстрая идентификация без обращения к реестру +
`TypeRegistry` для метаданных пользовательских/параметризованных типов. `TypeId = uint64_t
{ TypeKind (upper32) | registry_index (lower32) }`. Параметризованные типы (функции/шаблоны/массивы)
интернируются по структуре через `TypeKey` (FoldingSet, `std::unordered_map`). `TypeData =
variant<SimpleTypeData|FunctionTypeData|TemplateTypeData|ArrayTypeData|...>`; `data = nullopt` → forward
declaration, `SimpleTypeData{}` → полностью определённый без структурных данных.

## Facts and invariants

- **Краткий инвариант:** `Data=0` → абстрактный тип (группа); `Data≠0` → конкретный встроенный.
  `isBuiltinConcrete(k) == (getData(k) != 0)`.
- **⚠ TypeId — контейнер (тип | поведенческие флаги), пригодный и в рантайме:** `TypeId` несёт НЕ
  только тип (TypeKind в старших 32 + registry_index в младших), но и модифицируемые пер-Symbol
  поведенческие флаги в зарезервированных битах младшей половины (см. `kSymbolFlagsMask`:
  kInferredFlag/kConstFlag/kUninitFlag). Флаги — НЕ семантика типа. Обязательное правило:
  работа с типом и работа с флагами идут через РАЗНЫЕ слои/канонизаторы; ни один потребитель не
  вправе трактовать биты флагов как тип (тип наружу выдаётся только через `structuralType`/канонизатор,
  снимающий `kSymbolFlagsMask`), а флаги читаются/пишутся только слоем флагов на конкретном Symbol,
  НЕ через `resolvedType`. Разрозненных РУЧНЫХ масок и исторических хелперов (`withConst/typeIsConst/…`)
  нет: доступ к флагам — только через единые функции слоя `setFlag/clearFlag/testFlag/clearSymbolFlags`
  с `enum SymbolFlag{Inferred,Const,Uninit}` (Const на Symbol::type = пер-symbol константность; тип
  может нести «const в типе» тем же битом для сигнатур/прототипов, это иной слой — type-const).

- **⚠ trap (встроенность):** признак «встроенный» — бит `BuiltinFlag` в TypeKind, НЕ `registry_index==0`;
  встроенные регистрируются первыми и занимают index 1..N. `isUserDefinedType = index!=0 &&
  index > m_builtinCount` — BuiltinFlag для этого не подходит: `registerType()` копирует kind из base,
  поэтому и алиас `Integer←Int64`, и пользовательский `MyInt←Int32` имеют BuiltinFlag.
- **Ортогональные квалификаторы-флаги (НЕ часть ключа интернирования):** `kInferredFlag` (выведен) и
  `kConstFlag` (константность) — биты младшей половины; `const T` и `T` разделяют один дескриптор.
  Операции идентичности снимают флаг; `getOrCreateStructuralType` делает `EXPECT(!inferred/!const)`.
  Различает auto-Bool (продвигается в арифметике) и явный Bool (ошибка).
- **kTrustFlag — СЕМАНТИЧЕСКИЙ дифференциатор** (в отличие от inferred/const): тип с trust-условиями НЕ
  эквивалентен идентичному → бит входит в TypeKind и в ключи структурного интернирования (`TypeKey::kind`)
  — интернируются раздельно.
- **kHasAttrsFlag (bit 25) — признак «тип несёт атрибуты» (fast-path при сравнении типов).** Выставляется
  `registerType` при непустых атрибутах и у встроенных типов с признаком (markAttrs, напр. политики sync).
  `TypeRegistry::typesEqual(a,b)`: если у ОБОИХ флага нет → `a==b` (без реестра); если есть хоть у одного —
  полное сравнение через реестр (канонический тип + атрибуты + данные). Аналог kTrustFlag, НО флаг не
  влияет на структурное интернирование (набор атрибутов в TypeKey НЕ входит).
- **Встроенные политики синхронизации — типы группы `Group::kSyncPolicy` (Data=1..3)**, cppName
  `trust::SyncMutexPolicy`/`SyncRwMutexPolicy`/`SyncSingleThreadPolicy`, инклуд `@trust/trusted-cpp-sync.hpp`,
  признак kHasAttrsFlag. Имя в реестре = РЕАЛЬНОЕ имя класса (без алиасов): 2-й аргумент
  `@[reftype("shared"/"weak", <policy>)@]` резолвится через `findType`, C++-имя — из `emitTypeName` (НЕ
  строковый маппинг). Распознавание — `isSyncPolicyType(id)` (группа kSyncPolicy).
- **RefType — X-macro единый источник:** enum + `refTypeName`/`refTypeFromString` (мнемоника для
  `@[reftype(...)]`) + `refTypeCppTemplateName` (C++-имя обёртки для кодогена) генерируются из
  `TRUST_REF_TYPE_TYPES` (types/typekind.hpp); вид `kValue|kShared|kWeak|kUnique|kPtr|kMptr|kRef|kRref|kPtrPtr|kLocker`.
  Первая ссылка на безпризнаковый тип — fast-path бит; ссылка на уже ссылочный — составной узел
  `getOrCreateRefType` (группа kReftype). Имя класса-обёртки (`trust::Shared`/`Weak`/`Locker`/`std::unique_ptr`)
  в `getCppTypeName` берётся из X-macro (НЕ хардкодится в registry.cpp).
- **⚠ trap (двухосевая модель):** RefType смешивает ось ВЛАДЕНИЯ (value/shared/weak/unique) и ось ДОСТУПА
  (сырой `&`/`*` vs охраняемый `kLocker`). `kLocker` — охраняемый доступ ТОЛЬКО к reference-wrapper
  (trust::Shared/Weak, результат lock()/lock_const()); сырые ссылки/unique_ptr локера НЕ имеют (другая
  идеология — прямой доступ без guard). Null-безопасность — отдельная ось (контракт типа), не guard-объект.
- **⚠ trap (пересечение в одном операторе):** многоуровневая ссылочность как ТИП разрешена (каждый
  уровень — отдельный объявленный тип, напр. `Locker<Locker<T>>`), НО пересечение уровней в ОДНОМ
  операторе/инструкции запрещено: каждый уровень — отдельная операция/тип (сначала один, потом следующий);
  цепочечного `**`/`ref.lock().lock()` в одном выражении нет. (Единый «дескриптор доступа», если понадобится,
  — это ОТДЕЛЬНЫЙ тип, НЕ Locker.)
- **preprocIncludes — ЕДИНЫЙ список без «зависимостей»; первый элемент — основной заголовок**, остальные —
  транзитивно требуемые. Элемент с ведущим `@` (`"@trust/rational.hpp"`) — рантайм-заголовок: путь совпадает
  с именем ELF-секции внутри trust-runtime.so (#embed); транслируется в реальный `#include`, Pipeline
  извлекает секции — только для реально использованных.
- **Методы — полный ключ-имя → интернированный функциональный тип** (`TypeDescriptor::methods:
  map<string, TypeId>`); ключ кодирует нативность (ведущий `%`) и константность (хвостовой `^`), отдельных
  полей нет. `addMethod` EXPECT'ом проверяет, что алиас полностью повторяет семантику цели. const и
  не-const перегрузки — разные записи (`%get^`/`%get`).
- **Инвариант (рантайм-символ):** `RuntimeSymbol` — ТОЛЬКО не-типовые функции; единый источник —
  `runtime_symbols.hpp` (X-macro `TRUST_RUNTIME_SYMBOLS`). Заголовки: №1 — канонические типы через
  `recordUsedType` (директивы ПОСЛЕ полного обхода); №2 — по рантайм-символу (EMBED-узлы). ЕДИНСТВЕННЫЙ
  способ записи заголовков символьного механизма — `recordRuntimeSymbolHeaders(RuntimeSymbolId)`
  (строковой перегрузки нет; для EmbedExpr — substring-скан текста вставки).
- **Range:** методы объявлены ОДИН раз на абстрактном `:Range`; при резолве `Range<Elem>`
  `instantiateRangeMethod` подставляет T→Elem. `:Range` (Data=0) → `auto`, конкретный → `trust::Range<ElemCpp>`.
- **Интринсики стека** (`types/intrinsics.hpp`): `intrinsic_stack_check(N)`/`_limit()`/`_reserve(N)` с
  `@trust/stack_check.hpp`.
- **BigInteger — часть Rational:** `trust::BigInteger` (GMP, pimpl). Группа `kArbitraryPrecision`
  (как Rational; Data: Rational=1, BigInteger=2); `isArithmeticGroup` их НЕ включает. Числ./знам. Rational
  — BigInteger.
- **Единый конвертер типа целого литерала** (`intLiteralType`): 0/1(+)→Int8 (минимальный знаковый Int; НЕ Bool),
  влезающий → минимальный знаковый Int, сверхразрядный (>2^63)→BigInteger; знак-учёт (`-129`→Int16,
  `-9223372036854775808`→Int64). Bool из числа 0/1 — ТОЛЬКО явная аннотация `0 :Bool`/`1 :Bool`
  (DSL `true`/`false` → `1:Bool`/`0:Bool`); в `x:Int64 := <huge>` — ошибка «exceeds range».
- **Статическая типизация литералов (`literal :Type`):** постфиксная аннотация → `Literal::typeAnnotation`;
  0/1 :Bool — явный Bool, :Rational/:BigInteger — в т.ч. сверхразрядный, :Int8..:Int64/:UInt — с проверкой
  влезания (Int64 — точная граница INT64_MAX). `num\den` всегда Rational.
- **Арифметика BigInteger/Rational — строгая, отдельная ветка:** Big op Big→Big, Rat op Rat→Rat, Big op
  Rat→Rat, X op машинное целое→X; X op float → ОШИБКА (нужен явный каст); `//` на Big/Rat → ОШИБКА.
  Ранее давали std::any.
- **Разделители разрядов `_`** срезаются единым хелпером `stripDigitSeparators` (и в кодогенерации —
  целевой clang не принимает `10_000`); `parseDecimalUInt` base 0.
- **Нативные шаблоны-типы:** объявление регистрирует АБСТРАКТНЫЙ шаблон (Group::kNativeTemplate,
  NativeTemplateTypeData{cppTemplate}); использование `vector<Int32>` интернирует конкретную
  инстанциацию. Совпадение C++-имени со встроенным контейнером (`std::vector`/`std::array` от `:Array`) —
  ОБЪЕДИНЯЕТСЯ с `:Array`, а не создаёт отдельный тип (мягкая диагностика).
- **Forward-объявление нативных классов** (`String ::= %std::string {...};`): Group::kNativeClass +
  NativeClassTypeData{cppName}, `registerNativeClass` (trust-имя → cppName, preprocIncludes из @[include],
  on-use). C++-struct не генерируется; cppName — в данных типа (манглинг НЕ используется). Методы/поля-
  интерфейс — в TypeDescriptor::methods (как у встроенных). Шаблон-класс переиспользует kNativeTemplate.
- **Array-литерал в типизированную Array-цель:** коэрция элемента литерала к типу элемента цели (только
  расширение; сужение — checkAssignmentNarrowing).
