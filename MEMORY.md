# MEMORY.md

> scope: . (root, project-level)
> role: persistent-memory
> last_reviewed: 2026-08-25
> review_period: 30
> max_size: 8192

## Architecture

TrustLang — компилятор (транспилятор в C++) высокоуровневого языка общего назначения.
Архитектура компонентов описана в пер-компонентных `MEMORY.md` (по одному на каталог, правило и
перечень компонентов — в `AGENTS.md`). Единый источник истины об архитектуре и персистентной
памяти — файлы `MEMORY.md`; пользовательская документация — `docs/content/ru` (исходники на
русском), `README.md` — краткий обзор. Скрипты живут в каталогах компонентов: корневого
`scripts/` нет.

## Facts and invariants

- Тесты всегда адаптируются под изменения кода, никогда наоборот (AGENTS.md правило 10), включая
  parser-тесты, закрепляющие внутреннюю структуру term/AST.

## Decisions

- **Порядок CTest и фикстуры (обязательный порядок запуска):** unit → lit → vscode → examples →
  integration → package. `package_target_test`/`deb_package_test` регистрируются ПОСЛЕДНИМИ
  (подкаталог `test/package/`), их CTest-номера финальные. Они REQUIRE фикстуру `package_deps`,
  которую SET UP unit/lit/vscode (по аналогии с `integration_tests`/`integration_deps`).
  Перенос пакетных тестов в начало ломает нумерацию (add_test корневого каталога всегда в начале).
- **LIT-подкаталоги не перечисляются вручную:** `test/lit/CMakeLists.txt` выводит их из реальных
  `.src`/`.ast`-файлов через `GLOB_RECURSE CONFIGURE_DEPENDS`; новый подкаталог подхватывается на
  переконфигурации.
- **Конвенция опций CLI (диагностики vs поведение, в духе GCC/Clang):** диагностики — фиксированный
  набор значений `ignore|warning|error`, орфография `-W<name>=<sev>`; поведенческие флаги —
  произвольные значения, орфография `--<name>=<value>` или `-f<name>`. Запрещено выносить
  поведенческий флаг под `-W` (и наоборот). `solver` — два ортогональных механизма: severity
  `-Wsolver=` (presence-диагностика, default warning, НЕ выдаётся при активном `--solver-mode`) и
  поведенческий `--solver-mode=assert|export|calculate`.
  Применение опций анализа (`-W` + `--solver-mode`/`--keywords`/`-fsolver-loop-unroll`)
  единообразно в CLI (trust) и LSP: определены ОДИН раз (`commonAnalysisOptions` в cli.hpp),
  применяются центрально (`applyAnalysisArgs` в pipeline/analysis_options.hpp). LSP НЕ объявляет их
  в своей таблице — неизвестные `--name=value`/`-fname` собираются через analysis_passthrough
  (общий parseDriverArgs). LSP применяет окружение и шебанг ПО ИСТОЧНИКУ (`trust.shebangMode`):
  ошибка опции из шебанга — обычная диагностика на строке шебанга, из окружения — в лог. Список
  конкретных работ/отклонений ведётся в `.tasklog`, не в MEMORY.

## Relations

- Перекрёстные связи компонентов — в пер-компонентных `MEMORY.md` (разделы «Relations»/
  «Facts and invariants»), напр. transpiler и semantic зависят от diag.
