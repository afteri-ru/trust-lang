# MEMORY.md

> scope: include/transpiler/module
> role: persistent-memory
> last_reviewed: 2026-11-04
> review_period: 30
> max_size: 5000

## Architecture

Модульные юниты кодогенерации (`module/module_emit.cpp`): публикация заголовка модуля, out-of-line
определения, umbrella-заголовки фасадов. Полный контракт заголовка модуля и связь с `pipeline`
(`lowerModuleUnit`, `HeaderArtifact`) — `pipeline/MEMORY.md`.

## Facts and invariants

- **Сайт импорта — `#include`, не forward-decl:** C++-объявления клиента публикует заголовок модуля;
  экспорт-набор нужен только для имя-зависимых фактов (native-import, stack-check). Сопоставление с
  AST-узлами заголовка — по `node->origin` (fallback — сам узел), TermPtr-сопоставления нет.
- **⚠ Заголовок публикуемого типа отдаёт только ПРОТОТИП;** определение печатает `.cppt` как out-of-line
  (`Owner::name`, default-аргументы не повторяются). В заголовке тело остаётся ТОЛЬКО когда его нельзя
  вынести: член шаблонного record, шаблонный метод, `@[constexpr]` (`methodBodyMustBeInHeader`). Если
  такое тело ссылается на `__::`-символ, это Trust-диагностика (anonymous namespace `.cppt` невыразим
  в `.hpp`).
- **Заголовок фасада (реэкспорт) — umbrella:** `#pragma once` + `#include` заголовков
  реэкспортируемых модулей, собственных объявлений не содержит.
- **Source map для forward-decl подавляется** (`suppressMapping`): синтетическое объявление маппится
  только в собственном `.cppt`. В заголовке модуля маппинги подавлены полностью — позиции принадлежат
  `.cppt`.

## Relations

- `pipeline` (владелец модульных юнитов и артефактов), `transpiler/emit` (декларации внутри юнита),
  `module_loader` (экспорт-набор/проекции), `ast` (`origin`).
