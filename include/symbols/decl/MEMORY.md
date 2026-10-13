# MEMORY.md

> scope: include/symbols/decl
> role: persistent-memory
> last_reviewed: 2026-11-05
> review_period: 30
> max_size: 5000

## Architecture

`DeclarationRegistry` (записи объявлений), `ModuleSurface` (экспорт-поверхность), `ModuleContext`,
`NameResolver` (резолв и видимость).

## Facts and invariants

- **⚠ Экспорт-поверхность:** только объявления МОДУЛЬНОГО уровня с квалифицированным ключом (функция/
  тип/анонимный блок не входят), ключ не `FilePrivate` и `key.module` пуст/равен модулю. Реэкспорт —
  только `m_reExported`, ТРАНЗИТИВЕН; `hasNamespace` учитывает оба набора.
- **⚠ forward→определение типа — отдельный путь:** `isForwardDeclaration` считает TypeDecl
  определением, поэтому `declareOrComplete` тип не завершает; `completeTypeDeclaration` обновляет
  записи скоупа с тем же TypeId и поверхность. `isExportImplementation` клиенту не публикуется.
- **⚠ Резолв `\path::name` — только `NameResolver::findExported`:** идентичность → `m_reExported` →
  `m_declarations`; видимость — плоская привязка либо конфликт (`Scope::ambiguous`); `maskBypass` — лишь
  диагностика; локальное имя ссылку НЕ затеняет.
- **⚠ `ExportRecord::symbol` — невладеющий `Symbol*`** (владелец — `ScopeStack`: узел скоупа живёт
  до конца прогона): набор перегрузок пополняется в том же объекте и сразу виден поверхности;
  поверхность — единственное хранилище записей.

## Relations

- `symbols/table`, `symbols/import`, `module_loader` (экспорт-набор), `types/site_visibility`.
