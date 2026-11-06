# ModuleLoader - загрузка и реестр модулей

## Назначение

Оркестрация загрузки модулей и реестр загруженных исходников. Предмет компонента — только ИСХОДНИКИ:
`parseSourceModule()` рекурсивно вызывает `Parser` и сразу конвертирует Term→AST. Публичный API —
`module_loader/module_loader.hpp`, `import_exports.hpp`, `module_export.hpp`.

## Особенности

- **Чистая политика пути** (резолв `\file`/`\\group`/`\\\pkg`, форма файл/каталог, обход группы,
  каноникализация, `TRUST_PACKAGE_PATH`) — `path/module_path_policy`; без `Context` и диагностик.
- **Пакеты** — `package/PackageStore` (кэш `<packageCacheDir>/<name>-<md5-12>/`).
- **Установка интерфейса файла** (штамп модуля, блок интерфейса, реэкспорт, экспорт-набор) —
  `interface/interface_install` в фиксированном порядке шагов; `import_exports` связывает сайты импорта
  с файлами; реестр модулей — `registry/module_registry`.
- **Разделение с `syntax`/`ast`:** они НЕ зависят от загрузчика (иначе `no_module_loader_guard`); ввод/вывод
  имён идёт через `module_api/module_resolver.hpp`.
- Реализация — internal-подкаталоги `path/`, `package/`, `interface/`, `registry/`. Инварианты — `MEMORY.md`.
