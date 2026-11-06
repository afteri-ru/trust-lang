# MEMORY.md

> scope: include/pipeline/modules
> role: persistent-memory
> last_reviewed: 2026-11-10
> review_period: 30

## Architecture

Модульные юниты (`modules/modules.cpp`): `buildModuleRoot`, `runModuleUnit`, `generateModuleOutputs`,
`runCppToFile`; из HIR одного модуля строятся оба артефакта — `<enc>.hpp` и `.cppt`.

## Facts and invariants

- **⚠ Модульные юниты — codegen-проекции:** `lowerModuleUnit` — ЕДИНСТВЕННАЯ точка понижения тела
  модуля; из её HIR строятся ОБА артефакта — `<enc>.hpp` и `.cppt`. Заголовок получает КАЖДЫЙ модуль
  (в т.ч. declaration-only), `.cppt` — только при out-of-line определениях. Header публикует РОВНО
  экспорт-набор: forward остаётся forward, полное определение в интерфейсе — полностью; определение
  из тела за forward'ом интерфейса не подставляется, а также ТЕЛА generic/constexpr free-функций
  (`bodyMustBeInModuleHeader`); `HeaderArtifact::publishedDeclarations` — определения, которые `.cppt`
  не повторяет (иначе redefinition), и `.cppt` начинается с `#include` своего заголовка.
- **⚠ `emitModuleHeader` пропускает ЧУЖИЕ узлы** (`entry.node->module != idx` — реэкспорт): они живут в
  заголовке владельца, а в `.hpp` фасада попадают umbrella-`#include` заголовков владельцев.
- **⚠ Имя `.hppt` = `moduleHeaderFileName(moduleEncodedName(moduleLogicalName(idx)))`;** `modulePath()` — путь ФС, для формулы
  не годится (encoder вернёт пусто ⇒ EXPECT). `generateModuleOutputs` — публичный: его вызывают
  драйвер `builddriver`, `--emit-cpp` и LSP (иначе `#include` клиента не разрешается). Семантику повторно не
  запускаем: тело конвертирует ЗАГРУЗЧИК, `buildLoweredHir` идёт по готовым фактам главного прогона.

## Relations

- `module_loader` (экспорт-набор/проекции), `transpiler` (module_emit), `toolchain` (сборка),
  `pipeline/run` (главный прогон).
