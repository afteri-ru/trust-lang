# MEMORY.md

> scope: include/module_loader/interface
> role: persistent-memory
> last_reviewed: 2026-11-04
> review_period: 30
> max_size: 6000

## Architecture

`interface_install` (`stampModuleLevelNode`/`installFileInterface`) — установка интерфейса файла: штамп
модуля, блок интерфейса, реэкспорт, экспорт-набор и его факты. Шаги выполняются в ФИКСИРОВАННОМ порядке:
они мутируют `AstNodeBase::visibility`, и порядок наблюдаем в эмитируемом C++.

## Facts and invariants

- **⚠ Экспорт-набор держит DECLARATION-узлы:** `ExportEntry::node` — совладение узлом тела
  (`AstNodePtr`). Семантика мутирует только СЛОТЫ объявления, а не сам declaration-node: его
  адрес/идентичность стабильны. Замена declaration-node запрещена.
- **⚠ Видимость:** top-level — exported; `__::` — private (module-qualified ключ, anonymous namespace);
  `_::` — protected (та же группа/пакет). `installFileInterface` исключает объявления вне блока из
  экспорт-набора: не-exported имена становятся private; определение record, завершающее forward из
  интерфейса, помечается `AstNodeBase::isExportImplementation` (видимость/ключи НЕ меняются — тот же
  TypeId) и клиенту не публикуется (виден forward). `_::` фильтруется импортом и публикуется заголовком.
- **⚠ Реэкспорт = импорт в блоке интерфейса:** маска-фильтр набора владельца добавляется в
  экспорт-набор; владелец (`node->module`) не меняется; реэкспорт ТРАНЗИТИВЕН.
- **⚠ `ExportEntry::visibility` — уровень блоков ДО приватизации, `AstNodeBase::visibility` — видимость
  эмиссии ПОСЛЕ:** это НЕ один и тот же факт.

## Relations

- `module_loader` (scanModuleBody), `symbols/decl` (поверхность), `types/site_visibility`.
