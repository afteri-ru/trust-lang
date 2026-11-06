# MEMORY.md

> scope: include/transpiler/type
> role: persistent-memory
> last_reviewed: 2026-11-12
> review_period: 30

## Architecture

`TypeEmitter` — фасад над `TypeNameResolver` (рендеринг C++-имени и ABI-проекция) и `TypeIncludeCollector`
(include/link/POD-проверки). `resolveCppTypeId` диспетчеризует по единственному дискриминатору — альтернативе
`TypeData` (`std::visit`, необработанный вариант не компилируется). `CppTypeRenderer` — номинальный
C++-рендерер (`transpiler/type`, R37): `render(TypeId/QualType)` строит
C++-представление из публичных аксессоров `TypeReadModel` (форма — `types`) и порта целевых фактов
`TypeCppFactsView` (карта фактов из `target` + языковые предикаты адресации `types`).

## Facts and invariants

- **⚠ AST state-поля — `QualType`:** для эмиссии C++-типа (const/type-level) — только QualType-перегрузки
  (`emitTypeName(QualType)`, `CppTypeRenderer::render(QualType)`); `.raw()`/TypeId-перегрузки state НЕ переносят. Потеря
  state — реальный дефект (`:Array^` эмитился как `std::vector` вместо `std::array`), проверяется
  `transpiler/codegen/array/const.src`.
- **⚠ Квалификаторы аннотации/переменной материализованы семантикой:** `IdentType::resolvedState` и
  `VarDecl::inferredType`; кодоген НЕ применяет reftype повторно.
- **C++-имя типа — два слоя (оба в кодогене):** `CppTypeRenderer::render` — НОМИНАЛЬНОЕ имя
  (`types`-данные + факты), `resolveCppTypeId` — кодоген-политику (`std::tuple`/`vector`/`array`, `trust::Tensor`,
  enum/variant, include on-use, диагностика). `resolveViaPrinter` делегирует `CppTypeRenderer`. Диспетчеризация —
  исчерпывающий `std::visit`: новый вариант не компилируется без renderer'а. `CppTypeRenderer::render` — всегда
  qualified; резолвер НЕ манглит вручную. Контракт слоёв — табличный тест `type_name_render_test.cpp`.
- **ABI-граница `trust::TypedValue` — `toRuntimeType`:** элемент несёт `QualType` целиком, identity встроенных
  и прозрачных алиасов — kind-only (`registry_index=0`, слот реестра в рантайме не воспроизводим); у
  номинальных пользовательских типов `registry_index` сохраняется. Невалидный тип элемента — `EXPECT`.
- **Абстрактные `:Tuple`/`:Range`/`:Array` без конкретики** (параметр/поле/возврат без инициализатора) — явная
  диагностика кодогена; с инициализатором — конкретный структурный тип сужением семантики.
- **Нативные шаблоны-типы:** `kNativeTemplate` → `cppTemplate<argsCpp>` + инклуд on-use; тип-конструктор НЕ
  эмитится как C++-функция.
- **Forward-объявление нативных классов:** C++-struct НЕ эмитится; C++-имя члена — проекция владельца (`obj.name` → `(obj).name`), поле —
  `(obj).field`, конструктор `MyStr(...)` → `std::string(...)`.
- **C++-имя ссылки/backend — единый источник:** `refTypeCppName(kind, pointee[, deleter])` +
  `refTypeRuntimeIncludes(kind, sync)`; sync композируется по РЕАЛЬНЫМ типам реестра, строки имён не передаются.

## Decisions

- **`CppTypeRenderer` и `TypeNameResolver` не сливаются:** `CppTypeRenderer::render` — номинальные имена,
  `resolveCppTypeId` — кодоген-политика. Новый `TypeDataKind` обязан классифицироваться обоими слоями
  (табличный тест). Оба слоя живут в кодогене (C++-рендеринг вынесен из `types`).

## Relations

- `types` (TypeId/TypeData/ref-виды/реестр), `transpiler/emit` (вызывает `TypeEmitter`),
  `runtime` (`trust/type_state.hpp` — ABI identity).
