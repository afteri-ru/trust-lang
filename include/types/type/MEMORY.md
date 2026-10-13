# MEMORY.md

> scope: include/types/type
> role: persistent-memory
> last_reviewed: 2026-11-05
> review_period: 30
> max_size: 4600

## Architecture

Идентичность/данные типа: `group`, `ref_type`, `storage_key`, `site_visibility` (реализация публичных
заголовков корня) и части модели данных `data_payload`/`data_kind`/`data_key`/`data_descriptor`/`data_builtin`
(umbrella — `types/type_data.hpp`). Документы: `TYPE.md`, `CONST.md`, `REFType.md`.

## Facts and invariants

- **⚠ C++-представление вида ссылки — ОДИН источник:** `ref_type.hpp`; и `TypeSystem::getCppTypeName`,
  и `TypeEmitter::wrapRefKind` обязаны строить обёртку/суффикс только там (иначе виды разойдутся при
  добавлении backend'а).
- **⚠ Обход type-полей `TypeData` исчерпывающий:** `type_traversal.hpp` использует `std::visit` —
  новый вариант не компилируется, пока для него не описаны type-поля; не-типовые поля
  (имена/shape/cppTemplate/флаги) в обход не входят.
- **⚠ RefType — X-macro единый источник** (enum + имена/сиглы/строки): `&&`=unique, `&*`=shared, `&?`=weak;
  ось `ptr`/`ref` — только явный `@[reftype]`, короткие маркеры — локальная зона. Над АЛИАСОМ ref-вид —
  составной узел `getOrCreateRefType` (иначе `getDesugaredTypeId` потерял бы вид, а C++-имя pointee обязано
  быть «как написано»: `shared<MyInt>` → `trust::Shared<c_MyInt>`).
- **⚠ deleter — часть типа только для `unique`:** `@[deleter(D)]` допустим только на владеющих видах; для
  `unique` D входит в ТИП; для `shared` D СТИРАЕТСЯ и применяется кодогеном при `adopt`.
- **⚠ Двухосевая модель:** RefType смешивает ось ВЛАДЕНИЯ (value/shared/weak/unique) и ось ДОСТУПА
  (сырой `&`/`*` vs охраняемый `kLocker`). `kLocker` — только у reference-wrapper (результат `lock()`);
  сырые ссылки/unique локера не имеют. Многоуровневая ссылочность как ТИП разрешена, пересечение уровней
  в ОДНОМ операторе запрещено.
- **⚠ Типы полей/элементов — `QualType`:** `RecordTypeData::fields` СОХРАНЯЕТ `const` (доезжает до
  типизации доступа и кодогена; запись в const-поле — диагностика semantic). Для `TupleTypeData::elements`
  состояние нормализуется к identity — в `TypeKey` входит только `getTypeId()`.

- **⚠ Представление `TypeId` (byte-aligned):** `registry_index` — биты 0–23, qualifier-байт — 24–31
  (const — bit 30, inferred — bit 31). `getUnqualifiedType` снимает весь qualifier-байт.
  `setFlag/clearFlag/testFlag(TypeId)` — ТОЛЬКО тестовые хелперы; production мутирует состояние
  ИСКЛЮЧИТЕЛЬНО через `QualType`.
- **⚠ `TypeId`/`QualType` физически определены в `runtime/trust/type_state.hpp`** — единственное определение
  для компилятора и рантайма (`types/type_id.hpp` добавляет только `makeTypeId` с EXPECT и `isTrustedType`).
  На ABI-границе identity встроенных приводится к kind-only, номинальные сохраняют `registry_index`.

## Relations

- `types/registry` (интернирование и дескрипторы), `types/func` (идентичность сигнатур),
  `transpiler/type` (рендер C++-имени).
