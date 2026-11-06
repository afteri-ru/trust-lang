# Session - фасад Context

## Назначение

Фасад `Context` объединяет сервисы, разделяемые всеми стадиями обработки одного исходника:
`SourceMapWriter`, `DiagnosticEngine`, `Options`, `AttrPool`, загруженный `Macro`, а также
невладеющие ссылки на `TypeSystem` и `ModuleResolver` (реализация — `ModuleLoader`).

## Особенности

- **Владение:** `SourceMapWriter`, `DiagnosticEngine`, `Options`, `AttrPool`, `Macro` — owned
  (`unique_ptr`/`shared_ptr`). `TypeSystem` и загрузчик модулей — НЕ owned: создаются и владеются
  `Pipeline`; в `Context` внедряются `setTypes` и `setModuleResolver` (невладеющие указатели;
  избегает циклов `diag↔types`/`diag↔module_loader`).
- **Счётчики и реестры:** счётчики макросов/гигиены/блоков, реестр макроопределений и доки
  макросов прогона (`macroDocs`); значения доков принадлежат пулу строк этого же `Context`.
- **`report<T>`:** convenience-обёртка над `DiagnosticEngine::report` с severity из `Options`.
- `Context` non-copyable.
