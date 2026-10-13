# MEMORY.md

> scope: include/module_loader/registry
> role: persistent-memory
> last_reviewed: 2026-11-04
> review_period: 30
> max_size: 4000

## Architecture

`ModuleRegistry` — реестр модулей (логическое имя, индекс, загруженные файлы); владелец — `ModuleLoader`.

## Facts and invariants

- **⚠ Одна AST на файл, Term НЕ удерживается:** тело конвертируется сразу; сайты импорта и модульный TU
  делят узлы и РАЗДЕЛЯЮТ вектор тела (`shared_ptr`).
- **⚠ Единая формула C++-имени модуля — `location/module_name.hpp`:** `encodeModuleNameForIdent`,
  `moduleEntryIdent`, `moduleCppNamespace` (пусто для root). Второй формулы нет.
- **⚠ Загрузка/импорт:** файл парсится один раз; duplicate-import — Error только при том же файле +
  scope + масках; ссылка `\path::name` НЕ загружает (`findLoaded`).

## Relations

- `module_loader/interface`, `module_loader/path`, `session` (`ModuleResolver`).
