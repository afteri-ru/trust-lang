# MEMORY.md

> scope: test
> role: persistent-memory
> last_reviewed: 2026-11-06
> review_period: 30
> max_size: 4000

## Architecture

`test/unit` — GTest-библиотека `unit_tests`; `test/lit` — lit/FileCheck-тесты компилятора;
`test/integration`, `test/package`, `test/vscode` — сквозные наборы; `test/hooks` — selftest hook-системы.
`test/tools/structure_budget.py` — warn-only отчёт по «бюджету структуры», не входит в CTest.

## Facts and invariants

- **⚠ `test/lit/lit.cfg.py` — СИМЛИНК на `_build/test/lit/lit.cfg.py`** (генерируется CMake из
  `lit.cfg.py.in`): правки в него теряются при реконфигурации. Единственный источник — `lit.cfg.py.in`.
- **⚠ `config.excludes` сравнивается по BASENAME:** имя, встречающееся в нескольких каталогах
  (`call.src`), выключило бы ВСЕ одноимённые тесты. Фрагменты без `RUN` исключаются локальным
  `lit.local.cfg` своего каталога.
- **Временные файлы — ТОЛЬКО в каталоге сборки `_build`,** остаются после тестов. Каждый тест
  использует СВОЙ фиксированный подкаталог `TEST_DATA_DIR/<suite>` и не создаёт уникальных имён
  (mkdtemp/getpid/счётчики); хелпер `trust::test::makeTestDataDir(name)` очищает каталог и возвращает путь.
- **⚠ Имена тестов:** имя файла НЕ содержит токен СВОЕГО каталога, префиксы/суффиксы, дублирующие
  предка, и не повторяет префикс операции (файлы с общим префиксом ≥2 выносятся в подкаталог по
  операции). Группировка ОДНОУРОВНЕВАЯ. Переименование требует правки self-`CHECK`, `lit.cfg.py.in`
  (`config.excludes`) и ссылок в `docs/content/ru`.
- TrustLang игнорирует строки с `#` — метки `# CHECK:` можно держать в том же файле.
- **Dev-only тесты гейтятся режимом сборки:** lit-фичи `not-release`/`release` (по
  `TRUST_ENABLE_TRACE`); в unit-тестах развилка внутри тела — `#if TRUST_TRACE_ENABLED`.
- **Регистрация тестов:** `unit_tests` — ОДИН бинарник, но в CTest делится на шарды `unit_tests_shardN`
  через `--gtest_filter` (TRUST_UNIT_SHARDS; первые буквы сьютов покрывают A-Z+'_' — новый сьют не
  пропускается, суммы шардов = весь набор). `examples_tests` — bash-гарнесс с параллельным запуском
  примеров (EXAMPLES_PARALLEL; prime/fibonacci требуют аргументов — сборка+запуск).
- **Формат RUN-строки:** вывод всегда во временный файл (`> %t.out`), FileCheck читает его
  (`<%t.out`); без `2>/dev/null` (вместо этого `-q`), без пайпов и абсолютных путей; данные для Trust —
  непосредственно в файле.

## Relations

- Хуки-тесты — `test/hooks/selftest.sh` (см. `.clinerules/README.md`).
