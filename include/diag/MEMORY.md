# MEMORY.md

> scope: include/diag
> role: persistent-memory
> last_reviewed: 2026-08-22
> review_period: 30
> max_size: 6670

## Architecture

`diag` — хранение исходников (`.src`) и вывода (`.cppt`/`.hppt`), диагностики по диапазонам,
маппинг позиций вход↔выход. Ключевое архитектурное решение — **tagged-пространства** позиций:
`Context` (builder space, offset относительно body) vs `SourceMapReader` (reader space, offset
относительно full content = prepend + body). Offset'ы различаются по смыслу при идентичном
`packed`-представлении — смешение — частая ошибка. Все типы параметризованы тегом
(`MapperFile`/`ReaderFile`), компилятор запрещает кросс-пространственную передачу.
На diag зависят transpiler и semantic.

## Facts and invariants

- **1-based vs 0-based offset (ловушка):** source map — **1-based** (первый символ = offset 1);
  лексер внутри ведёт позиции **0-based**, поэтому при создании диапазона токена обязателен `+1`
  (`YY_MKRANGE`/`YY_TOKEN_SRC`). Синтаксические ошибки парсера (bison) отображают 0-based колонку —
  это отдельный механизм, не затрагивающий source map. Под 1-based рассчитаны `FileEntry::calc_column`
  (offset−1), `SourceMapWriter::mapStart/mapStop` (outputBegin = `size+1`), `locationToLspPosition`.
- `TaggedFile` и `TaggedLocation` используют НЕЗАВИСИМЫЕ битовые раскладки (флаг в бите 31; вход: 9
  бит index + 22 бита offset; выход: 5 бит index + 26 бит offset). Выход за пределы — FAULT.
- **Options (diag) — единая точка применения CLI-диагностик `-W`:** все потребители, включая
  trust-lsp `html_emit`, идут через `applyDiagnostics → Options::parse_argv`; обходного прямого
  вызова `parse_argv` из приложений нет. Поддерживает scoped `push()/pop()`, агрегаты `-Wall`/
  `-Wextra`, `-Whelp`, `-Werror`. Сентинелы `All` нет — report без id всегда выводит.
- **Пер-компонентные id диагностик (без единого DiagId):** каждая компонента объявляет СВОЙ enum
  через переиспользуемый `TRUST_DIAG_SET`/`TRUST_FLAG_SET` (`diag/diag_set.hpp`, определён ОДИН раз)
  в своём заголовке. Метаданные берутся через ADL (`diagName`/`flagName`/...) => `diag` остаётся
  листом (не включает заголовки компонентов). Регистрация — `registerDiagnostics()` на static-init.
- **Группы-агрегаты (стиль clang)** — центральный X-macro `WARN_GROUPS`; отдельные диагностики при
  объявлении привязываются к группам битовой маской. `-Wall`/`-Wextra`/`-W<group>` включают группу;
  `-Wno-<group>` выключает. Группа обрабатывается только при отсутствии `=value` (т.к. `deprecated` —
  и группа, и диагностика).
- **push/pop (ловушка):** дельты хранят СТАБИЛЬНОЕ (литерал) имя записи, НЕ переданный `string_view`
  (переданный из parser строковый литерал может быть временным — хранение его string_view привело
  бы к висячей ссылке; фиксировалось в MacroTest.OptionPushPopRestores).
- `OutputBuffer` (`Context::output_prepend`): префиксы группируются по namespace, дубликаты в рамках
  одного namespace подавляются (`std::set`). `prepend` добавляет строку целиком (без разбиения).
- Система группировки опций/флагов — в `include/diag/OPTIONS.md`.
