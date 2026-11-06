# ═══════════════════════════════════════════════════════════════════════
#  Build System — краткая справка
#  Соглашения о путях, манифесте компонента, публичном API и PCH проверяются
#  автоматически (trust_component + cmake/check_*.cmake), поэтому здесь их нет.
# ═══════════════════════════════════════════════════════════════════════
#
#  конфигурация:  cmake -B _build -G "Unix Makefiles" [-D<OPT>=...]
#  сборка/тесты:  make build | timeout 900 make test | make test-quiet
#  фон:           _build/trust-build.sh --background <name> [args...],
#                 затем ОДИН вызов --wait <name> (разные интервалы внутри);
#                 файлы сеанса — <build>/.trust-build/<name>.{pid,status,log}
#  параллелизм:   политика в cmake/parallelism.cmake; -j вручную НЕ передаётся
#  компоненты:    include/<component>/CMakeLists.txt = trust_component(...) (манифест)
#  опции:         WITH_TORCH, WITH_SOLVER, TRUST_BUILD_VSIX, TRUST_BUILD_PACKAGE, TRUST_BUILD_DAP,
#                 TRUST_BUILD_DEBUG_INFO (fine|gmlt|none), TRUST_USE_CCACHE, TRUST_CCACHE_MAXSIZE
#  линковка:      lld при наличии (CMAKE_EXE/SHARED_LINKER_FLAGS += -fuse-ld), иначе системный bfd
#
# ═══════════════════════════════════════════════════════════════════════

cmake_minimum_required(VERSION 3.28)

# -- Compilers --
set(CLANG_VERSION 24)
set(CMAKE_C_COMPILER clang-${CLANG_VERSION} CACHE FILEPATH "" FORCE)
set(CMAKE_CXX_COMPILER clang++-${CLANG_VERSION} CACHE FILEPATH "" FORCE)

project(trust-lang LANGUAGES C CXX)

# -- Common paths --
# PROJECT_INCLUDE_DIR / TRUST_SRC_DIR — пути проекта. НЕ переопределяем встроенные
# переменные CMake (PROJECT_SOURCE_DIR принадлежит project() и указывает на корень проекта).
set(PROJECT_INCLUDE_DIR "${CMAKE_SOURCE_DIR}/include")
set(TRUST_SRC_DIR "${CMAKE_SOURCE_DIR}/src")

# -- Global settings --
set(CMAKE_CXX_STANDARD 23)
set(CMAKE_CXX_STANDARD_REQUIRED ON)
set(CMAKE_CXX_EXTENSIONS OFF)
set(CMAKE_EXPORT_COMPILE_COMMANDS ON)

# -- Symlink compile_commands.json into project root for clang-tidy --
if(NOT EXISTS "${CMAKE_SOURCE_DIR}/compile_commands.json")
    file(CREATE_LINK "${CMAKE_BINARY_DIR}/compile_commands.json"
         "${CMAKE_SOURCE_DIR}/compile_commands.json" SYMBOLIC)
endif()
set(CMAKE_RUNTIME_OUTPUT_DIRECTORY ${CMAKE_BINARY_DIR})
set(CMAKE_LIBRARY_OUTPUT_DIRECTORY ${CMAKE_BINARY_DIR})

# -- Strict compile options --
# NOTE: These flags apply to ALL project code via add_compile_options.
# Exceptions are made for:
#   - System/external headers: marked as SYSTEM include directories
#     (see cmake/dependencies.cmake and component CMakeLists.txt)
#   - Generated files (lexer.yy.cpp, parser.yy.cpp): use
#     set_source_files_properties(... COMPILE_OPTIONS "-Wno-old-style-cast")
#     (see include/syntax/CMakeLists.txt, test/unit/CMakeLists.txt)
#   - test/unit/unit_tests: additional -Wno-unused-result
add_compile_options(
    -Werror
    -Wold-style-cast
    -Wreorder
    -Wnon-virtual-dtor
    -Woverloaded-virtual
)

# -- Linker options --
# -rdynamic нужен для backtrace_symbols() - чтобы видеть имена функций
# в стеке вызовов. Применяем ко всем исполняемым файлам.
set(CMAKE_EXE_LINKER_FLAGS "${CMAKE_EXE_LINKER_FLAGS} -rdynamic")

# -- lld linker (fallback на системный bfd) --
# Измерение (Debug, llvm-24): линковка unit_tests bfd ~5.7s против lld ~0.8s.
# Берём АБСОЛЮТНЫЙ путь: clang выводит lld по имени из своего каталога LLVM, но в Debian
# `ld.lld` в PATH нет (есть ld.lld-<версия>), поэтому по имени могут быть расхождения.
find_program(TRUST_LD_LLD NAMES "ld.lld" "ld.lld-${CLANG_VERSION}"
             HINTS "/usr/lib/llvm-${CLANG_VERSION}/bin" "/usr/local/lib/llvm-${CLANG_VERSION}/bin")
if(TRUST_LD_LLD)
    set(CMAKE_EXE_LINKER_FLAGS    "${CMAKE_EXE_LINKER_FLAGS} -fuse-ld=${TRUST_LD_LLD}")
    set(CMAKE_SHARED_LINKER_FLAGS "${CMAKE_SHARED_LINKER_FLAGS} -fuse-ld=${TRUST_LD_LLD}")
    message(STATUS "Linker:     ${TRUST_LD_LLD}")
else()
    message(STATUS "Linker: lld не найден, используется системный линкер (bfd)")
endif()

# -- Module path & version module --
# CMAKE_MODULE_PATH must be set before any include(<cmake module>). cmake/version.cmake is
# included BEFORE the options below because option defaults (TRUST_BUILD_PACKAGE /
# TRUST_BUILD_VSIX) depend on the effective build mode computed there - the SINGLE source of
# truth for TRUST_BUILD_MODE (release == CMAKE_BUILD_TYPE=Release; Debug / unset => dev).
list(APPEND CMAKE_MODULE_PATH "${CMAKE_CURRENT_SOURCE_DIR}/cmake")
include(version)
include(release_checks)

# TRUST_IS_RELEASE is defined in cmake/version.cmake — the single source of truth for the mode.

# Release-consistency gate (configuration time): a release build embeds the clean release
# number, so it is allowed only on a release branch 'release/<MAJOR>.<MINOR>.X' or
# 'release/<MAJOR>.X' (number matches VERSION) with HEAD exactly on the ANNOTATED release tag
# 'v<X.Y.Z>', a fully clean tree (git status --porcelain empty) and a matching '[Release v<X.Y.Z>]'
# entry in CHANGELOG.md (or detached at that tag). Any other git state -> configuration error
# ending with a `git` fix command. Runs only for CMAKE_BUILD_TYPE=Release.
if(TRUST_IS_RELEASE)
    trust_check_release_consistency()
endif()

# -- Options --
option(WITH_TORCH "Enable libtorch support (builds libtensor_cpu.so)" OFF)
# Capability flag for C++ code: CMake's WITH_TORCH is not visible to sources. The
# existing TRUST_HAS_TORCH macro is the single source of truth for "this toolchain was
# built with libtorch" (also used by runtime/tensor.hpp and src/runtime/tensor.cpp).
if(WITH_TORCH)
    add_compile_definitions(TRUST_HAS_TORCH=1)
endif()
option(WITH_SOLVER "Enable SMT solver (Z3) for formal verification" OFF)
# Distribution artifacts (VSIX / archive + .deb) are part of the default build only in release
# builds, so their option defaults follow the release-mode flag; they can still be forced on a
# dev build (-DTRUST_BUILD_VSIX=ON / -DTRUST_BUILD_PACKAGE=ON) to run their tests.
option(TRUST_BUILD_VSIX "Build trust-lang VSIX package (requires Node.js)" ${TRUST_IS_RELEASE})
option(TRUST_BUILD_DAP "Build trust-dap debug adapter (requires GDB)" OFF)
option(TRUST_BUILD_PACKAGE "Build distribution archive and Debian package (and run their tests)" ${TRUST_IS_RELEASE})

# -- Второй компилятор целевой матрицы (R47) --
# Целевая матрица — основной компилятор (clang) + второй (g++), стандарт берётся из целевого
# контракта. Отсутствие компилятора — ОШИБКА конфигурации, а не тихий пропуск lit-кейса
# (AGENTS.md §10); ось отключается только явно: -DTRUST_SECOND_CXX=none.
set(TRUST_SECOND_CXX "" CACHE STRING
    "Путь ко второму компилятору целевой матрицы (по умолчанию auto-detect g++; 'none' — ось отключена)")
if(TRUST_SECOND_CXX STREQUAL "")
    find_program(_trust_second_cxx_auto NAMES g++-13 g++-14 g++-15 g++)
    if(_trust_second_cxx_auto)
        set(TRUST_SECOND_CXX "${_trust_second_cxx_auto}")
    endif()
endif()
if(NOT TRUST_SECOND_CXX STREQUAL "none" AND NOT EXISTS "${TRUST_SECOND_CXX}")
    message(FATAL_ERROR
        "Второй компилятор целевой матрицы не найден (TRUST_SECOND_CXX='${TRUST_SECOND_CXX}').\n"
        "Установите g++ или задайте путь: -DTRUST_SECOND_CXX=/path/to/g++; "
        "отключить ось явно: -DTRUST_SECOND_CXX=none")
endif()

# -- Compiler debug tracing (two levels: C++ TRUST_DEBUG/TRUST_SCOPE and TrustLang @__DEBUG__/@__DEBUG_SCOPE__) --
# TRUST_TRACE_ENABLED=1 in dev builds, 0 in release: at 0 the C++ macros are compiled out entirely
# (arguments not evaluated) and the TrustLang system macros produce no output (a warning instead).
set(_trust_trace_default ON)
if(TRUST_IS_RELEASE)
    set(_trust_trace_default OFF)
endif()
option(TRUST_ENABLE_TRACE "Enable compiler debug tracing (TRUST_TRACE_ENABLED)" ${_trust_trace_default})
if(TRUST_ENABLE_TRACE)
    add_compile_definitions(TRUST_TRACE_ENABLED=1)
else()
    add_compile_definitions(TRUST_TRACE_ENABLED=0)
endif()

# -- C++ debug information (Debug builds) --
# fine — полный -g (по умолчанию, текущее поведение); gmlt — только line-tables (-gmlt: сборка
# быстрее/легче, в отладчике нет локальных переменных); none — без отладочной информации.
set(TRUST_BUILD_DEBUG_INFO "fine" CACHE STRING "C++ debug info level: fine | gmlt | none")
set_property(CACHE TRUST_BUILD_DEBUG_INFO PROPERTY STRINGS fine gmlt none)
if(NOT TRUST_BUILD_DEBUG_INFO MATCHES "^(fine|gmlt|none)$")
    message(FATAL_ERROR
        "TRUST_BUILD_DEBUG_INFO: ожидается fine, gmlt или none (получено '${TRUST_BUILD_DEBUG_INFO}')")
endif()
if(TRUST_BUILD_DEBUG_INFO STREQUAL "gmlt")
    string(REPLACE "-g" "-gmlt" CMAKE_CXX_FLAGS_DEBUG "${CMAKE_CXX_FLAGS_DEBUG}")
elseif(TRUST_BUILD_DEBUG_INFO STREQUAL "none")
    set(CMAKE_CXX_FLAGS_DEBUG "")
endif()

# -- Clean-tree pre-check severity for artifact targets (package / deb / VSIX) --
# Artifacts that embed VERSION/TRUST_VERSION must correspond to a committed state: hard error in
# release builds (artifacts in the default build), warning when force-enabled on a dev build.
# require_clean_tree.cmake.in is configured once here; referenced by cmake/package.cmake and
# include/vscode/CMakeLists.txt.
if(TRUST_IS_RELEASE)
    set(TRUST_CLEAN_SEVERITY "error")
else()
    set(TRUST_CLEAN_SEVERITY "warning")
endif()
configure_file(
    ${CMAKE_CURRENT_SOURCE_DIR}/cmake/require_clean_tree.cmake.in
    ${CMAKE_BINARY_DIR}/cmake/require_clean_tree.cmake
    @ONLY
)


if(WITH_TORCH)
    set(LIBTORCH_ROOT "${CMAKE_SOURCE_DIR}/contrib/libtorch")
    set(LIBTORCH_INCLUDE_DIRS
        ${LIBTORCH_ROOT}/include
        ${LIBTORCH_ROOT}/include/torch/csrc/api/include
    )
    set(LIBTORCH_LIB_DIRS ${LIBTORCH_ROOT}/lib)
endif()

# -- Modules (dependencies / generated files / packaging) --
# (cmake/version.cmake and cmake/release_checks.cmake are included above, before the options.)
set(TRUST_LICENSE "LGPL+commercial")
set(_trust_ext_dir "${CMAKE_CURRENT_SOURCE_DIR}/include/vscode/extensions/trust-lang")
configure_file("${_trust_ext_dir}/package.json.in" "${_trust_ext_dir}/package.json" @ONLY)
configure_file("${_trust_ext_dir}/package-lock.json.in" "${_trust_ext_dir}/package-lock.json" @ONLY)
include(dependencies)
include(generate_files)
include(embed_assets)
include(generate_parsers)
include(package)
include(component)
include(parallelism)
include(test)

# -- Build parallelism: single policy (cmake/parallelism.cmake) --
# The job count is recomputed by the generated `trust-build.sh` on every build: available
# memory and the cgroup headroom at build time differ from the configure-time ones.
# Configure only reports the current value; trust-build-jobs.txt is written EXCLUSIVELY by
# the wrapper (single writer) — a configure-time copy would be stale between builds.
trust_compute_build_jobs(TRUST_BUILD_JOBS_EFFECTIVE)
# Тестовый параллелизм ctest: независим от бюджета памяти сборки (см. parallelism.cmake).
set(TRUST_TEST_JOBS "" CACHE STRING "ctest -j for tests (default: logical cores)")
trust_compute_test_jobs(TRUST_TEST_JOBS_EFFECTIVE)
configure_file(cmake/trust-build.sh.in "${CMAKE_BINARY_DIR}/trust-build.sh" @ONLY)
file(CHMOD "${CMAKE_BINARY_DIR}/trust-build.sh"
     PERMISSIONS OWNER_READ OWNER_WRITE OWNER_EXECUTE GROUP_READ GROUP_EXECUTE WORLD_READ WORLD_EXECUTE)

# -- Configuration display --
message(STATUS "═══════════════════════════════════════════════════")
message(STATUS "Project Configuration:")
message(STATUS "  C++ Compiler: ${CMAKE_CXX_COMPILER}")
message(STATUS "  C Compiler:   ${CMAKE_C_COMPILER}")
message(STATUS "  C++ Standard: ${CMAKE_CXX_STANDARD}")
message(STATUS "  Second CXX:   ${TRUST_SECOND_CXX} (второй компилятор целевой матрицы, R47)")
message(STATUS "  Build Type:   ${CMAKE_BUILD_TYPE}")
message(STATUS "  Build Mode:   ${TRUST_BUILD_MODE} (release|dev)")
message(STATUS "  Build Dir:    ${CMAKE_BINARY_DIR}")
message(STATUS "  Build jobs:   -j${TRUST_BUILD_JOBS_EFFECTIVE} (memory budget ${TRUST_BUILD_MEMORY_BUDGET_MIB} MiB, ${TRUST_BUILD_CORES} cores)")
message(STATUS "  Test jobs:    -j${TRUST_TEST_JOBS_EFFECTIVE} (ctest)")
message(STATUS "  ccache:       ${TRUST_CCACHE_LAUNCHER}")
message(STATUS "  Debug info:   ${TRUST_BUILD_DEBUG_INFO}")
message(STATUS "  lit:          ${LIT_EXECUTABLE}")
message(STATUS "  Flex:         ${FLEX_EXECUTABLE}")
message(STATUS "  nlohmann_json: ${nlohmann_json_VERSION}")
message(STATUS "  tomlplusplus:  ${tomlplusplus_VERSION}")
message(STATUS "  LLVM:         ${LLVM_VERSION}")
message(STATUS "  Version:      ${TRUST_VERSION}")

message(STATUS "═══════════════════════════════════════════════════")

# -- Interface targets --
add_library(trust_include_dirs INTERFACE)
target_include_directories(trust_include_dirs INTERFACE ${PROJECT_INCLUDE_DIR} ${CMAKE_BINARY_DIR}/include)

# -- Unified LLVM system include target --
# `-idirafter` (not `-isystem`): with clang++ over libstdc++ an `-isystem` LLVM dir
# shadows <cxxabi.h> with LLVM's libc++abi header; gtest (which includes <cxxabi.h>) then
# fails with conflicting __cxa_init_primary_exception declarations. LLVM headers remain
# available, but standard-library headers are searched first.
add_library(trust_llvm_dirs INTERFACE)
foreach(_llvm_include IN LISTS LLVM_INCLUDE_DIRS)
    target_compile_options(trust_llvm_dirs INTERFACE "-idirafter" "${_llvm_include}")
endforeach()

# -- Subdirectories (компоненты) --
# Компонент = каталог include/<component>/ с CMakeLists.txt, поэтому список
# НЕ поддерживается вручную: он выводится из дерева (CONFIGURE_DEPENDS). Порядок —
# алфавитный. Бинарь `trust` объявляется ПОСЛЕ цикла (см. ниже): он линкует `builddriver_lib`
# и требуется runtime/stdlib-пакетам, которые добавляются следом.
add_subdirectory(include/trust)

# Precompiled header: torch_pch (libtorch, WITH_TORCH) only; project TUs always compile from fully
# explicit includes. PCH is a build helper, not a component: it lives in cmake/torch_pch.cmake.
include(cmake/torch_pch.cmake)

# -- trust executable (CLI-драйвер) --
# Бинарь объявлен здесь, а НЕ в манифесте компонента: он не компонент, а программа-драйвер,
# линкующая конвейер (`pipeline_lib`) и драйвер сборки (`builddriver_lib`). Объявление вне
# компонентного манифеста — так граница «компиляционный конвейер ↔ toolchain» остаётся в модели
# компонент (component_deps_guard), а не размывается исполняемым файлом.
# Цель создаётся ДО цикла компонент: `runtime`/`stdlib`-пакеты добавляют к ней add_dependencies.
# Связывание с библиотеками — ПОСЛЕ цикла (target_link_libraries ниже).
add_executable(trust ${TRUST_SRC_DIR}/trust.cpp)

file(GLOB _trust_component_dirs CONFIGURE_DEPENDS
    "${CMAKE_CURRENT_SOURCE_DIR}/include/*/CMakeLists.txt")
list(SORT _trust_component_dirs)
foreach(_component_cmake IN LISTS _trust_component_dirs)
    get_filename_component(_component_dir "${_component_cmake}" DIRECTORY)
    get_filename_component(_component_name "${_component_dir}" NAME)
    if(_component_name STREQUAL "trust")
        continue()  # инфраструктура модуля добавлена выше
    endif()
    add_subdirectory("${_component_dir}")
endforeach()

# -- trust executable: линковка (цель создана выше) --
# diag не зависит от module_loader (ModuleLoader внедряется в Context из Pipeline) и не владеет
# TypeRegistry - циклическая зависимость, требовавшая --start-group/--end-group, отсутствует.
target_link_libraries(trust PRIVATE
    builddriver_lib
    pipeline_lib
    module_loader_lib
    diag_lib
    ast_lib
    # src/trust.cpp напрямую включает package/package.hpp: зависимость объявляется явно
    # (pipeline_lib линкует package_lib как PRIVATE — в его публичных заголовках package нет).
    package_lib
)

# Core stdlib and provider packages are built from these subdirectories and delivered as
# `.trust` archives in share/trust. Each package self-registers its target; `trust` already
# exists here (include/pipeline), поэтому пакеты добавляются после всех компонентов.
add_subdirectory(include/stdlib/core)
add_subdirectory(include/stdlib/tensor)

# Каждый .cpp компонента зарегистрирован ровно в одном trust_component(SOURCES ...) —
# ловит TU, выпавшие из сборки после перегруппировки файлов (см. cmake/component.cmake).
trust_component_verify_sources()

# All packages (core stdlib and providers) self-register in TRUST_PACKAGE_TARGETS (see their
# CMakeLists). One aggregate target keeps distribution/test ordering free of a hardcoded
# package list; the archive itself copies the whole share/trust directory.
get_property(TRUST_PACKAGE_TARGETS GLOBAL PROPERTY TRUST_PACKAGE_TARGETS)
add_custom_target(trust_packages ALL)
if(TRUST_PACKAGE_TARGETS)
    add_dependencies(trust_packages ${TRUST_PACKAGE_TARGETS})
endif()

if(TARGET package)
    add_dependencies(package trust_packages)
endif()
if(TARGET deb)
    add_dependencies(deb trust_packages)
endif()

# -- Общие пути для тестовых данных --
# Определены здесь (до test-поддиректорий), чтобы быть доступными во всех test/CMakeLists.txt
set(TEST_DATA_DIR "${CMAKE_BINARY_DIR}/test_data" CACHE INTERNAL "Test data directory")
set(TEST_SRC      "${CMAKE_SOURCE_DIR}/test/unit/debug/simple_example.src" CACHE INTERNAL "Trust source file")
set(TEST_CPP      "${TEST_DATA_DIR}/.trust/simple_example.cpp" CACHE INTERNAL "Transpiled C++ file")
set(TEST_MAP      "${TEST_DATA_DIR}/.trust/simple_example.map" CACHE INTERNAL "Source map file")
set(TEST_BINARY   "${TEST_DATA_DIR}/simple_example" CACHE INTERNAL "Compiled ELF binary")

# TEMPORARY switch for large architecture refactors (AGENTS.md §10.1, "test isolation").
# Default OFF → tests always build and run. While ON, test suites are excluded from the build;
# the switch MUST be flipped back OFF before the stage completes.
option(TRUST_REFACTOR_SKIP_TESTS "TEMPORARY: exclude test suites from the build during a large architecture refactor" OFF)

# The standard library package and all registered provider packages must exist before anything
# that runs the pipeline (product LSP/playground as well as the test harnesses).
add_dependencies(trust-lsp trust_packages)
add_dependencies(trust-playground trust_packages)

if(NOT TRUST_REFACTOR_SKIP_TESTS)
# -- Enable CTest (must precede add_test in the test subdirectories) --
enable_testing()

# -- Unit tests --
add_subdirectory(test/unit)

# -- LIT tests --
add_subdirectory(test/lit)

# -- Link-guard: syntax/ast/session не зависят от module_loader --
add_subdirectory(test/guards)

# -- Структурные гейты среды сборки: раскладка, границы компонент, зависимости, публичный API --
# Проверки архитектурных требований в систему сборки не входят (AGENTS.md §16) — они выполняются тестами.
# Обвязка — cmake/guard_common.cmake, регистрация — cmake/guard_register.cmake.
include(cmake/guard_register.cmake)

# Зависимости кода соответствуют зависимостям сборки: каждый include покрыт link-замыканием
# компонента, каждое ребро подкреплено include-ами, PUBLIC-ребро используется в заголовках.
trust_add_guard(component_deps_guard cmake/check_component_deps.cmake FIXTURE test/guards/fixture_component_deps)

# Публичная граница компонента: другой компонент не включает <comp>/<zone>/...
# (детали реализации) — только заголовки корня компонента.
trust_add_guard(internal_boundary_guard cmake/check_internal_boundary.cmake FIXTURE test/guards/fixture_internal_boundary)

# Публичный API компонента = только объявленные в манифесте заголовки корня; каждый объявленный
# доступен снаружи (или через зонтичный заголовок), каждый подкаталог классифицирован.
trust_add_guard(public_api_guard cmake/check_public_api.cmake FIXTURE test/guards/fixture_public_api)

# Раскладка подкаталогов: реализация вне корня src/<component>/ (кроме компонентов, объявленных
# одно-TU), каждый подкаталог объявлен (INTERNAL_DIRS/PUBLIC_DIRS), корень include/<component>/ —
# публичный интерфейс и общая архитектура, корень компонента не публикуется как include-путь.
trust_add_guard(subdir_layout_guard cmake/check_subdir_layout.cmake FIXTURE test/guards/fixture_subdir_layout)

# Хуки (.clinerules/hooks): классы правил PreToolUse (блокировка/информирование) и запись tasklog.
trust_add_test_suite(hooks_selftest
    TIMEOUT 120
    LABELS hooks
    COMMAND bash ${CMAKE_SOURCE_DIR}/test/hooks/selftest.sh
)

# Обёртка сборки: файлы статуса именованного сеанса — только в подкаталоге каталога сборки.
add_subdirectory(test/build_status)

# -- VSCode extension tests --
add_subdirectory(test/vscode)

# -- Top-level test target -- (см. конец файла: должен определяться ПОСЛЕ всех
# add_subdirectory, чтобы DEPENDS на все тестовые/плейграунд target'ы были валидны)

# -- Examples (после unit/lit/vscode) --
# examples/ contains standalone programs (run/pause individually, e.g. for
# debugging) - NOT lit tests. Each is run one-by-one via `trust --run`.
# Registered here (after unit/lit/vscode) and gated by the `examples_deps` fixture.
add_subdirectory(test/examples)

# -- Playground integration (после examples) --
# Реальная цепочка server→worker→lsp→worker→server. Запускается только после
# examples_tests (фикстура integration_deps).
add_subdirectory(test/integration)

# -- Distribution archive / Debian package tests (see test/package/CMakeLists.txt) --
# Registered in the LAST test subdirectory, so their CTest numbers are the final ones
# (after integration_tests). They still run only after the unit/lit/vscode suites pass
# (FIXTURES_REQUIRED package_deps).
# Built and tested ONLY when TRUST_BUILD_PACKAGE=ON (see cmake/package.cmake); otherwise
# neither the package/deb targets nor these tests exist.
if(TRUST_BUILD_PACKAGE)
    add_subdirectory(test/package)
endif()

# -- Top-level test targets (определяются ПОСЛЕ всех add_subdirectory, чтобы DEPENDS
# на все тестовые/плейграунд target'ы были валидны) --
# Список тестовых бинарников - ЕДИНСТВЕННОЕ место определения зависимостей тестов.
# Задачи редактора (VSCode tasks.json) ссылаются ТОЛЬКО на готовые target'ы
# (test-binaries / run_tests), а НЕ перечисляют бинарники вручную.
# trust-dap НЕ входит в сборку тестов (собирается отдельно и только при TRUST_BUILD_DAP=ON).
set(_run_tests_targets unit_tests trust trust-lsp trust-playground integration generate_lit_cfg_py)

# The standard library package and all registered provider packages must exist before
# anything that runs the pipeline (LSP, playground, unit tests, integration harness).
add_dependencies(unit_tests trust_packages)
add_dependencies(integration trust_packages)

# Только сборка тестовых бинарников (без запуска ctest).
# Обычная агрегирующая цель: сборка идёт ОДНИМ вызовом cmake --build, поэтому политика
# параллелизма (cmake/parallelism.cmake) применяется ровно один раз — в обёртке trust-build.sh.
add_custom_target(test-binaries
    DEPENDS ${_run_tests_targets}
    COMMENT "Test binaries (unit/lit/vscode/integration), no ctest"
)

# Плагин tensor_cpu (WITH_TORCH) входит в `all`, но НЕ в линк-замыкание тестовых бинарников.
# Лит-тест codegen/tensor/run.src загружает его В РАНТАЙМ «рядом с trust» (runtime locator).
# Без явной зависимости сборка только test-binaries после clean дала бы missing plugin.
if(TARGET tensor_cpu)
    add_dependencies(test-binaries tensor_cpu)
endif()

# Собирает тестовые бинарники и запускает ВСЕ зарегистрированные CTest-тесты.
add_custom_target(run_tests
    COMMAND ${CMAKE_CTEST_COMMAND} --test-dir ${CMAKE_BINARY_DIR} --output-on-failure -j ${TRUST_TEST_JOBS_EFFECTIVE}
    DEPENDS test-binaries
    COMMENT "Build test binaries, then run all tests via CTest (unit + lit + vscode + integration)"
)
else()
    message(STATUS "TRUST_REFACTOR_SKIP_TESTS=ON: test suites are excluded from the build (temporary — architecture refactor)")
endif()



