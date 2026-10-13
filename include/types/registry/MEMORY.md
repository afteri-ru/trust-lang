# MEMORY.md

> scope: include/types/registry
> role: persistent-memory
> last_reviewed: 2026-11-05
> review_period: 30
> max_size: 8000

## Architecture

`TypeSystem` — композиционный корень: владеет зонами, lifecycle, кешем кодов и общим ядром.
Зоны: `TypeStore` (дескрипторы, `declaredRecords`, `registry_index`, identity/видовые проекции),
`TypeNameIndex` (привязки имён + дубликат-диагностика), `NamedTypeRegistrar` (reserve→push→bind),
`MethodTable` (сигнатуры/алиасы методов), `BuiltinRegistry` (встроенные реализации, рантайм-символы,
include/link), `StructuralInterner` (TypeKey-интернирование), `TypePrinter` (печать),
`TypeCodeCache` (memo кодов), `FunctionIdentity` (идентичность сигнатур),
`BuiltinCoreBuilder` (seed ядра).
Запись — только через зону-владельца; чтение — `TypeReadModel` (view над зонами, он же `read()` на корне).

## Facts and invariants

- **Мутирующие примитивы зон — `private` + `friend`** (запись — через зоны-владельцы). Friends:
  `TypeStore` ← {NamedTypeRegistrar, MethodTable, StructuralInterner}; `TypeNameIndex` ←
  {NamedTypeRegistrar, StructuralInterner}; `BuiltinRegistry::setCore` ← TypeSystem.
- **Зоны не хранят `TypeSystem&`**: `StructuralInterner(read, store, names, diag, opts)`,
  `MethodTable(store, printer, codes)`, `TypePrinter(store, builtins)`, `FunctionIdentity(store,
  printer, codes)`. `types/subst` принимает `TypeWriteContext{read, interner}`: чтение — модель,
  интернирование — только интернер (иначе цикл через агрегат).
- **`TypeDescriptor*` валиден до `reset()`:** дескрипторы живут в `std::deque` (адреса стабильны), но
  `reset()` возвращает реестр к builtin-состоянию.
- **Кэш канонических кодов типов** — зона `TypeCodeCache` (ключ `(structural TypeId, TypeIdentityScope)`),
  только для binder-free типов, очищается в `reset()`; владелец — корень, `mutable` (memo).
- **Печать C++-имени** (`kCpp`) — номинальное имя из реестра; кодоген-политика —
  `transpiler/type/type_name_resolver` (единый `CppTypeRenderer` НЕ сливается).

- **⚠ `TypeKey::tag` — ТИПИЗИРОВАННЫЙ дискриминатор идентичности, ВЫЧИСЛЯЕМЫЙ из `TypeData`:** плоских
  children/names недостаточно (layout слотов сигнатуры, C++-имя шаблона, размерности, форма тензора,
  маркер шаблона). Не-типовые поля идентичности НИКОГДА не сериализуются в строки. Выводится в
  `FunctionIdentity::slotLayout`, параметром API не является. Новый вариант `TypeData` не компилируется,
  пока не описаны его не-типовые поля (`std::visit`).
- **⚠ Интернирование НЕ прерывается на builtin-ядре:** `BuiltinTypeCore` хранит TypeKey-таблицу
  seed-структурных типов, и каждый инстанс продолжает её — иначе та же структура получила бы ВТОРОЙ TypeId.
- **preprocIncludes — ЕДИНЫЙ список; первый элемент — основной заголовок.** Ведущий `@` — quoted include.
- **⚠ Общее иммутабельное ядро встроенных типов** строит зона `BuiltinCoreBuilder`; корень лишь
  разделяет результат (`TypeSystem::builtinCore()`), инстанс хранит только пользовательские типы.
  `addMethod` — только на пользовательских типах; встроенные дескрипторы иммутабельны.
- **⚠ trap (встроенность/предобъявление):** `BuiltinFlag` — признак ПРЕДОБЪЯВЛЕННОГО типа; ставится
  seed-регистрацией (`BuiltinCoreBuilder`/`registerBuiltinAlias`); `NamedTypeRegistrar::registerType`
  его всегда СБРАСЫВАЕТ. Классификация — `isBuiltinType` (`index <= BuiltinTypeCore::builtinCount`),
  НЕ бит и НЕ `index==0`; число встроенных — единственный источник `builtinCount`.
  `BuiltinRegistry::define(read, name, ...)` привязывает реализацию к существующему TypeId.
- **⚠ Ортогональные квалификаторы:** `getOrCreateStructuralType` требует `isUnqualifiedType(child)`, а
  `getOrCreateFunctionType` нормализует TypeId-поля (FunctionTypeData — identity data, не носитель state);
  константность параметров НЕ входит в структурный тип сигнатуры. `kTrustFlag` — СЕМАНТИЧЕСКИЙ
  дифференциатор (входит в ключ и TypeKind); `kHasAttrsFlag` — fast-path сравнения (нет флага у ОБОИХ →
  `a==b`, иначе полное сравнение), на интернирование НЕ влияет.
- **`getDesugaredTypeId` — O(1):** `TypeDescriptor::canonicalType` — кеш ПРОИЗВОДНОГО от `baseType`, не второй
  источник identity; для не-алиаса возвращается сам id, `baseType` остаётся НЕПОСРЕДСТВЕННОЙ целью (рендер `using`).
- **`isCompleteType` = `data.has_value()`; `isForwardDecl` = `!data.has_value()`;** `Data=0` → абстрактный тип
  (группа), `Data≠0` → конкретный (смысл `Data` задаёт `Group`, у встроенных — размер bits).
  `isBuiltinConcrete(k) == (getData(k) != 0)` — «есть конкретные данные», НЕ «встроенный».
- **Инициализация — `TypeSystem(ctx)` один раз при старте;** владение разделяемое (`shared_ptr`):
  `Pipeline` и `SemanticModel` держат один объект (TypeId в SymbolInfo/LSP валиден после Pipeline);
  read-view — `read()` у владельца.
- **Структурные виды (интернируются по `TypeKey`):** `Range<Elem>` — методы объявлены ОДИН раз на
  абстрактном `:Range` (`T→Elem`), абстрактный C++-имени не имеет; `Enum` — члены одного типа;
  `Variant` — гетерогенный, доступ по имени типа; `Array<Elem>` — абстрактный `:Array` (Data=0) +
  структурный (Data=1) по (elementType, dims), константность — `kConstFlag`; `Tensor<Elem,shape>` —
  по elementType+shape, sparse — отдельный вид, без провайдера/`with-torch` — явная диагностика;
  нативный шаблон — абстрактный (Data=0, `cppTemplate`) + инстанциация (Data=1); record-шаблон —
  `templateOf/templateArgs`, self-ссылка в полях — через forward/указатели.
  ⚠ Специализация record-шаблона классифицируется явно (`TypeDescriptor::specializationKind`,
  `TemplateSpecializationKind`): значение НЕ выводится из `TypeId`/`TypeKey` (интернированный ключ не
  кодирует семантический статус); неявная инстанциация = `kImplicitInstantiation`, primary-шаблон/обычный
  record = `kNone`, explicit = резерв (S5).

## Relations

- `types/…` публичные заголовки/`type_system.hpp`, `types/subst`, `transpiler/type` (рендеринг),
  `session`/`Context` (невладеющий доступ), `pipeline`+`semantic/SemanticModel` (разделяемое владение).
