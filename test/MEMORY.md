# MEMORY.md

> scope: test
> role: persistent-memory
> last_reviewed: 2026-09-23
> review_period: 30
> max_size: 5000

## Architecture

## Facts and invariants

- Имя файла теста НЕ содержит токен СВОЕГО каталога (предки не учитываются), а также
  префиксы/суффиксы, дублирующие каталог-предок (`trust_solver_`, `_codegen`, `_func`, `_var`, `_type`).
- Файлы с общим ведущим префиксом одной операции (порог ≥2) выносятся в подкаталог, названный по ней, а
  имя укорачивается на префикс: `checkarea/area_if_inside_ok.src` → `checkarea/if_inside_ok.src`. Если
  короткое имя совпало бы с именем каталога — используй `basic.src`. Группировка ОДНОУРОВНЕВАЯ: внутри
  выделенного каталога под-варианты остаются соседями, повторно не дробятся.
- Переименование теста обязательно сопровождается правкой его self-`CHECK`-строк,
  `test/lit/lit.cfg.py`/`lit.cfg.py.in` (`config.excludes`) и ссылок в `docs/content/ru`.
- Временные файлы создаются ТОЛЬКО в каталоге сборки `_build` и остаются после тестов (не удаляются).
  Артефакты собираются в `TEST_DATA_DIR`.
- Каждый тест использует СВОЙ фиксированный подкаталог `TEST_DATA_DIR/<suite>` и НЕ создаёт уникальных
  временных имён/каталогов (mkdtemp/getpid/счётчики). Хелпер — `trust::test::makeTestDataDir(name)`:
  очищает каталог в начале и возвращает путь. Удаление после теста убрано.
- TrustLang игнорирует строки с `#` — метки `# CHECK:` можно размещать в том же файле.
- Dev-only debug-тесты гейтятся по режиму сборки: lit-фичи `not-release`/`release` (по
  `TRUST_ENABLE_TRACE`: dev - трассировка ON, release - OFF). Вывод отладки - `# REQUIRES: not-release`;
  информационное сообщение release-сборки - `# REQUIRES: release`. В unit-тестах развилка стоит
  внутри тела каждого теста: `#if TRUST_TRACE_ENABLED` (dev) / `#else` (release-сообщение).

### LIT-тесты: формат RUN строки

```
# RUN: %trust -q [options] %s > %t.out && %FileCheck %s <%t.out
```

- Вывод всегда во временный файл (`> %t.out`), FileCheck читает из него (`<%t.out`).
- Без `2>/dev/null` (вместо этого `-q`), без пайпов, без абсолютных путей (только `%FileCheck`,
  `%trust`).
- Данные для Trust — непосредственно в файле (без `echo`/`sed`).

## Decisions

## Relations
