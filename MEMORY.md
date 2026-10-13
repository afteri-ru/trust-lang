# MEMORY.md

> scope: . (root, project-level)
> role: persistent-memory
> last_reviewed: 2026-11-05
> review_period: 30
> max_size: 13600

## Architecture

TrustLang — транспилятор высокоуровневого языка в C++. Архитектура — в `MEMORY.md` компонентов:
корневой `include/<c>/MEMORY.md` — только публичный API и правила, детали реализации — в `MEMORY.md`
internal-подкаталогов (покрывают и `src/<c>/<internal-dir>/`); это единственный источник истины о
компонентах и ловушках.
Пользовательская документация — `docs/content/ru` (первоисточник), `content/en` — синхронный перевод;
правила документации — `docs/MEMORY.md` и AGENTS.md.

**Отладочная диагностика:** `TRUST_DEBUG(msg)`/`TRUST_SCOPE(...)` (C++) и `@__DEBUG__`/`@__DEBUG_SCOPE__`
(`.src`) под гейтом `TRUST_ENABLE_TRACE` → `TRUST_TRACE_ENABLED`: при 0 C++-макросы вырезаны, `.src` дают
`Warning` без вывода. `@__DEBUG__` — фильтр, `@__DEBUG_SCOPE__` — дамп всегда; ядро — `utils`.

## Facts and invariants

- **⚠ Видимость на сайте импорта — одно правило:** `types/site_visibility.hpp`
  (`siteVisibilityOf`/`siteKeysFor`) решает «видно ли имя с сайта и под каким ключом»; `SymbolTable`
  применяет решение, `module_loader` даёт ДАННЫЕ сайта (проекция экспорт-набора). `utils::ImportMasks` —
  только разбор/матчинг строки масок. `_::` без доступа группы — невидимо (в т.ч. на сайте, первым
  запустившем анализ тела модуля).
- **Переполнение целых включено по умолчанию** (`-foverflow-check`): знаковые `+ - *`/`+= -= *=` →
  `throw trust::IntMinus` (ловится `{-…-}`). Классификацию решает `semantic` (`Binary::m_overflowCheck`),
  кодоген только читает. **Целочисленные `//`/`%` проверяются ВСЕГДА** (деление на ноль и `INT_MIN / -1` —
  не wrap-арифметика, флаг на них не влияет).
- **Временные переменные создаёт ТОЛЬКО анализатор** (lowering) как реальные `const VarDecl` в AST;
  транспилятор их не синтезирует (исключения — `transpiler/MEMORY.md`).
- **LIT-подкаталоги не перечисляются вручную:** выводятся из реальных `.src`/`.ast` через GLOB_RECURSE
  CONFIGURE_DEPENDS.
- **Опции CLI (GCC/Clang-стиль):** диагностики — `ignore|warning|error`/`-W<name>=<sev>`; поведение —
  `--<name>=<value>`/`-f<name>`. `solver`: severity `-Wsolver=` + `--solver-mode`. Определены ОДИН раз
  (`commonAnalysisOptions`) и применяются центрально (`applyAnalysisArgs`) в CLI и LSP.
- **Встроенный тип — предобъявление, реализация отдельно** (`=` — определение, `::=` — новый тип и для
  встроенного имени запрещён); провайдеры — intrinsic, auto C++ std или внешние. Модель — `types`,
  реализации и пакет `stdlib.trust` — `stdlib/core`, рантайм-заголовки — `runtime`, ресурсы — `package`.
- **Сравнение типов `<~`/`~~`/`~~~` — статическая свёртка в `Bool`:** `<~` — номинальная с наследованием,
  `~~` — утиная по полям, `~~~` — строгая по тождеству; кодоген эмитит константу. Динамической проверки и
  имени типа из строки НЕТ (явная ошибка компиляции); голая `~` — не оператор.
- **Generic-функции/native-import:** `<T> f(...)` → C++ `template` с явными `c_f<...>(...)`;
  `<T> run(...) := %native...;` — алиас без эмиссии, результат выводится унификацией. Type-packs
  `<T...>`/`xs:T...` — только trailing у функций и в пользовательских native class templates. Встроенных
  C++ FFI-синонимов нет: `Tuple<...>` — ошибка.
- **`:Tensor` — Core-тип с провайдером:** предобъявлен (`types`), реализация — `tensor.trust`
  (libtorch-плагин `tensor_cpu`); без `with-torch` — диагностика до линковки.
- **HIR — единственный вход бэкендов, MIR — структура:** семантика материализует факты в поля узлов,
  `buildHir` их клонирует, поэтому codegen и solver не резолвят имена/типы повторно; solver не зависит от
  `semantic`, режимы получает данными.
- **AST неизменяема, производная форма — HIR/MIR:** анализ работает по `(AST, SymbolTable)`; AST и её факты
  не мутируются после семантики (кроме ErrorExpr-recovery), lowering — только на клоне. Диагностики/LSP
  читают pre-lowering AST, кодоген — HIR, flow-анализы — MIR. «Мутированная AST в роли IR» запрещена.
- **⚠ Домены имён — единственные точки:** spelling → trust-имя (`utils::qualifyTrustName`) → storage-ключ
  (`StorageKey`) → C++-имя (`cppName`); модульная идентичность — `ModuleIndex`. Ручной сбор/разбор ключей
  запрещён (гейт `name_domains_guard`); маски импорта — только `utils::ImportMasks`. LSP не показывает
  storage-ключи.
- **Соответствие include-графа графу сборки** — гейт `component_deps_guard`: include покрыт link-замыканием,
  ребро подкреплено include-ами, PUBLIC-ребро используется в публичных заголовках (детали — `cmake/MEMORY.md`).
- **⚠ Идентичность узлов и факты AST↔HIR:** идентичность узла — указатель на неизменяемый узел в пределах
  компиляции; HIR-клон несёт `origin`. Факты для бэкендов материализуются в ПОЛЯХ узлов, side-table по
  `NodeId` запрещён; пользовательские имена резолвит только семантика. `NodeId`/`SymbolId`, межпрогонный
  кэш и сериализация не вводятся (TypeId реестрово-локален).
- **Фазы пайплайна принадлежат `Pipeline`:** semantic → buildHir → `verifyHir` → buildMir →
  `verifyFlowModule` → `ir` (DA/borrow по MIR) → backend-конвейер (данные: стадии `Solver` →
  `Cpp`, см. `pipeline/MEMORY.md`). `SemanticPassRunner` выполняет только семантику.
  MIR — единственный вход и flow-анализов, и верификатора: DA/borrow — штатные потребители MIR (не
  удаляются), их требования входят в контракт MIR.
- **⚠ Видимость имён (default-public):** top-level и `::`/`name::` — exported; `__::` — private (C++
  anonymous namespace); `_::` — protected для файлов той же группы/пакета. Одноимённые экспорты разных
  файлов сосуществуют (модульная идентичность), голое имя становится неоднозначным на сайте использования.
  Детали и ловушки — `module_loader`, `symbols`, `types`.
- **⚠ Модуль — часть идентичности экспорта:** `\a::f` и `\b::f` сосуществуют; ссылка `\path::name` модуль
  НЕ загружает. C++-изоляция модуля — автоматический `namespace <enc>` для всех сущностей
  (entry/`{% %}`/нативные декларации вне); headerless root её не получает.
- **⚠ Префиксы путей — тип первого компонента:** `\file` — файл в корне проекта, `\\module\...` —
  модуль-каталог, `\\\pkg\...` — пакет-архив; позиционной семантики (импортёр/корень программы/глобальный
  корень) нет.

## Decisions

- **Раскладка компонента: корень — только публичный интерфейс.** Публичный API — объявленные заголовки
  корня `include/<c>/` (`PUBLIC_HEADERS`); детали — в подкаталогах `INTERNAL_DIRS`/`PUBLIC_DIRS`
  (рекурсивны, классификация по длиннейшему префиксу — `cmake/internal_dirs.cmake`); документов
  разработки в корне и TU в корне `src/<c>/` у немалых компонентов нет. Гейты: `internal_boundary_guard`
  (чужие internal-подкаталоги), `public_api_guard` (полнота/доступность API), `internal_memory_guard`
  (память: корень — контракт и таблица подкаталогов, детали — в их `MEMORY.md`; необязательна при ≤3
  файлах/≤16 KiB), `subdir_layout_guard` (раскладка, allowlist долга).

- **Порядок CTest:** unit → lit → vscode → examples → integration → package (пакетные — последними;
  REQUIRE-фикстура `package_deps`).
- **Пакеты/`.deb` — только в `Release`** (`TRUST_BUILD_PACKAGE` default ON в release / OFF в dev; при OFF
  нет targets, скриптов и package-тестов); `TRUST_BUILD_VSIX` — так же.
- **Provider-пакеты и link-контракт:** необязательные нативные библиотеки — отдельные `.trust` в
  `share/trust`; линковка demand-driven (извлечение пакета само link-опций не добавляет).
  `trust-runtime`/`gmp`/`tensor_cpu`/`pthread` — `toolchain`/`system` артефакты; async-слой — header-only в
  `stdlib.trust`, `-pthread` ON-USE.
- **Версия по режиму сборки:** `TRUST_VERSION` = SHORT (X.Y.Z) в release / FULL (X.Y.Z-<hash>) в dev;
  predef `@__TRUST_VERSION__`→SHORT, `@__TRUST_VERSION_FULL__`→FULL.
- **Релизная сборка — ветка `release/*` + строгий annotated-тэг `v<X.Y.Z>`:** конфиг `Release` допустим,
  только если HEAD ровно на тэге `v<X.Y.Z>` == VERSION, ветка `release/<MAJOR>.<MINOR>.X` или
  `release/<MAJOR>.X` (либо detached на тэге), дерево полностью чистое и в шапке CHANGELOG.md есть
  `[Release v<X.Y.Z>]`; иначе — ошибка конфигурации.
- **Артефакты `package`/`deb`/VSIX — только из чистого дерева** (вкл. untracked): изменения → ошибка в
  release / warning в dev.
- **TrustLang не владеет оптимизацией:** единственное изменение формы — lowering; производительность —
  C++ компилятор. Safety-проверки обязаны сохраняться (side-effect/`throw`), тесты на `-O2/-O3`.
- **Бэкенд — данные, не `if`:** контракт — internal-подкаталог `pipeline/backend/`
  (`BackendRequest{HIR,MIR,modes,target}`, `CodegenArtifact`), C++/LLVM — реализации; C++-сборка
  (`make`) — слой `toolchain`, не pipeline.
- **Доказательства — слой `verify`:** модель обязательств/отчёт/сертификат `*.trustcert` принадлежат
  `verify`, решатель — сменный `ProofBackend`; гейт `--verify=all` — ошибка на недоказанное
  обязательство (Envelope ≠ доказано). Флаги применяются по cli-имени из `diag/flag_ids.hpp`.

## Relations

- Перекрёстные связи компонентов — в `MEMORY.md` компонентов и их подкаталогов (Relations/Facts).
- Build-хелперы и CI-гейты (PCH libtorch, `cmake/check_*.cmake`) — в `cmake/`; их память —
  `cmake/MEMORY.md`. Каталог `include/*` — только компоненты.
- Компонентные знания обязательны перед планированием: см. AGENTS.md п.11 и `.clinerules/TASK.md`.
