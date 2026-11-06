# MEMORY.md

> scope: cmake
> role: persistent-memory
> last_reviewed: 2026-11-13
> review_period: 30

## Architecture

`cmake/` — build-хелперы, НЕ компоненты: `version`, `package` (+`trust_artifact_target`), `generate_*`,
`component`+`component_manifest`+`internal_dirs`, `dependencies`, `release_checks`/`clean_tree`, `test`,
`run_examples`, PCH (`torch_pch`), политика параллелизма (`parallelism`+`build_jobs`+`trust-build.sh.in`) и
структурные гейты среды сборки (`check_public_api`/`check_internal_boundary`/`check_component_deps`/
`check_subdir_layout` + `guard_common`/`guard_register`).

## Facts and invariants

- **⚠ Параллелизм — одна политика** (`parallelism.cmake`): `jobs = clamp(budget/1100 MiB, 1, cores)`,
  `budget = min(MemAvailable, cgroup-запас, TOTAL)`; RESERVE (3000 MiB) — только без MemAvailable.
  trust-build.sh пересчитывает при каждой сборке, `flock` ПОСЛЕ захвата; `trust-build-jobs.txt` — wrapper.
- **⚠ Файлы статуса сборки — ОДИН каталог** (`STATUS_DIR` = подкаталог каталога сборки, override
  `TRUST_BUILD_STATUS_DIR`): именованный сеанс `<name>` задаёт `<name>.{pid,status,log}`; дерево проекта и /tmp не сорятся.
- **Тест. параллелизм ОТДЕЛЬНО** (`trust_compute_test_jobs`); unit-шарды — TRUST_UNIT_SHARDS, examples — bash.
- **⚠ ccache — сгенерированный wrapper** (`_build/ccache-wrapper.sh`): `set(ENV{CCACHE_*})` при configure не
  доживает до сборки; кэш `<root>/.cache/ccache`; `torch_pch` — без обёртки; link — pass-through.
- **⚠ `torch_pch` только при `WITH_TORCH=ON`** (`-fno-modules`, `-fPIC`): libtorch несовместим с модулями,
  clang отвергает смешивание PCH/TU с разным PIC. Проектные TU — из явных `#include`.
- **⚠ Манифест объявляется ОДИН раз — `trust_component(...)`**, разбирается `trust_manifest_parse`. Ловушки:
  file(READ)+резать комменты (file(STRINGS) рвёт кириллицу); в MATCHES нет `[[:space:]]`.
- **⚠ Подкаталоги — классификация `internal_dirs.cmake`** (longest-prefix по `PUBLIC_DIRS`/`INTERNAL_DIRS`;
  рекурсивны; `INCLUDE_PUBLIC` на корень запрещён). `subdir_layout_guard`: нет TU в корне `src/<c>/`, кроме
  компонентов, объявленных одно-TU в `_root_tu_by_design` (намерение, не размер); allowlist долга только
  сокращается. Корень `include/<c>/` — публичный интерфейс и общая архитектура (README обязателен), детали
  реализации — в internal-подкаталогах (README там не требуется).
- **⚠ В системе сборки — только проверки среды сборки** (AGENTS.md §16): `public_api_guard`,
  `internal_boundary_guard`, `component_deps_guard`, `subdir_layout_guard`. Обвязка — `guard_common.cmake`
  (`guard_components()`/`guard_include_edges()`; begin/end/violation — МАКРОСЫ). **⚠ `component_deps_guard`**:
  include-граф ↔ `target_link_libraries`, провайдер include-корня — компонент с заголовками, отклонения —
  `_known_exceptions`. Проверок архитектурных требований в сборке нет (AGENTS.md §16).
- **⚠ Все тесты — `trust_add_test_suite`** (TIMEOUT обязателен), структурные гейты — `trust_add_guard`. Прямой
  `add_test` запрещён.
- **⚠ `run_tests` НЕ запускать внутри фоновой сессии** (`--background`): обёртка выставляет
  `TRUST_BUILD_INNER=1`, переменная наследуется `ctest`, и самомтест обёртки получает от `--help` справку
  `cmake --build` → ложный FAIL `--help does not document named sessions`. Тесты — только форграунд
  (`timeout 900 make test`); фоном — сборка.
- **⚠ `trust_artifact_target`** — единственная регистрация `package`/`deb`/`vsix` (clean-tree pre-check); тесты
  артефактов проект не пересобирают.
- **⚠ Проверки архитектурных требований удалены из системы сборки** (AGENTS.md §16):
  `requirements_matrix.cmake` и маркерные гейты (`backend_purity_guard`, `emission_mapping_guard`,
  `diag_determinism_guard`, `kind_dispatch_guard`, `name_domains_guard`, `ast_slots_guard`,
  `using_namespace_guard`, `requirement_matrix_guard`) — архитектурные требования проверяются только тестами
  (`test/`). Поиск маркеров/подстрок по исходному тексту критерием не является (AGENTS.md §15).

## Decisions

- Build-хелперы и гейты — в `cmake/`; `include/*` — только компоненты, `src/<component>/` — их TU.
- `PROJECT_INCLUDE_DIR`/`TRUST_SRC_DIR` — пути проекта; встроенные переменные CMake не переопределяются.
- Компоненты подключаются автообнаружением (`include/*/CMakeLists.txt`): порядок алфавитный, исключения —
  `trust` (первым) и пакеты `stdlib/*`.

## Relations

- `include/runtime` (`tensor_cpu`) — потребитель `torch_pch`; `test/guards` — фикстуры гейтов;
  корневой `CMakeLists.txt` — единственная точка подключения модулей.
