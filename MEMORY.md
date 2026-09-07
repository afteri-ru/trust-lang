# MEMORY.md

> scope: . (root, project-level)
> role: persistent-memory
> last_reviewed: 2026-09-07
> review_period: 30
> max_size: 9830

## Architecture

TrustLang — транспилятор в C++ высокоуровневого языка общего назначения. Архитектура компонентов — в
пер-компонентных `MEMORY.md` (по одному на каталог; перечень — в AGENTS.md). Единый источник истины об
архитектуре и персистентной памяти — `MEMORY.md`; пользовательская документация — `docs/content/ru`
(рус., исходник), `README.md` — краткий обзор. Корневого `scripts/` нет.

## Facts and invariants

- Тесты всегда адаптируются под изменения кода, никогда наоборот (AGENTS.md правило 10), вкл.
  parser-тесты, закрепляющие внутреннюю структуру term/AST.
- **Временные переменные — уровень анализатора, не транспилятора:** конструкции, требующие временную
  (match, while-else, деструктуризация, hoist возврата), обязаны иметь её в AST как `const VarDecl`,
  созданный проходом lowering; транспилятор — буквальный перевод AST в C++ и НЕ создаёт временные сам
  (подробно: include/transpiler/MEMORY.md, Decisions).

## Decisions

- **Порядок CTest и фикстуры (обязательный):** unit → lit → vscode → examples → integration → package.
  Пакетные тесты регистрируются ПОСЛЕДНИМИ (test/package/), их CTest-номера финальные; REQUIRE фикстуру
  package_deps (SET UP unit/lit/vscode). Перенос пакетных тестов в начало ломает нумерацию.
- **Сборка/тесты пакетов — только в релизе:** дистрибутивный архив и `.deb` (`package`/`deb`) существуют
  и собираются ТОЛЬКО при `CMAKE_BUILD_TYPE=Release`. Опция `TRUST_BUILD_PACKAGE` имеет дефолт ON в release
  и OFF в dev (можно принудительно включить на dev для прогона package-тестов). При OFF ни targets
  `package`/`deb`, ни скрипты, ни package-тесты не существуют (cmake/package.cmake и
  add_subdirectory(test/package) обёрнуты в if).
- **Версия по режиму сборки:** три макроса — `TRUST_VERSION_SHORT` (чистая X.Y.Z), `TRUST_VERSION_FULL`
  (X.Y.Z-<hash>) и эффективная `TRUST_VERSION` = SHORT в release, FULL в dev. Продуктовые строки, кеш `--run`
  и имена артефактов/архивов используют эффективную `TRUST_VERSION`; языковые преdef-макросы зафиксированы
  независимо от режима: `@__TRUST_VERSION__`→SHORT, `@__TRUST_VERSION_FULL__`→FULL. Локус — cmake/version.cmake.
- **Релизная сборка — release/*-ветка + строгий annotated-тэг v<X.Y.Z> (release_checks.cmake):** при
  `CMAKE_BUILD_TYPE=Release` конфиг допустим только если HEAD стоит ровно на annotated-тэге `v<X.Y.Z>`
  == VERSION (`git describe --exact-match --tags HEAD` == `v<X.Y.Z>` и `cat-file -t` == `tag`) И текущая
  ветка — `release/<MAJOR>.<MINOR>.X` (== MAJOR.MINOR) или `release/<MAJOR>.X` (== MAJOR), либо detached
  на этом тэге; дерево полностью чистое (`git status --porcelain` пуст) и в шапке CHANGELOG.md есть
  `[Release v<X.Y.Z>]`. Любое другое состояние — ошибка конфигурации; сообщение заканчивается
  Fix-командой git. Релизные бинарники встраивают чистый номер версии, поэтому он обязан соответствовать
  источнику сборки.
- **Артефакты `package`/`deb`/VSIX — только из полностью чистого дерева (require_clean_tree.cmake):**
  чистота по `git status --porcelain` (вкл. staged/untracked; в manifest архива — GitHash). Любые
  незакоммиченные изменения → ошибка в release (артефакты в дефолтной сборке), предупреждение при
  принудительном включении в dev. `TRUST_BUILD_VSIX` — как `TRUST_BUILD_PACKAGE`: default ON в
  release, OFF в dev.
- **LIT-подкаталоги не перечисляются вручную:** test/lit/CMakeLists.txt выводит их из реальных .src/.ast
  через GLOB_RECURSE CONFIGURE_DEPENDS.
- **Конвенция опций CLI (диагностики vs поведение, GCC/Clang-стиль):** диагностики — фикс. набор
  `ignore|warning|error`, орфография `-W<name>=<sev>`; поведенческие — произвольные значения,
  `--<name>=<value>`/`-f<name>`. `solver` — severity `-Wsolver=` (не выдаётся при активном `--solver-mode`)
  + поведенческий `--solver-mode=assert|export|calculate`. Применение единообразно в CLI и LSP:
  определены ОДИН раз (`commonAnalysisOptions`), применяются центрально (`applyAnalysisArgs`); LSP не
  объявляет их (analysis_passthrough). LSP применяет окружение и шебанг ПО ИСТОЧНИКУ (trust.shebangMode):
  ошибка из шебанга — диагностика на строке шебанга, из окружения — в лог. Список конкретных работ/
  отклонений — в `.tasklog`, не в MEMORY.

## Relations

- Перекрёстные связи компонентов — в пер-компонентных `MEMORY.md` (Relations/Facts), напр. transpiler и
  semantic зависят от diag.
