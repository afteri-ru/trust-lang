# MEMORY.md

> scope: include/diag/serialize (+ src/diag/serialize)
> role: persistent-memory
> last_reviewed: 2026-11-10
> review_period: 30

## Architecture

Форматтеры диагностик — реализации `DiagnosticFormatter` (diag_formatter.hpp): `TextFormatter`
(по умолчанию: file:line:col + caret), `JsonFormatter` (массив объектов), `SarifFormatter`
(SARIF 2.1.0, один run). Единственная точка создания — `makeDiagnosticFormatter`; владение — у
`DiagnosticEngine`. JSON-экранирование — `utils::escapeJson` (`utils/strings/strings_json.hpp`
через фасад `utils/strings.hpp`), `json_write.hpp` — header-only хелперы поверх него.

## Facts and invariants

- **⚠ JSON/SARIF открываются конструктором и закрываются деструктором:** у движка нет сигнала
  «конец прогона»; границы держит время жизни форматтера (копирование запрещено, владение —
  `unique_ptr`).
- **⚠ `DiagnosticEntry` — единственная модель данных:** text/json/sarif — проекции одной записи
  (отдельного «текстового» пути нет).
- Локацию резолвит ЕДИНЫЙ `diag::diagnosticLocation`/`decodeRange` (нет дубля offset→line/col);
  невалидная локация → `null`-поля (никаких «0;0»).
- Fixits: json — `fixits`, sarif — `fixes`, сериализуются ВСЕ; SARIF `fix` — spec-валидный
  (`artifactChanges`/`replacements`/`deletedRegion`/`insertedContent`), локация fixit обязана
  резолвиться (иначе `EXPECT`). `writeFixitNote` — только TextFormatter; координаты печатаются лишь
  при валидной локации (никаких `:0:0`).

## Decisions

- Поток вывода по умолчанию — `trust::errs()`. `Json`/`Sarif` принимают `std::ostream&`
  (default-аргумент); `TextFormatter` (дефолт движка) резолвит `errs()` В МОМЕНТ ЗАПИСИ — движок
  создаётся раньше, чем вызывается `setErrs`. Инъекция потока (для юнит-тестов) есть у всех трёх.

## Relations

- `diag` (движок/`DiagnosticEntry`/`SourceTextProvider`), `utils` (`errs`, `FAULT`, `escapeJson`).
