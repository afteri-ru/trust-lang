# MEMORY.md

> scope: include/transpiler/type
> role: persistent-memory
> last_reviewed: 2026-11-04
> review_period: 30
> max_size: 7000

## Architecture

`TypeEmitter` (`type/type_emit.hpp`) — фасад над `TypeNameResolver` (`type/type_name_resolver.hpp`,
рендеринг C++-имени и ABI-проекция) и `TypeIncludeCollector` (`type/type_include_collector.hpp`,
include/link/POD-проверки). `TypeNameResolver::resolveCppTypeId` диспетчеризует по единственному
дискриминатору — альтернативе `TypeData` (`std::visit`, необработанный вариант не компилируется).

## Facts and invariants

- **⚠ AST state-поля — `QualType`:** для эмиссии C++-типа (const/type-level qualifiers) — только
  QualType-перегрузки (`emitTypeName(QualType)`, `getCppTypeName(QualType)`); `.raw()`/TypeId-перегрузки
  state НЕ переносят. Потеря state — реальный дефект (`:Array^` эмитился как `std::vector` вместо
  `std::array`), проверяется lit `transpiler/codegen/array/const.src`.
- **⚠ Квалификаторы аннотации/переменной материализованы семантикой:** `IdentType::resolvedState`
  (const/reftype/policy/impl) и `VarDecl::inferredType` (для ссылочных переменных); кодоген НЕ
  применяет reftype повторно.
- **C++-имя типа — два слоя:** `printType(kCpp)` даёт НОМИНАЛЬНОЕ имя из реестра, `resolveCppTypeId` —
  кодоген-политику (`std::tuple`/`std::vector`/`std::array`, `trust::Tensor`, enum/variant, include
  on-use, диагностика). Диспетчеризация — исчерпывающий `std::visit` по `TypeData`: новый вариант
  не компилируется без C++-renderer'а. `getCppTypeName` — кодоген-проекция (всегда qualified);
  `TypeNameResolver` НЕ манглит имена вручную. Контракт слоёв — табличный тест
  `type_name_render_test.cpp` и `transpiler/README.md`.
- **ABI-граница `trust::TypedValue` — `TypeNameResolver::toRuntimeType`:** элемент словаря несёт `QualType`
  целиком, а identity встроенных типов и прозрачных алиасов приводится к kind-only (`registry_index=0`),
  т.к. слот реестра компилятора в рантайме не воспроизводим; у номинальных пользовательских типов
  `registry_index` сохраняется. Невалидный тип элемента — `EXPECT`.
- **Абстрактные `:Tuple`/`:Range`/`:Array` без конкретики** (параметр/поле/возврат без инициализатора) —
  явная диагностика кодогена; переменная с инициализатором получает конкретный структурный тип
  сужением семантики в `VarDecl::inferredType`.
- **Нативные шаблоны-типы:** `kNativeTemplate` → `cppTemplate<argsCpp>` (рекурсивно) + инклуд on-use;
  `emitTypeNameForNode` резолвит ПОЛНУЮ инстанциацию; тип-конструктор НЕ эмитится как C++-функция.
- **Forward-объявление нативных классов:** C++-struct НЕ эмитится; доступ `obj.%method` → `obj.method`,
  поле `obj.%field` → `(obj).field`, конструктор `MyStr(...)` → `std::string(...)`.
- **C++-имя ссылки/backend — единый источник:** `refTypeCppName(kind, pointee[, deleter])` +
  `refTypeRuntimeIncludes(kind, sync)`; sync композируется по РЕАЛЬНЫМ типам реестра, строки C++-имён не
  передаются.

## Decisions

- **Единый `CppTypeRenderer` не сливается:** `printType(kCpp)` — номинальные имена из реестра,
  `resolveCppTypeId` — кодоген-политика. Новый вариант `TypeDataKind` обязан быть классифицирован
  обоими слоями (табличный тест).

## Relations

- `types` (TypeId/TypeData/ref-виды/реестр), `transpiler/emit` (вызывает `TypeEmitter`),
  `runtime` (`trust/type_state.hpp` — ABI identity).
