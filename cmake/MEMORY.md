# MEMORY.md

> scope: cmake
> role: persistent-memory
> last_reviewed: 2026-11-06
> review_period: 30
> max_size: 5000

## Architecture

`cmake/` — build-хелперы, а НЕ компоненты: `version`, `package` (+`trust_artifact_target`),
`generate_*`, `component`+`component_manifest`+`internal_dirs`, `dependencies`,
`release_checks`/`clean_tree`, `test`, `run_examples`, PCH (`torch_pch`), политика параллелизма
(`parallelism`+`build_jobs`+`trust-build.sh.in`) и CI-гейты (`check_*` + `guard_common`/`guard_register`).

## Facts and invariants

- **⚠ Параллелизм — одна политика** (`parallelism.cmake`): `jobs = clamp(budget/1100 MiB, 1, cores)`,
  `budget = min(MemAvailable, запас cgroup, TOTAL)`; RESERVE (3000 MiB) — только без MemAvailable.
  Калибровка: пики 0.35/0.92 GiB (clang-22). Cgroup `memory.current`/`high`; нечитаемый — «нет данных».
  trust-build.sh пересчитывает на каждой сборке, `flock` ПОСЛЕ захвата; trust-build-jobs.txt — только wrapper.
- **Тест. параллелизм ОТДЕЛЬНО** (`trust_compute_test_jobs`, `ctest -j` = ядра, TRUST_TEST_JOBS);
  unit-шарды (TRUST_UNIT_SHARDS), examples — bash параллельно.
- **⚠ ccache — сгенерированный wrapper** (`dependencies.cmake`→`_build/ccache-wrapper.sh`):
  `set(ENV{CCACHE_*})` при configure не доживает до сборки; кэш `<root>/.cache/ccache`;
  `torch_pch` — без обёртки; link — pass-through.
- **⚠ `torch_pch` только при `WITH_TORCH=ON`** (`-fno-modules`, `-fPIC`): libtorch несовместим с модулями,
  clang отвергает смешивание PCH/TU с разным PIC. Проектные TU — из явных `#include`.
- **⚠ Манифест объявляется ОДИН раз — `trust_component(...)`** (`component.cmake`), разбирается
  `trust_manifest_parse(<file> <prefix>)` (`component_manifest.cmake`). Ловушки: file(READ)+резать
  комменты (file(STRINGS) рвёт кириллицу); в MATCHES нет `[[:space:]]`.
- **⚠ Подкаталоги — классификация `internal_dirs.cmake`** (longest-prefix по
  `PUBLIC_DIRS`/`INTERNAL_DIRS`; рекурсивны). `INTERNAL_DIRS` — include ИЛИ src;
  `INCLUDE_PUBLIC` на корень запрещён (иначе обход `#include "dap/..."`).
  `subdir_layout_guard`: нет TU в корне `src/<c>/` у немалых компонентов (малый — ≤4 TU/≤32 КиБ/≤4
  заголовка), все подкаталоги src объявлены, в корне include — только MEMORY/README; allowlist долга
  только сокращается (неиспользованная запись — ошибка).
- Гейты: `guard_components()`/`guard_include_edges()` (`guard_common.cmake`); begin/end/violation — МАКРОСЫ.
  **⚠ `component_deps_guard`**: include-граф ↔ `target_link_libraries` (A/B/C; рёбра — манифест + literal
  `target_link_libraries`), провайдер include-корня — компонент с заголовками (`trust` — пространство имён),
  отклонения — `_known_exceptions`.
- **⚠ Все тесты — `trust_add_test_suite`** (TIMEOUT обязателен), гейты — `trust_add_guard`
  (TIMEOUT 300 + `<имя>_selftest` WILL_FAIL). Прямой `add_test` запрещён.
- **⚠ `trust_artifact_target`** (`package.cmake`) — единственная регистрация `package`/`deb`/`vsix`
  (clean-tree pre-check); тесты артефактов проект не пересобирают.
- `internal_memory_guard`: корень — интерфейс, детали — в подкаталогах (рекурсивно); для малой
  реализации опционален (≤3 файлов/≤16 KiB), иначе ≥1000 байт ≤`max_size` с шапкой.

## Decisions

- Build-хелперы и гейты — в `cmake/`; `include/*` — только компоненты, `src/<component>/` — их TU.
- `PROJECT_INCLUDE_DIR`/`TRUST_SRC_DIR` — пути проекта; встроенные переменные CMake (`PROJECT_SOURCE_DIR`)
  не переопределяются.
- Компоненты подключаются автообнаружением (`include/*/CMakeLists.txt`): порядок алфавитный,
  исключения — `trust` (первым) и пакеты `stdlib/*` (нужен готовый target `trust`).

## Relations

- `include/runtime` (`tensor_cpu`) — потребитель `torch_pch`; `test/guards` — фикстуры гейтов;
  корневой `CMakeLists.txt` — единственная точка подключения модулей.
