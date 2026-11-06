# MEMORY.md

> scope: include/symbols
> role: persistent-memory
> last_reviewed: 2026-11-10
> review_period: 30

## Architecture

Символы и видимость имён: `SymbolTable` — фасад над internal-подкаталогами (состав — `README.md`);
`SymbolIndex` — LSP-проекция объявлений. Потребители — `semantic` (пишет) и `lsp` (читает); кодоген таблицу
НЕ читает. Реализация — `table/`, `decl/`, `import/`.

## Facts and invariants

- **⚠ Модульный УРОВЕНЬ — `ScopeKind::ModuleRoot`, а НЕ глубина стека:** объявления регистрируются под
  ключом идентичности модуля и могут быть file-private; без `ModuleNode` (stdlib DSL/тесты) им становится
  ПЕРВЫЙ вложенный скоуп. `moduleRootNode()` без `ModuleNode` = 0 (global). Предикаты не смешивать.
- **⚠ `ScopeStack` persistent:** `pop()` закрывает скоуп, но не удаляет объявления; `Symbol*` переживает
  `pop()`/move. `ExportRecord::symbol` — указатель на живой символ; `SymbolArena` — только синтетические
  слитые наборы.
- **⚠ Экспорт-поверхность:** только объявления МОДУЛЬНОГО уровня, ключ не `FilePrivate` и `key.module`
  пуст/равен модулю (функция/тип/анонимный блок не входят). Реэкспорт — только `m_reExported`,
  ТРАНЗИТИВЕН.
- **⚠ forward→определение типа — отдельный путь:** `isForwardDeclaration` считает TypeDecl определением,
  поэтому `declareOrComplete` тип не завершает; `completeTypeDeclaration` обновляет записи того же TypeId.
  `isExportImplementation` клиенту не публикуется.
- **⚠ Резолв экспорта ЧУЖОГО модуля — только `NameResolver::findExported`:** identity → `m_reExported` →
  `m_declarations`; видимость — плоская привязка либо `Scope::ambiguous`; локальное имя ссылку НЕ затеняет.
- **⚠ Привязка сайта импорта — ОДИН путь:** тело анализируется один раз, затем всегда
  `bindImportedModule`. Видимость — ЕДИНОЕ правило `types/names.hpp` (маски, глубина цепочки, `Private`,
  `Protected` без доступа группы → `NotVisible` на ЛЮБОМ сайте); «маска + protected + прямой доступ» — одна
  точка `NameResolver::hasVisibleDirectSite`.
- **⚠ Неоднозначность — ОДНА точка:** `Scope::ambiguous` пишется там, где живёт привязка, и проверяется в
  `NameResolver` на любом уровне; callable-наборы сливаются. Одноимённые экспорты разных модулей не
  конфликтуют; регистрация идемпотентна (`AlreadyRegistered` — тот же узел).
- **⚠ `using` (`... = X`):** один проход, внутри скоупа — ВСЕ цели, callable-наборы СЛИВАЮТСЯ; C++-имя — по
  namespacePath выбранного объявления.
- **⚠ Namespace-алиас — привязка ПРЕФИКСА:** синтаксис как у алиаса имени, но цель с завершающим
  `::` (`a :- ns::;`, LHS — только NAME, как в C++); область резолвится по видимым модулям.
  Неоднозначная область (имя в НЕСКОЛЬКИХ модулях) — ОТЛОЖЕННЫЙ резолв (`targetModule` невалиден):
  префикс раскрывается, имя решается обычным поиском (`resolveImplNoAlias`), неоднозначность ловится им же.
  Цепей нет, чужая привязка того же `(owner,alias)` отвергается; в блоке интерфейса — warning (не
  экспортируется);
  резолв — САМЫЙ ДЛИННЫЙ префикс (`matchesAliasPrefix`), только для алиасов своего модуля; при выходе из
  exported namespace переносится в корень квалифицированным.
- **⚠ Сайт импорта — ОДИН тип `ImportSite`:** он же стек активных сайтов и журнал LSP; запись идемпотентна,
  вызывается в ДВУХ точках (`pushImportScope`, `bindImportedModule`), стек снимается RAII;
  `bindImportedModule` НЕ переключает текущий модуль.
- **⚠ Module-aware видимость — ПОЛЕ ключа `StorageKey`:** private/import-hidden несут модуль, `ModuleExport` —
  ИДЕНТИЧНОСТЬ, плоский `Exported` — ПРИВЯЗКА сайта; `declarationStorageKeys` — единая таблица видимости,
  ключи ИНТЕРНИРУЮТСЯ.
- **⚠ Перегрузка — на ПРИВЯЗКЕ ИМЕНИ** (`types/call.hpp`), `declareOrComplete` кладёт КОПИЮ; тип — `Symbol::type`;
  скоуп-стек: вход/выход — только `enterScope`/`exitScope`.

## Decisions

- `semantic` пишет через `SymbolTable`, `lsp` читает `SemanticModel` (символы/видимость); кодоген читает факты
  AST, а не таблицу.

## Relations

- Правило видимости — `types/names.hpp`; перегрузка — `types/call.hpp`; identities —
  `types/{storage_key,type_id}.hpp`. Потребители — `semantic`, `lsp`, `pipeline` (LSP-проекция).
