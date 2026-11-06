# MEMORY.md

> scope: include/target
> role: persistent-memory
> last_reviewed: 2026-11-13
> review_period: 30

## Architecture

`TargetContract` — per-run владелец целевых C++-фактов (вне языковой модели): `TargetInfo` (целевая
платформа: triple, стандарт C++, features, провайдер сборки), `DeclCppFacts`
(C++-имя/суффикс объявления), `TypeCppFacts` (`TypeId → {cppName, preprocIncludes, libs, cppTemplate}`),
`MethodDelegates` (делегаты нативных методов), таблица рантайм-символов (`runtime_symbols.hpp`).
Порт — `Context::target()`/`Context::declCppFacts()` (невладеющий, внедряет `Pipeline`). Компонент не
зависит от `types`/`ast`.

## Facts and invariants

- **⚠ `TargetInfo` — единственное представление целевой платформы (R1/R37/E20):** значения (triple/стандарт
  C++/features/провайдер) считаются ОДИН раз при создании контракта
  (`pipeline/run/target_options.hpp` → ctor `TargetContract`) и хранятся у владельца; стадии бэкенда читают
  `BackendRequest::target->targetInfo()`, сборка — `Pipeline::targetContract().targetInfo()`. Копий в
  артефактах/`PipelineOpts` нет; `kDefaultCppStandard` — единственный источник стандарта.
- **⚠ Хранилища целевых фактов НЕ очищаются `TypeSystem::reset()`:** контракт привязан к прогону
  (`Pipeline` создаёт новый `TargetContract` на каждый запуск), а не к реестру типов; `DeclId`/`TypeId`
  детерминированы, повторный прогон переписывает те же ключи. **Следствие:** провайдерские определения
  не «протекают» между запусками, т.к. каждый запуск получает свежий контракт; `reset()` реестра их не
  трогает.
- **⚠ `TypeCppFacts`/`DeclCppFacts`/`MethodDelegates` — единственные владельцы (R1):** адресация тип-фактов —
  по каноническому ключу `TypeStore::cppFactsKeyOf` (чистый предикат в `types`); seed интринсиков засевает
  `Pipeline` из `builtinCppSeed()`, `hasUser` отличает пользовательскую реализацию от seed. Читатели:
  порт кодогена `transpiler/type/TypeCppFactsView` (карта + языковые предикаты адресации) и отдельный путь
  семантики через `Context`. Второго хранилища нет.

## Decisions

- **`TargetContract` — контракт C++-бэкенда, а не «единственной цели навсегда»:** платформенная часть
  (`TargetInfo`: triple/features/provider) отделена от C++-фактов (`DeclCppFacts`/`TypeCppFacts`/
  `MethodDelegates`/runtime symbols). LLVM-бэкенд вводит свой контракт с тем же портом `Context::target()`;
  C++-факты он не наследует (E20/R37).
- Целевые C++-факты вынесены из `types`/`ast` в `target`: языковая модель
  целевых фактов не хранит; `types` не зависит от `target` (R34); C++-рендеринг — `transpiler`.

## Relations

- Порт: `session::Context::target()`; владелец: `pipeline::Pipeline`; читатели: `transpiler`
  (`TypeCppFactsView`), `semantic`.
