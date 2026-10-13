# MEMORY.md

> scope: include/types
> role: persistent-memory
> last_reviewed: 2026-11-05
> review_period: 30
> max_size: 7200

## Architecture

Закрытая система типов: `TypeKind` (uint32) — идентификация без реестра; `TypeSystem` — метаданные
пользовательских/параметризованных типов. `TypeId` — identity-view, `QualType` — qualifier-view того же raw.
`TypeKind`/`TypeId`/`QualType` определены ОДИН раз в `runtime/trust/type_state.hpp` (общий ABI-заголовок);
параметризованные типы интернируются по структуре (`TypeKey`).

**Встроенный тип — предобъявление + отдельная реализация:** предрегистрируется при создании реестра,
реализация — ОПРЕДЕЛЕНИЕ `=` (`::=` для встроенного имени запрещён). Нет реализации при кодогенерации —
«реализация встроенного типа X не определена; загрузите провайдер Y», НЕ «тип X неизвестен».

Публичный API (корень): `typekind.hpp`, `type_id.hpp`, `type_data.hpp` (umbrella частей
`types/type/data_*.hpp`), `group.hpp`, `type_system.hpp`, `intrinsics.hpp`, `runtime_symbols.hpp`, `types.hpp`,
умбреллы — `call.hpp` (вызов), `names.hpp` (домен имён).

| internal-подкаталог | MEMORY | ответственность |
|---|---|---|
| `types/call/` | `call/MEMORY.md` | `resolveCall`, сигнатуры методов, binder'ы, унификация шаблонов, продвижение арифметики, операторы, целые литералы |
| `types/func/` | `func/MEMORY.md` | `FunctionIdentity`: идентичность сигнатур, `cppSuffix`, набор перегрузок |
| `types/names/` | `names/MEMORY.md` | `StorageKey`, видимость на сайте импорта, индекс имён типов |
| `types/registry/` | `registry/MEMORY.md` | состояние/печать реестра, ядро встроенных типов, интернирование, структурные виды |
| `types/subst/` | `subst/MEMORY.md` | подстановка типовых параметров, унификация, ref поверх `TemplateParam` |
| `types/type/` | `type/MEMORY.md` | `data_*.hpp`, `Group`, `RefType`, представление `TypeId`, `preprocIncludes` |

## Facts and invariants

- **⚠ `TypeId` — identity-view, `QualType` — qualifier-view:** `TypeId` — ключ интернирования/перегрузок;
  `QualType` — identity + const/inferred, НЕ ключ, без `operator==`/`std::hash` (`raw()` — полный state,
  `getTypeId()` — lossy проекция). Квалификаторы НЕ входят в ключ (`const T`/`T` делят дескриптор);
  представление (layout, флаги) — `type/`, `registry/`.
- **⚠ Три отношения identity — единая точка `identityOf(TypeId, TypeIdentityScope)`:** `kStructural` (ключи
  интернирования), `kSemantic` (встроенные алиасы раскрыты, пользовательские номинальны), `kCanonical`
  (корень alias-цепочки — C++-представление); `kCppInterop` — `kCanonical` + раскрытие алиасов на каждом
  уровне. Глубокая эквивалентность — только `FunctionIdentity::typeCode`.
- **⚠ Пользовательский алиас (`Name ::= <base>`) — НОВЫЙ номинальный тип Trust** (отдельный `TypeId`; в C++ —
  `using`), встроенные алиасы прозрачны. **`:A ::- T;` — ВТОРОЕ имя того же `TypeId`** (display-имя и
  C++-проекции — у цели). Отсюда `MyInt`/`Int32` — разные перегрузки, `Array<MyInt>`/`Array<Int32>` — разные типы.
- **⚠ Три константности не смешивать:** type-level `kConstFlag` (→ `const T` в имени), declaration
  `attr::ReadOnly` (→ `const ` в объявлении + запрет записи), became-const (flow-факт DA). `const_cast` —
  только явный const-вызов `obj.method^()`.
- **⚠ Имена типов: storageKey ≠ display name** (ключ СТРУКТУРНЫЙ, текст в ключ не кодируется); `nominalCppName`
  ставит модульный `<enc>` первым сегментом (без `ModuleNameLookup` — FAULT). Детали — `names/`.
- **⚠ Единый `resolveCall` для ВСЕХ форм вызова**; `:Any` — конкретный тип, `INVALID`-аргумент Any-like.
- **⚠ `TypeReadModel` (`types/type_read.hpp`):** READ-ONLY view зон; зона — через `read().store()/
  names()/printer()/builtins()/methods()`, на модели — только `getType`/`findType`/`forEachType`
  и `identity()`. Берётся через `Context::readTypes()`/`TypeSystem::read()`,
  мутации — только через зоны записи.
- **⚠ RefType:** X-macro единый источник (`&&`/`&*`/`&?`), над алиасом — составной узел, deleter — часть типа
  только для `unique`, двухосевая модель (владение × доступ). Детали — `type/`.
- **`RuntimeSymbol`** — только не-типовые функции (X-macro; запись — `recordRuntimeSymbolHeaders`).
  **BigInteger — часть Rational** (GMP, pimpl), `kArbitraryPrecision`; `isArithmeticGroup` их НЕ включает.

## Decisions

- **Алиас — привязка ИМЕНИ, а не свойство типа:** alias-метаданные живут в объявлении/analysis-таблицах, не
  в TypeId; в рантайме алиасов нет (C++-имя нормализовано на цель, alias-метаданные не эмитятся).
- **Единая точка C++-идентичности сигнатур — `FunctionIdentity`;** binder-контекст передаёт семантика,
  `FunctionIdentity` AST-независима. Детали — `func/`.
- **Native-контракт:** анализатор проверяет все операторы/методы; C++-делегация — только native free
  C-linkage single-signature; native-имена (включая статические члены) не манглируются.
## Relations

- `semantic` (`compare`/`sameCppOverload`/`cppSuffix`), `transpiler` (`m_cppSuffix`/`resolvedCalleeSuffix`/
  `resolvedMethodSuffix`/`resolvedOperatorSignature`, `getCppTypeName`), `runtime` (`type_state.hpp` —
  единственное определение `TypeId`/`QualType`), `symbols` (binder-контекст).
