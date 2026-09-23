# MEMORY.md

> scope: . (root, project-level)
> role: persistent-memory
> last_reviewed: 2026-09-23
> review_period: 30
> max_size: 14200

## Architecture

TrustLang — транспилятор высокоуровневого языка в C++. Архитектура компонентов — в пер-компонентных
`MEMORY.md` (по одному на каталог); они же — единый источник истины об архитектуре и персистентной
памяти. Пользовательская документация — `docs/content/ru` (рус., первоисточник); `docs/content/en` — её
синхронный перевод; `README.md` — обзор.

### Отладочная диагностика анализатора (кратко)

Проверка внутреннего состояния анализатора AST (скоупы, регистрация имён, borrow-checker) без правки
кода: два уровня + гейт сборки. Гейт: CMake `TRUST_ENABLE_TRACE` (dev ON / release OFF) →
`TRUST_TRACE_ENABLED`; при 0 C++-макросы вырезаны (тихий no-op; аргументы не вычисляются), а
`.src`-макросы не создают вывода и дают `Severity::Warning`.

- Источник (C++, только dev): `TRUST_DEBUG("tags","fmt",args...)` — сообщение;
  `TRUST_SCOPE("tags", symbols[, "level=all", …])` — ручной дамп состояния.
- Управление (TrustLang-макросы в `.src`): `@__DEBUG__("<маски>")`/`@__DEBUG__()` — включить/выключить
  ФИЛЬТР сообщений; `@__DEBUG_SCOPE__([<маски имён>][, key=value…])` — дамп состояния, от фильтра НЕ
  зависит (позиционные = маски имён; значения опций без кавычек: `level|types|max|count`).
- Фильтр: авто-токены из `__FILE__` (`root|path|component|rel|stem|file`) + явные теги; маски `*`/`?`,
  списки через `,`. Без заданного фильтра сообщения молчат. Каждая строка префиксуется
  `.../файл:строка:` (для `TRUST_DEBUG` — место вызова, для `@__DEBUG*` — локация самого макроса).
- Готовые теги: `scope` (вход/выход скоупа), `resolve` (резолв имени), `declare` (var/func), `borrow`
  (вид/владение/регион, заём→anchor, эпоха, R4/R6).

## Facts and invariants

- **Контроль целочисленного переполнения включён по умолчанию** (`-foverflow-check`/
  `-fno-overflow-check`): знаковая арифметика `+ - *` и `+= -= *=` транслируется через
  `__builtin_*_overflow` с `throw trust::IntMinus` (ловится `{-…-}`); только знаковые машинные целые.
  Составные присваивания в AST — `AssignOp` (лексер `OP_ASSIGN`), арифметика — `MathOp`.
- **Тесты всегда адаптируются под изменения кода, никогда наоборот** (AGENTS.md правило 10), вкл.
  parser-тесты, закрепляющие внутреннюю структуру term/AST.
- **Временные переменные — уровень анализатора, не транспилятора:** конструкции, требующие временную
  (match, while-else, деструктуризация, hoist возврата), обязаны иметь её в AST как `const VarDecl`,
  созданный проходом lowering (подробно — `include/transpiler/MEMORY.md`).
- **LIT-подкаталоги не перечисляются вручную:** выводятся из реальных `.src`/`.ast` через GLOB_RECURSE
  CONFIGURE_DEPENDS.
- **Конвенция опций CLI (диагностики vs поведение, GCC/Clang-стиль):** диагностики — фикс. набор
  `ignore|warning|error`, орфография `-W<name>=<sev>`; поведенческие — произвольные значения,
  `--<name>=<value>`/`-f<name>`. `solver` — severity `-Wsolver=` (не выдаётся при активном
  `--solver-mode`) + поведенческий `--solver-mode=assert|export|calculate`. Применение единообразно в
  CLI и LSP: определены ОДИН раз (`commonAnalysisOptions`), применяются центрально
  (`applyAnalysisArgs`); boolean-поведенческие флаги валидируются единообразно (`parseBoolFlagValue`);
  LSP не объявляет их (analysis_passthrough). LSP применяет окружение и шебанг ПО ИСТОЧНИКУ
  (trust.shebangMode): ошибка из шебанга — диагностика на строке шебанга, из окружения — в лог.
- **Слои встраиваемых артефактов разделены компонентами (префиксы НЕ смешиваются):** встроенные типы
  → `include/runtime/` (публикуются как `@trust/…`); стандартная библиотека → `include/stdlib/`
  (вшиты в КОМПИЛЯТОР, публикуются как `@stdlib/…`); инфраструктура компилятора → `include/trust.cppm`
  + генерируемый `version.h`. Ассеты ОБОИХ слоёв описаны ЕДИНЫМ каталогом `include/assets/asset_catalog.hpp`
  (поле `AssetSource`); извлечение — единый алгоритм с двумя бэкендами: `#embed`-провайдер компилятора
  (`src/assets/asset_provider.cpp`) и ELF-секции trust-runtime. Критерии отнесения — в пер-компонентных `MEMORY.md`.

- **Сравнение типов `<~`/`~~`/`~~~` — статическая (компиляционная) свёртка:** результат `Bool`;
  номинальная (`<~`) — с наследованием (реестр `baseClasses`), утиная (`~~`) и строгая (`~~~`) — по
  полям/тождеству. Кодоген эмитит `true`/`false` (нулевая стоимость). Динамическая (рантайм) проверка
  стёртых значений и имя типа из строковой переменной НЕ реализованы — явная ошибка компиляции
  (без silent fallback). Символы: только `<~`/`~~`/`~~~` (отрицаний нет; голая `~` — не оператор).

## Decisions

- **Общий PCH — только оптимизация, НЕ несущая (`TRUST_USE_PCH`):** default ON в dev / OFF в release;
  `ON` в release — ошибка конфигурации (`release_checks`-стиль). Обязательный верификационный конфиг —
  `TRUST_USE_PCH=OFF` (PCH маскирует отсутствующий `#include`). Стабильный префикс —
  `include/precompiled/trust_pch.hpp` (без `trust/version.h`); актуальность — depfile.
  Детали — `include/precompiled/MEMORY.md`.
- **Порядок CTest и фикстуры (обязательный):** unit → lit → vscode → examples → integration → package.
  Пакетные тесты регистрируются ПОСЛЕДНИМИ (их CTest-номера финальные); REQUIRE фикстуру `package_deps`
  (SET UP unit/lit/vscode). Перенос пакетных тестов в начало ломает нумерацию.
- **Сборка/тесты пакетов — только в релизе:** дистрибутивный архив и `.deb` (`package`/`deb`)
  существуют ТОЛЬКО при `CMAKE_BUILD_TYPE=Release`. Опция `TRUST_BUILD_PACKAGE` — default ON в release,
  OFF в dev (принудительно `-D...=ON`). При OFF ни targets `package`/`deb`, ни скрипты, ни
  package-тесты не существуют.
- **Версия по режиму сборки:** `TRUST_VERSION_SHORT` (X.Y.Z), `TRUST_VERSION_FULL` (X.Y.Z-<hash>) и
  эффективная `TRUST_VERSION` = SHORT в release / FULL в dev. Продуктовые строки, кеш `--run` и имена
  артефактов используют эффективную; языковые predef-макросы зафиксированы независимо:
  `@__TRUST_VERSION__`→SHORT, `@__TRUST_VERSION_FULL__`→FULL (locus — `cmake/version.cmake`).
- **Релизная сборка — release/*-ветка + строгий annotated-тэг `v<X.Y.Z>`:** при `Release` конфиг
  допустим только если HEAD стоит ровно на annotated-тэге `v<X.Y.Z>` == VERSION И ветка —
  `release/<MAJOR>.<MINOR>.X` или `release/<MAJOR>.X`, либо detached на этом тэге; дерево полностью
  чистое и в шапке CHANGELOG.md есть `[Release v<X.Y.Z>]`. Иначе — ошибка конфигурации. Релизные
  бинарники встраивают чистый номер версии, поэтому он обязан соответствовать источнику сборки.
- **Артефакты `package`/`deb`/VSIX — только из полностью чистого дерева:** чистота по
  `git status --porcelain` (вкл. staged/untracked; в manifest архива — GitHash). Любые незакоммиченные
  изменения → ошибка в release (артефакты в дефолтной сборке), предупреждение при принудительном
  включении в dev. `TRUST_BUILD_VSIX` — как `TRUST_BUILD_PACKAGE`: default ON в release, OFF в dev.

## Relations

- Перекрёстные связи компонентов — в пер-компонентных `MEMORY.md` (Relations/Facts), напр. transpiler и
  semantic зависят от diag.
