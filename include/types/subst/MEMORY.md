# MEMORY.md

> scope: include/types/subst
> role: persistent-memory
> last_reviewed: 2026-11-05
> review_period: 30
> max_size: 4000

## Architecture

Подстановка типовых параметров для шаблонов: `template_subst` (`substituteTemplateParams`,
`substituteTemplateArgList`) — internal-подкаталог `types`; публичный контракт унификации —
`types/call.hpp`, привязки — `TemplateBindings`. Все функции подстановки принимают один
`TypeWriteContext{read, interner}` (`types/subst/write_context.hpp`): чтение структуры — `read`,
интернирование результата — `interner`.

## Facts and invariants

- **⚠ pack-expansion `T...` — отдельный структурный узел `PackExpansion(pattern)`:** `Group::kEllipsis`
  занят runtime-вариадиками, смешивать нельзя. Привязки — `TemplateBindings` (non-pack = ровно один тип,
  pack = набор). Подстановка — `substituteTemplateParams` (pack в одиночной позиции → INVALID), списковый
  splice — `substituteTemplateArgList`. `paramPackType/paramPackPosition` — отдельные слоты, НЕ элементы
  `paramTypes`.
- **⚠ Идентичность типового параметра = (owner, depth, index) через `TemplateParamTag`; имя — только
  диагностика.** `owner==INVALID` → не-scoped (generic-функции), интернируется по имени — прежнее
  поведение. Record-шаблоны объявляют параметры с owner = TypeId записи (`declareRecord` → params →
  `setRecordTemplateParams`): одинаковые имена в РАЗНЫХ шаблонах — разные TypeId (owner-scoping,
  C++-модель TemplateTypeParmDecl); подстановка/унификация работают по равенству TypeId внутри шаблона.
- **⚠ подстановка ветки FunctionTypeData ОБЯЗАНА сохранять pack-слот/позицию и `kTrustFlag`.**

## Relations

- `types/template_unify` (публичная унификация), `types/registry` (TypeData/TypeKey), `types/type_traversal`.

- **⚠ Унификация типовых параметров — строгая:** завершённые поддеревья сравниваются через
  `sameSemanticType`; числовые конверсии — ПОЗЖЕ (по инстанцированной сигнатуре). `collectTemplateParams`
  обходит type-поля ЕДИНЫМ исчерпывающим `types/types.hpp` (`std::visit`, compile-time
  exhaustiveness); списковость позиции выводится из вида РОДИТЕЛЯ. Все типовые параметры объявленной
  сигнатуры обязаны быть выведены.
- **⚠ fast-path ref поверх TemplateParam:** `applyRefType(T, kind)` ставит RefType-биты в `TypeKind`,
  группа остаётся `kTemplateParam`, поэтому `isTemplateParamType(shared<T>)` истинно. TypeId-перегрузки —
  identity-only (state снимается), QualType-перегрузки сохраняют state. В `collectTemplateParams` и
  `unifyTemplateType` ref-вид и `kMptr` проверяются ДО template-param — иначе `T` не выводится (или
  выводится в ключ `shared<T>` вместо `T`).
