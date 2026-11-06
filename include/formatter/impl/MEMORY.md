# MEMORY.md

> scope: include/formatter/impl
> role: persistent-memory
> last_reviewed: 2026-11-13
> review_period: 30

## Architecture

Внутренняя зона форматтера (детали реализации; публичный API — `formatter/format.hpp`).
Разделение по стадиям конвейера:

- `format_model.hpp` — модель токена/зазора (`Token`, `SegKind`, `Seg`, `Gap`) и объявления
  классификации токенов (`isOpen`/`isClose`/`isBinaryOperator`/`isUnaryOperator`/`isDoc`/
  `isNameLike`/`isNumericLiteral`), `splitNegativeLiterals`, `parseGap`.
- `emitter.hpp` — класс `Emitter` (объявление): один проход по токенам, правила перевода строки
  (`computeNewline`) и пробела (`computeSpace`), отступы, вывод зазоров/комментариев.
- `src/formatter/impl/token_gap.cpp` — определения классификации и разбора зазоров.
- `src/formatter/impl/emitter.cpp` — определения методов `Emitter`.
- `src/formatter/impl/format.cpp` — точка входа: прогон парсера для сбора токенов и макро-timeline,
  затем `Emitter::run()`; `formatFile` — чтение файла + standalone-контекст.

## Facts and invariants

- **Классификация токенов — политика ФОРМАТИРОВАНИЯ, а не грамматики:** наборы `TermID` в
  `isBinaryOperator`/`isUnaryOperator`/`isNameLike` задают расстановку пробелов, а не разбор.
- **«Ключевые слова» не встроены:** эффективный список приходит в `FormatOptions.keywords`
  (DSL-макросы); распознавание не зависит от ведущего `@` (`stripMacroSigil`).
- **Зазор `verbatim`** (raw/embed/macro) сохраняется как есть; нормализуются только ведущие
  пробелы. Хвост после последнего токена при `verbatim` не теряется.

## Relations

- `formatter/format.hpp` (публичный API, `FormatOptions`), `syntax` (TermID/парсер/макросы),
  `utils` (`stripMacroSigil`, `FileIO`).
