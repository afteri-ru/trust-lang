# MEMORY.md

> scope: include/types/type
> role: persistent-memory
> last_reviewed: 2026-11-13
> review_period: 30

## Architecture

Идентичность/данные типа: `group`, `ref_type`, `storage_key`, `site_visibility`, `method_key` и части модели данных
`data_payload`/`data_kind`/`data_key`/`data_descriptor` (umbrella — `types/type_data.hpp`).
Документы: `TYPE.md`, `CONST.md`, `REFType.md`.

## Facts and invariants

- **⚠ `MethodKey` — имя-форма метода, НЕ полная идентичность:** `name` + `isConst`/`isStatic`;
  trust-spelling (`count^`, `ns::Class::st`) разбирается ОДИН раз на границе (`methodKeyFromSpelling`),
  в хранении/поиске флаги из строки не извлекаются. Полная идентичность = owner TypeId × MethodKey ×
  `FunctionIdentity` (та же сигнатура, что у свободных функций). Нативность — свойство ЗАПИСИ
  `MethodInfo::isNative` (симметрия с предикатом `Decl::m_bindsNativeCpp`), а не ключ. `methodAliases` хранит
  bare-имя → `MethodKey` цели. `methodKeyFromSpelling` ОТВЕРГАЕТ `%` (EXPECT): имя метода trust-only,
  нативное имя — только биндинг/запись (для C++-проекции контейнеров признак выводится из дескриптора владельца).
- **⚠ C++-представление вида ссылки — ОДИН источник (`ref_type.hpp`):** `CppTypeRenderer` и
  `TypeEmitter::wrapRefKind` строят обёртку/суффикс только там (иначе виды разойдутся при добавлении backend'а).
- **⚠ Обход type-полей `TypeData` исчерпывающий:** `type_traversal.hpp` — `std::visit`; новый вариант не
  компилируется без type-полей; не-типовые поля (имена/shape/флаги) в обход не входят.
- **⚠ RefType — X-macro единый источник** (enum + имена/сиглы/строки): `&&`=unique, `&*`=shared, `&?`=weak;
  ось `ptr`/`ref` — только явный `@[reftype]`. Над АЛИАСОМ ref-вид — составной узел `getOrCreateRefType`
  (иначе `getDesugaredTypeId` потерял бы вид; C++-имя pointee «как написано»: `shared<MyInt>` →
  `trust::Shared<c_MyInt>`).
- **⚠ deleter — часть типа только для `unique`:** `@[deleter(D)]` — на владеющих видах; у `unique` D входит в
  ТИП; у `shared` D СТИРАЕТСЯ и применяется кодогеном при `adopt`.
- **⚠ Двухосевая модель:** ось ВЛАДЕНИЯ (value/shared/weak/unique) × ось ДОСТУПА (сырой `&`/`*` vs охраняемый
  `kLocker`). `kLocker` — только у reference-wrapper (`lock()`); сырые ссылки/unique локера не имеют.
  Многоуровневая ссылочность как тип разрешена, пересечение уровней в одном операторе запрещено.
- **⚠ Типы полей/элементов — `QualType`:** форма record (`RecordTypeData`, приватные поля) СОХРАНЯЕТ `const`
  объявленного поля; для `TupleTypeData::elements` состояние нормализуется к identity — в `TypeKey` входит
  только `getTypeId()`. Чтение формы — только через `RecordTypeAccess` (`types/type/data_record_access.hpp`,
  internal) внутри `types` и через аксессоры `TypeStore` снаружи.
- **⚠ Проекции TypeKind — ОДИН путь (`typeGroupOf`/`typeDataOf`/`typeRefOf`/`typeClassOf`/`typeSizeUnitOf`,
  `refAxisOf`):** overload-ы принимают `TypeKind`, `TypeId` и `QualType`; двухшаговое
  `typeGroupOf(getTypeKind(x))` в потребителях запрещено. Канонизация — ОТДЕЛЬНО (`desugaredTypeId`),
  аксессоры её не выполняют.
- **⚠ Представление `TypeId` (byte-aligned):** `registry_index` — биты 0–23, qualifier-байт — 24–31
  (const — 30, inferred — 31). `setFlag/clearFlag/testFlag(TypeId)` — ТОЛЬКО тесты; production мутирует
  состояние ЧЕРЕЗ `QualType`.
- **⚠ `TypeId`/`QualType` физически определены в `runtime/trust/type_state.hpp`** — единственное определение
  для компилятора и рантайма (`types/type_id.hpp` — только `makeTypeId` с EXPECT и `isTrustedType`). На
  ABI-границе identity встроенных — kind-only, номинальные сохраняют `registry_index`.

- **⚠ `Group` (общий ABI-лейаут с рантаймом, `runtime/trust/type_state.hpp`):** `kAuto` = 0 — ВНУТРЕННИЙ
  sentinel «тип не выведен» (нетипизированные слоты; без категории); `kAny` — ОТДЕЛЬНАЯ группа в КОНЦЕ enum
  (верхний материализуемый `std::any`). `kGroupCategoryMask` индексируется номером группы — при добавлении
  группы сдвиг существующих кодов запрещён (только дописывание; `static_assert` в `group.cpp`).

## Relations

- `types/registry` (интернирование и дескрипторы), `types/func` (идентичность сигнатур),
  `transpiler/type` (рендер C++-имени).
