# MEMORY.md

> scope: include/symbols/import
> role: persistent-memory
> last_reviewed: 2026-11-04
> review_period: 30
> max_size: 5000

## Architecture

`ImportSiteRegistry` (сайты импорта: активный стек + журнал для LSP), `AliasRegistry`
(namespace/module-алиасы).

## Facts and invariants

- **⚠ Привязка сайта импорта — ОДИН путь:** тело анализируется один раз, затем всегда
  `DeclarationRegistry::bindImportedModule`. Видимость решает ЕДИНОЕ правило
  `types/names.hpp`: маски, глубина цепочки, `Private`, `Protected` без доступа группы
  (`NotVisible` на ЛЮБОМ сайте). Сочетание «маска + protected + прямой доступ» — одна точка
  `NameResolver::hasVisibleDirectSite`.
- **⚠ Сайт импорта — ОДИН тип `ImportSite`:** он же стек активных сайтов и журнал для LSP; запись
  идемпотентна, вызывается в ДВУХ точках (`pushImportScope` до анализа тела, `bindImportedModule` для
  повторного сайта), стек снимается RAII (`ImportSiteRegistry::Guard`); `bindImportedModule` НЕ
  переключает текущий модуль. Активность — `isSiteActive(scopeRange)`.
- **⚠ Неоднозначность — ОДНА точка:** `Scope::ambiguous` (один `AmbiguousBinding`: decls+modules) пишется
  там, где живёт привязка (в т.ч. в блоке) и проверяется в `NameResolver` на ЛЮБОМ уровне скоупа;
  callable-наборы сливаются. Одноимённые экспорты разных модулей не конфликтуют; регистрация
  идемпотентна (`AlreadyRegistered` — тот же узел AST).
- **⚠ `using` (`... = X`):** один проход, внутри скоупа — ВСЕ цели, callable-наборы СЛИВАЮТСЯ; C++-имя —
  по namespacePath выбранного объявления.
- **⚠ Namespace-алиас — привязка ПРЕФИКСА:** цепей нет, чужая привязка того же `(owner,alias)` отвергается;
  резолв — САМЫЙ ДЛИННЫЙ префикс (`matchesAliasPrefix`), только для алиасов своего модуля; при выходе из
  exported namespace переносится в корень квалифицированным. Экспортируемые алиасы фасада — факт
  ЗАГРУЗЧИКА; `viaAlias` — доступ без прямого сайта.

## Relations

- `symbols/decl`, `symbols/table`, `types/site_visibility`, `module_loader`.
