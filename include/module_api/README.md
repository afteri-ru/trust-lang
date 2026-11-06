# module_api

Низкоуровневый interface-компонент загрузки модулей: узкий интерфейс `ModuleResolver`
(аналог Clang `ExternalASTSource`) и единая диагностика `reportModuleNotLoaded`.

Интерфейс объявлен НИЖЕ реализации (`module_loader` реализует `ModuleResolver`), поэтому
`ast`/`syntax` зависят от интерфейса, а не от фасада `session`/`module_loader` (R34).
Header-only (INTERFACE-библиотека).
