# MEMORY.md

> scope: include/formatter
> role: persistent-memory
> last_reviewed: 2026-11-10
> review_period: 30

## Architecture

Публичный API — `formatter/format.hpp`: `format(source, sourceName, opts, Parser&)`,
`formatFile(path, opts)`, `loadConfig`/`findConfig` (поиск `.trust-format` вверх по дереву, как
clang-format), `FormatOptions` (+ x-macro `KEYWORD_SIGIL_LIST` — единый источник значений
`remove|add|none` для конфига/CLI/dump).

Конвейер: `Parser` отдаёт поток токенов через `on_token` и классификацию макросов через
`Macro::on_macro_kind` (события `MacroEvent`) → строится `std::vector<Token>` с 0-based
offset'ами → зазоры между токенами разбирает `parseGap` (пробелы/`#`-комментарии/`/*...*/`;
любой иной символ → `verbatim`-зазор) → `Emitter`: `precomputeGroups()` (multiline-группы
скобок, `openStack_`, `inParen_`), затем один проход: вывод зазоров/комментариев
(`emitGapComments`), правила перевода строки (`computeNewline`) и пробела (`computeSpace`),
отступы (`indent_`, `contractIndent_`). Хвостовой зазор после последнего токена сохраняется
вербатим, если парсер не дал токенов (незакрытая строка/комментарий).

`format.cpp` — точка входа (сбор токенов + макро-timeline + `Emitter::run()`); детали стадий —
в `impl/` (`format_model.hpp`/`token_gap.cpp` — модель токена/зазора и классификация,
`emitter.hpp`/`emitter.cpp` — `Emitter`). `config.cpp` —
разбор `.trust-format` (key:value; неизвестный ключ/значение → ошибка, без silent fallback).
`command.cpp` — CLI-команда `--format`/`--format-check` (чтение файла, конфиг, keywords от
вызывающего, вывод/проверка); не зависит от `pipeline_lib`.

## Facts and invariants

## Decisions

## Relations
