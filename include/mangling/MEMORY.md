# MEMORY.md

> scope: include/mangling
> role: persistent-memory
> last_reviewed: 2026-09-28
> review_period: 30
> max_size: 3000

## Architecture

Единый лист ПРАВИЛ именования Trust → C++/нативные имена: C++-идентификаторы
функций/операторов/entry, суффиксы перегрузок, линковочные (asm) имена экспортов и схемы
временных имён. Потребители — анализатор (`semantic`) и кодогенерация (`transpiler`);
правила НЕ дублируются вне компонента. Кодоген имена НЕ порождает сам — только потребляет.

Низкоуровневый маппинг идентификатора Trust→C++ остаётся в `utils::name_to_cpp` (лист ниже);
`mangling` поднимает правила уровня языка над ним.

## Facts and invariants

- `sanitizeSymbolPart` ИНЪЕКТИВЕН: каждый не-алфанумерический символ (включая `_`) → `_<hex>`,
  поэтому разные имена дают разные линковочные символы.
- `exportSymbol` — единая схема линковочного имени экспорта: `__trust_<fn|var|op>_` +
  `sanitizeSymbolPart(qualifiedCppName)`; одна точка для кодогена (asm-метка) и Trust-интерфейса.
- `interfaceDecl` — единый рендер привязки интерфейса: `fwdDecl` (` := ...;`) → ` := %<symbol>;`.
- `overloadSuffix` детерминирован по СИГНАТУРЕ (не зависит от `registry_index`) — совпадает
  у объявления и сайта вызова.

## Decisions

- Компонент-лист: зависит только от `utils` и `types`.

## Relations

- `semantic` — вычисляет суффиксы перегрузок (`mangling::overloadSuffix`), синтезирует имена
  временных (`mangling::tmpName`).
- `transpiler` — потребляет `cppFuncIdent`/`sanitizeSymbolPart`/`tmpName`.
