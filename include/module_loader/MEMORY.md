# MEMORY.md

> scope: include/module_loader
> role: persistent-memory
> last_reviewed: 2026-09-27
> review_period: 30
> max_size: 5500

## Architecture

Оркестрация загрузки модулей и реестр. Интегрирован с syntax Parser: `parseSourceModule()`
рекурсивно вызывает Parser (`expand_module=true`); `ensureLoaded()` резолвит `<name>.trust` (заглушка)
или `<name>.src`. Идентификация модулей — ТОЛЬКО по индексу (`ModuleRegistry`); `moduleId`
используется лишь при загрузке для детекции циклических зависимостей.

`module_export` (`module_export.hpp`/`.cpp`) — сбор экспорт-интерфейса модуля из AST-тела и утилиты
интерфейса: `matchesAnyMask`/`matchGlob` (фильтр масок), `declName`/`filterModuleDecls` (фильтр строк
Trust-интерфейса `.pcp`). Правая часть декларации — конкретное нативное имя символа `%<symbol>`; имя
символа ГЕНЕРИРУЕТ транспилятор (`__trust_*`, `asm`-метка) — см. `include/transpiler/MEMORY.md`.

**Владение:** `ModuleLoader` (по значению владеет `ModuleRegistry`) — владелец **Pipeline**
(`unique_ptr`), внедряется в `Context` невладеющим указателем. Обратная ссылка `m_ctx` — невладеющая
`Context&`; цикл по ссылкам, но безопасен (единый владелец — Pipeline). Не копируется/не перемещается.

## Facts and invariants

- **⚠ Необъявленная связь syntax ↔ module_loader:** `syntax/parser.cpp` при обработке import сам
  вызывает `m_ctx.loader()` (`ensureLoaded`/`body`/`indexOf`), но `syntax_lib` не линкует
  `module_loader_lib` — символы используются транзитивно через `Context`. Разрыв инкапсуляции;
  целесообразно перенести обработку import в module_loader.
- **Детекция цикла:** запись существует, но тело ещё не заполнено → модуль в процессе загрузки →
  FAULT. Выход индекса за границы → FAULT.
- **Экспорт:** по умолчанию экспортируются ВСЕ top-level объявления в НЕ анонимных областях имён
  (глобальная `::`, именованные `ns::` — с квалификацией); из анонимной `_` и локальных — нет.
- **Раздельная компиляция:** каждый импортированный `.src`-модуль — отдельный `.cppt` (полное тело),
  линкуется с главным (`SRC_MODULES` в Makefile). На сайте импорта — только forward-decl экспортов
  (`asm`-метка зеркалит символ модуля). Экспорт-таблицы/`.trust.sabi` нет: семантику несёт
  Trust-интерфейс `.pcp` (`<stem>.src`).
- **Интерфейс:** `ModuleRecord::m_interface` — «полный» экспорт (заполняет анализатор);
  `ModuleNode::m_exports` — отфильтрованный интерфейс конкретного сайта импорта (по маскам
  `\module(mod, masks)`; пустой список = все экспорты).

## Decisions

## Relations
