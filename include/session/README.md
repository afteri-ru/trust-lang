# Session - фасад Context

## Назначение

Фасад `Context` объединяет сервисы, разделяемые всеми стадиями обработки одного исходника:
`SourceMapWriter`, `DiagnosticEngine`, `Options`, `AttrPool`, загруженный `Macro`, а также
невладеющие ссылки на `TypeRegistry` и `ModuleLoader`.

## Особенности

- **Владение:** `SourceMapWriter`, `DiagnosticEngine`, `Options`, `AttrPool`, `Macro` — owned
  (`unique_ptr`/`shared_ptr`). `TypeRegistry` и `ModuleLoader` — НЕ owned: создаются и владеются
  `Pipeline`, внедряются через `setTypes`/`setLoader` (избегает цикла `diag↔types`/`diag↔module_loader`).
- **Счётчики и реестры:** счётчики макросов/гигиены/блоков, реестр макроопределений, глобальное
  хранилище доков макросов (`macroDocs`), индекс текущего модуля.
- **`report<T>`:** convenience-обёртка над `DiagnosticEngine::report` с severity из `Options`.
- `Context` non-copyable.
