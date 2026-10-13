# MEMORY.md

> scope: include/types/names (+ src/types)
> role: persistent-memory
> last_reviewed: 2026-11-05
> review_period: 30
> max_size: 3200

## Architecture

Домен имён типов: `storage_key.hpp` (структурный ключ видимости), `site_visibility.hpp` (правило
«видно ли имя с сайта импорта и под каким ключом»), `type_names.hpp` (индекс имён). Публичный
доступ — umbrella `types/names.hpp`.

## Facts and invariants

- **⚠ storageKey ≠ display name:** ключ СТРУКТУРНЫЙ (`{VisibilityClass, ModuleIndex, namespacePath,
  name}`), текст в ключ не кодируется; одноимённые типы разных модулей не конфликтуют.
- **⚠ `siteVisibilityOf`/`siteKeysFor` — ЕДИНСТВЕННЫЙ владелец правила видимости сайта:**
  `Private` → единственный module-local ключ; `Protected` без доступа группы → `NotVisible`.

## Decisions

- **⚠ `TypeNameIndex` ключует привязки `StorageKey` для ВСЕХ именованных видов;** ключ СТРУКТУРНЫЙ
  (`{VisibilityClass, ModuleIndex, namespacePath, name}`), текст в ключ не кодируется. Идентичность
  экспортируемого типа — `ModuleExport` (модуль в ключе): одноимённые типы разных модулей не конфликтуют,
  а ПЛОСКИЙ ключ — привязка сайта импорта. Встроенные имена — ОТДЕЛЬНЫЙ процессный домен (`BuiltinTypeCore`):
  `findType(StorageKey)` ищет пользовательский реестр, затем — только для `Exported` без namespace —
  встроенные; неоднозначное имя текстом не резолвится.
- **⚠ `nominalCppName` ставит модульный namespace `<enc>` ПЕРВЫМ сегментом** (`TypeSystem::moduleCppNamespace`;
  провайдер `ModuleNameLookup` внедряет Pipeline, без него — FAULT, не тихое имя без изоляции).
  `declareRecord` идемпотентен ТОЛЬКО в пределах одного файла (другой файл ⇒ другой `TypeId`, а не
  `duplicate type name`); `reserveTypeName`/`reportDuplicateTypeName` — ЕДИНСТВЕННАЯ точка дубликат-диагностики.
- **⚠ Класс видимости имеет ЕДИНСТВЕННЫЕ предикаты** (`switch` без `default`): новый перечислитель обязан
  ломать сборку (`-Werror`), а не тихо менять LSP/проекции.

## Relations

- `module_loader` (данные сайта), `symbols` (применение решения), `utils::ImportMasks` (разбор масок).
