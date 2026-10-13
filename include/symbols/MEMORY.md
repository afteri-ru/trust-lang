# MEMORY.md

> scope: include/symbols
> role: persistent-memory
> last_reviewed: 2026-11-05
> review_period: 30
> max_size: 7000

## Architecture

Символы и видимость имён: `SymbolTable` — фасад над internal-подкаталогами (состав — `README.md`), `SymbolIndex` —
проекция объявлений для LSP. Потребители — `semantic` (пишет таблицу) и `lsp` (читает проекцию);
кодоген таблицу не читает. Реализация — в internal-подкаталогах `table/`, `decl/`, `import/` (MEMORY в каждом).

## Facts and invariants

- **⚠ Модульный УРОВЕНЬ — `ScopeKind::ModuleRoot`, а НЕ глубина стека:** его объявления
  регистрируются под ключом идентичности модуля и могут быть file-private; без `ModuleNode`
  (прелюдия/тесты) им становится ПЕРВЫЙ вложенный скоуп. `ModuleContext` хранит узел-корень
  (tree id); `moduleRootNode()` без `ModuleNode` = 0 (global). Предикаты не смешивать.
- **⚠ `ScopeStack` persistent:** `pop()` закрывает скоуп, но не удаляет объявления; `Symbol*`
  переживает `pop()`/move (deque-узлы + map-элементы). `ExportRecord::symbol` — указатель на живой
  символ; `SymbolArena` — только синтетические слитые наборы.
- **⚠ Экспорт-поверхность:** только объявления МОДУЛЬНОГО уровня с квалифицированным ключом (функция/
  тип/анонимный блок не входят), ключ не `FilePrivate` и `key.module` пуст/равен модулю. Реэкспорт —
  только `m_reExported`, ТРАНЗИТИВЕН; `hasNamespace` учитывает оба набора.
- **⚠ forward→определение типа — отдельный путь:** `isForwardDeclaration` считает TypeDecl
  определением, поэтому `declareOrComplete` тип не завершает; `completeTypeDeclaration` обновляет
  записи скоупа с тем же TypeId и поверхность. `isExportImplementation` клиенту не публикуется.
- **⚠ Резолв `\\path::name` — только `NameResolver::findExported`:** идентичность → `m_reExported` →
  `m_declarations`; видимость — плоская привязка либо конфликт (`Scope::ambiguous`); `maskBypass` — лишь
  диагностика; локальное имя ссылку НЕ затеняет.
- **⚠ Привязка сайта импорта — ОДИН путь:** тело анализируется один раз, затем всегда
  `DeclarationRegistry::bindImportedModule`. Видимость решает ЕДИНОЕ правило
  `types/names.hpp`: маски, глубина цепочки, `Private`, `Protected` без доступа группы
  (`NotVisible` на ЛЮБОМ сайте). Сочетание «маска + protected + прямой доступ» — одна точка
  `NameResolver::hasVisibleDirectSite`. LSP-проекция `visibleSite` упрощена.
- **⚠ Неоднозначность — ОДНА точка:** `Scope::ambiguous` (один `AmbiguousBinding`: decls+modules) пишется
  там, где живёт привязка (в т.ч. в блоке) и проверяется в `NameResolver` на ЛЮБОМ уровне скоупа;
  callable-наборы сливаются. Одноимённые экспорты разных модулей не конфликтуют; регистрация
  идемпотентна (`AlreadyRegistered` — тот же узел AST).
- **⚠ `using` (`... = X`):** один проход, внутри скоупа — ВСЕ цели, callable-наборы СЛИВАЮТСЯ; C++-имя —
  по namespacePath выбранного объявления.
- **⚠ Namespace-алиас — привязка ПРЕФИКСА:** цепей нет, чужая привязка того же `(owner,alias)`
  отвергается; резолв — САМЫЙ ДЛИННЫЙ префикс (`matchesAliasPrefix`), только для алиасов своего
  модуля; при выходе из exported namespace переносится в корень квалифицированным. Экспортируемые
  алиасы фасада — факт ЗАГРУЗЧИКА; `viaAlias` — доступ без прямого сайта.
- **⚠ Сайт импорта — ОДИН тип `ImportSite`:** он же стек активных сайтов и журнал для LSP; запись
  идемпотентна, вызывается в ДВУХ точках (`pushImportScope` до анализа тела, `bindImportedModule` для
  повторного сайта), стек снимается RAII (`ImportSiteRegistry::Guard`); `bindImportedModule` НЕ
  переключает текущий модуль. Активность — `isSiteActive(scopeRange)` (по `begin.asPacked()`).
- **⚠ Module-aware видимость — ПОЛЕ ключа `StorageKey`:** private/import-hidden несут модуль,
  `ModuleExport` — ИДЕНТИЧНОСТЬ, плоский `Exported` — ПРИВЯЗКА сайта; `declarationStorageKeys` — единая
  таблица видимости (символы И типы), ключи ИНТЕРНИРУЮТСЯ.
- **⚠ Перегрузка — на ПРИВЯЗКЕ ИМЕНИ** (`types/call.hpp`), `declareOrComplete` кладёт КОПИЮ;
  Type-state — `Symbol::type`; путь перегрузки из ключа — только если пуст;
  скоуп-стек: вход/выход — только `enterScope`/`exitScope`.

## Decisions

- Компонент владеет таблицей символов и её LSP-проекцией: `semantic` пишет через `SymbolTable`, `lsp`
  читает `SemanticModel` (символы/видимость); кодоген читает факты AST, а не таблицу.

## Relations

- Правило видимости — `types/names.hpp` (единственный владелец); перегрузка —
  `types/call.hpp`; identities — `types/{storage_key,type_id}.hpp`.
- `semantic` → `symbols_lib`; `lsp` → `symbols_lib`; `pipeline` — для LSP-проекции.
