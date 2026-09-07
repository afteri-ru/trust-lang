# MEMORY.md

> scope: include/diag
> role: persistent-memory
> last_reviewed: 2026-08-22
> review_period: 30
> max_size: 8004

## Architecture

Хранение исходников (`.src`) и вывода (`.cppt`/`.hppt`), диагностики по диапазонам, маппинг вход↔выход.
Ключевое — **tagged-пространства позиций:** `Context` (builder, offset от body) vs `SourceMapReader`
(reader, offset от полного содержимого = prepend+body). `packed`-представление идентично, поэтому
смешение скрыто; типы параметризованы тегом, компилятор запрещает кросс-передачу. На diag зависят
transpiler и semantic.

## Facts and invariants

- **1-based vs 0-based offset (ловушка):** source map — 1-based (первый символ = offset 1); лексер внутри —
  0-based, поэтому при создании диапазона токена обязателен `+1`. Синтаксические ошибки bison — 0-based
  (отдельный механизм). Под 1-based рассчитаны `FileEntry::calc_column`, `mapStart/mapStop`, locationToLspPosition.
- `TaggedFile`/`TaggedLocation` — НЕЗАВИСИМЫЕ битовые раскладки (бит 31 — флаг; вход 9+22, выход 5+26).
- **Options (diag) — единая точка применения `-W`:** все потребители (вкл. trust-lsp html_emit) идут через
  `applyDiagnostics → Options::parse_argv`. scoped push/pop, агрегаты `-Wall/-Wextra`, `-Werror`.
- **Команды справки `-Whelp*` — тема, а не флаг диагностики:** `-Whelp`/`-Whelp-dsl`/`-Whelp-predef-macros`
  выставляют `Options::help_topic_` (`helpRequested()`=тема!=None, `helpTopic()`). Печать по теме — в
  приложении (trust.cpp): `Diagnostics`→`printHelp`; `DslMacros`/`PredefMacros`→макро-справка (diag — лист,
  не знает DSL/макросы; данные — из `Context::macroDefs()` и x-macro `predef_macro_x.hpp`/`pragma_macro_x.hpp`).
  Сентинела `All` нет — report без id всегда выводит.
- **Пер-компонентные id (без единого DiagId):** каждая компонента объявляет СВОЙ enum через
  `TRUST_DIAG_SET`/`TRUST_FLAG_SET` (определены ОДИН раз) в своём заголовке. Метаданные — через ADL =>
  diag остаётся листом (не включает заголовки компонентов).
- **Группы-агрегаты (clang-стиль):** центральный X-macro `WARN_GROUPS`; группа обрабатывается ТОЛЬКО при
  отсутствии `=value` (т.к. `deprecated` — и группа, и диагностика).
- **push/pop (ловушка):** дельты хранят СТАБИЛЬНОЕ (литерал) имя записи, НЕ переданный `string_view`
  (временный → висячая ссылка; фиксировалось в MacroTest.OptionPushPopRestores).
- `OutputBuffer` (`Context::output_prepend`): префиксы группируются по namespace, дубли подавляются
  (`std::set`); `prepend` добавляет строку целиком.
- Система группировки опций/флагов — `include/diag/OPTIONS.md`.
