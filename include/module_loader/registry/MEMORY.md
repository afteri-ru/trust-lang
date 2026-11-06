# MEMORY.md

> scope: include/module_loader/registry
> role: persistent-memory
> last_reviewed: 2026-11-13
> review_period: 30

## Architecture

`ModuleRegistry` — реестр модулей (логическое имя, индекс, загруженные файлы); владелец — `ModuleLoader`.

## Facts and invariants

- **⚠ Одна AST на файл, Term НЕ удерживается:** тело конвертируется сразу; сайты импорта и модульный TU
  делят узлы и РАЗДЕЛЯЮТ вектор тела (`shared_ptr`).
- **⚠ Единая формула C++-имени модуля — `location/module_name.hpp`:** `encodeModuleNameForIdent`,
  `moduleEntryIdent`, `moduleEncodedName` (пусто для root). Второй формулы нет.
- **⚠ Загрузка/импорт:** файл парсится один раз; duplicate-import — Error только при том же файле +
  scope + масках; `findLoaded` загрузку НЕ инициирует (только уже загруженные модули).
- **⚠ `m_modules` — `std::deque<ModuleRecord>`:** аксессоры возвращают ссылки внутрь записей
  (`interfaceFacts`/`bodyAst`/`scopeRoot`); `vector` запрещён — загрузка следующего модуля инвалидировала бы
  выданные ссылки (R1: невладеющая ссылка действительна в объявленном scope).

## Relations

- `module_loader/interface`, `module_loader/path`, `session` (`ModuleResolver`).
