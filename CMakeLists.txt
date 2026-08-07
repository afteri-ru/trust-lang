# ═══════════════════════════════════════════════════════════════════════
#  Build System Rules
# ═══════════════════════════════════════════════════════════════════════
#
#  1. Source file paths
#     ┌ src/ files   →  ${PROJECT_SOURCE_DIR}/<component>/<file>.cpp
#     │                  (PROJECT_SOURCE_DIR = ${CMAKE_SOURCE_DIR}/src)
#     ├ non-src files →  ${CMAKE_SOURCE_DIR}/<path>/<file>.cpp
#     │                  (tests/, contrib/, etc.)
#     └ Generated     →  ${CMAKE_BINARY_DIR}/<path>/<file>.cpp
#     NO relative paths with ".." in source file lists.
#
#  2. Header directories
#     ┌ Project headers →  trust_include_dirs INTERFACE target
#     │                    (adds ${PROJECT_INCLUDE_DIR} = include/)
#     └ Component-local →  target_include_directories(<lib> PUBLIC/PRIVATE ...)
#
#  3. Libraries (static)
#     name: <component>_lib
#     type: STATIC
#     visibility: PUBLIC propagates upstream, PRIVATE for impl details
#
#  4. Executables (tools)
#     name: prefixed by purpose (trust_cli, stdlib, gencpp_from_ast, ...)
#     CMAKE_RUNTIME_OUTPUT_DIRECTORY = ${CMAKE_BINARY_DIR} (all binaries flat).
#
#  5. Test categories
#     Tests are registered with CTest (enable_testing in the root CMakeLists.txt)
#     and run via `ctest --output-on-failure`. Each suite (unit_tests, lit_tests,
#     vscode_*) is a single CTest test; on failure `--output-on-failure` prints
#     the full report including the failing test names.
#     ┌ unit/  — C++ unit tests (single executable: unit_tests)
#     │           Sources under test/unit/, registered in test/unit/CMakeLists.txt.
#     ├ lit/   — LIT integration tests (lit.cfg.py.in → lit.cfg.py)
#     │           Sources under test/lit/, registered in test/lit/CMakeLists.txt.
#     ├ vscode/ — VSCode extension unit & integration tests
#     │           (run by jest/node, registered in test/vscode/CMakeLists.txt).
#     └ examples/ — standalone programs (examples/*.src), run one-by-one via
#                   `trust --run` as the FINAL CTest stage `examples_tests`
#                   (registered in the root CMakeLists.txt). Runs only after all
#                   prior suites pass (gated via the `examples_deps` fixture);
#                   if any prior suite fails, examples_tests is marked "Not Run".
#                   These are NOT lit tests — individual run/pause programs.
#
#  6. Debug executables (gdb_debuggee / gdb_main)
#     ─ Declared in test/unit/CMakeLists.txt (standalone debuggee + GDB harness).
#     ─ Helper executables; debug tests are not part of the CTest suite.
#     To build manually:  cmake --build _build --target gdb_main gdb_debuggee
#
#  7. Generated files
#     custom_command + custom_target + add_dependencies for ordering.
#
#  8. C++20 modules
#     Separate PCM build via custom_command.
#     target_trust_module() / target_runtime_module() to attach.
#
#  9. Component CMakeLists.txt
#     Each component has exactly one CMakeLists.txt file, located
#     at include/<component>/CMakeLists.txt. This file defines the static
#     library and/or executables for the component. The src/<component>/
#     directory contains only .cpp implementation files, but does not
#     contain its own CMakeLists.txt file.
#     This rule applies only to project COMPONENTS (include/*/). Test
#     directories (test/*) are not components and may have their own
#     CMakeLists.txt layout.
#
# 10. VSCode tasks.json
#     The project root must always contain an up-to-date .vscode/tasks.json.
#     Required tasks:
#     - Project build ("cmake: configure + build (full)")
#       Combine configure and build in a single command (generator: Unix Makefiles,
#       build dir: _build/).
#     - Build only test binaries ("cmake: build test binaries only")
#       (unit_tests, trust, trust-dap, trust-lsp, generate_lit_cfg_py).
#     - All tests via CTest ("test: all via ctest (only failures)")
#     - Direct execution of unit_tests with shuffle and filter
#     There may be more tasks, but these must be present.
#
# 11. Common build commands
#     ──────────────────────────────────────────────────────────
#     # Конфигурация (из корня проекта)
#     cmake -B _build -G "Unix Makefiles"
#     cmake -B _build -G "Unix Makefiles" -DWITH_TORCH=ON   # с Torch
#
#     # Сборка всего проекта
#     cmake --build _build
#
#     # Сборка отдельных библиотек
#     cmake --build _build --target <component>_lib         # e.g. ast_lib, diag_lib
#
#     # Сборка исполняемых файлов
#     cmake --build _build --target <name>                  # e.g. trust, trust-dap, trust-lsp
#
#     # Сборка и запуск тестов
#     cmake --build _build --target unit_tests             # сборка unit-тестов (без запуска)
#     ctest --test-dir _build --output-on-failure          # запуск ВСЕХ тестов (GTest + lit + vscode + examples)
#     ctest --test-dir _build --output-on-failure -R 'lit_tests'   # только lit
#     ctest --test-dir _build --output-on-failure -R 'vscode_'     # только vscode
#     ctest --test-dir _build --output-on-failure -R '^examples_tests$'  # финальная стадия examples (фикстуры прогонят и предыдущие суиты)
#     cmake --build _build --target run_tests              # собрать бинарники + запустить ctest (все тесты)
#
#     # Генерация парсеров (bison + flex) в _build/syntax
#     cmake --build _build --target gen_syntax_parsers
#
#     # Очистка артефактов сборки (исходники не затрагиваются)
#     cmake --build _build --target clean
#
# 12. AI agent build commands
#     ──────────────────────────────────────────────────────────
#     Неинтерактивные команды для запуска сборки ИИ-агентом
#     (например, Cline). Все команды выполнять из корня проекта.
#     Избегать шагов, требующих ввода или TTY.
#
#     # 1) Configure — генерация make-файлов (аналог "cmake: configure"
#     #    из .vscode/tasks.json). Опции задаются здесь.
#     cmake -B _build -G "Unix Makefiles"
#     cmake -B _build -G "Unix Makefiles" -DWITH_TORCH=ON   # с опциями
#
#     # 2) Configure + build одной командой (полный цикл с нуля)
#     cmake -B _build -G "Unix Makefiles" && cmake --build _build -j"$(nproc)"
#
#     # 3) Инкрементальная параллельная сборка (после изменения кода)
#     cmake --build _build -j"$(nproc)"
#
#     # 4) Сборка конкретной цели (библиотека/исполняемый/тесты)
#     cmake --build _build --target ast_lib -j"$(nproc)"
#     cmake --build _build --target trust -j"$(nproc)"
#     cmake --build _build --target unit_tests -j"$(nproc)"
#
#     # 5) Запуск тестов ОБЯЗАТЕЛЬНО с timeout (антизависание):
#     timeout 600 ctest --test-dir _build --output-on-failure   # все тесты (включая examples — финальная стадия)
#     timeout 300 cmake --build _build --target run_tests       # собрать бинарники + ctest
#
#     # 5a) Финальная стадия examples (examples/*.src — standalone-программы).
#     #     Выполнится только если все предыдущие суиты прошли (fixture examples_deps);
#     #     иначе ctest пометит examples_tests как "Not Run".
#     #     Примечание: из-за фикстуры -R прогонит также setup-суиты (unit/lit/vscode/package).
#     timeout 600 ctest --test-dir _build --output-on-failure -R '^examples_tests$'
#
#     # 6) Переконфигурация (обычно выполняется сама через
#     #    CMAKE_CONFIGURE_DEPENDS после правки CMakeLists/терминалов)
#     cmake -B _build -G "Unix Makefiles"
#
#     # 7) Очистка артефактов сборки (не трогает исходники)
#     cmake --build _build --target clean
#
#     # Требования к окружению (см. cmake/dependencies.cmake):
#     #   clang/clang++-22, CMake >= 3.28, GTest, flex, bison, lit,
#     #   libzstd, gmp, nlohmann_json, CLI11, msgpack-c (static), LLVM 22.
#     # Опции конфигурации (задаются на шаге configure):
#     #   -DWITH_TORCH=ON        libtorch (tensor_cpu plugin)
#     #   -DWITH_SOLVER=ON       SMT solver Z3
#     #   -DTRUST_BUILD_VSIX=OFF  отключить сборку .vsix (требует Node.js/npm)
#
# ═══════════════════════════════════════════════════════════════════════

cmake_minimum_required(VERSION 3.28)

# ── Compilers ──
set(CLANG_VERSION 22)
set(CMAKE_C_COMPILER clang-${CLANG_VERSION} CACHE FILEPATH "" FORCE)
set(CMAKE_CXX_COMPILER clang++-${CLANG_VERSION} CACHE FILEPATH "" FORCE)

project(trust-lang LANGUAGES C CXX)

# ── Common paths ──
set(PROJECT_INCLUDE_DIR "${CMAKE_SOURCE_DIR}/include")
set(PROJECT_SOURCE_DIR "${CMAKE_SOURCE_DIR}/src")

# ── Global settings ──
set(CMAKE_CXX_STANDARD 23)
set(CMAKE_CXX_STANDARD_REQUIRED ON)
set(CMAKE_CXX_EXTENSIONS OFF)
set(CMAKE_EXPORT_COMPILE_COMMANDS ON)

# ── Symlink compile_commands.json into project root for clang-tidy ──
if(NOT EXISTS "${CMAKE_SOURCE_DIR}/compile_commands.json")
    file(CREATE_LINK "${CMAKE_BINARY_DIR}/compile_commands.json"
         "${CMAKE_SOURCE_DIR}/compile_commands.json" SYMBOLIC)
endif()
set(CMAKE_RUNTIME_OUTPUT_DIRECTORY ${CMAKE_BINARY_DIR})
set(CMAKE_LIBRARY_OUTPUT_DIRECTORY ${CMAKE_BINARY_DIR})

# ── Strict compile options ──
# NOTE: These flags apply to ALL project code via add_compile_options.
# Exceptions are made for:
#   - System/external headers: marked as SYSTEM include directories
#     (see cmake/dependencies.cmake and component CMakeLists.txt)
#   - Generated files (lex.yy.cpp, parser.tab.cpp): use
#     set_source_files_properties(... COMPILE_OPTIONS "-Wno-old-style-cast")
#     (see include/parser/CMakeLists.txt, test/unit/CMakeLists.txt)
#   - test/unit/unit_tests: additional -Wno-unused-result
add_compile_options(
    -Werror
    -Wold-style-cast
    -Wreorder
    -Wnon-virtual-dtor
    -Woverloaded-virtual
)

# ── Linker options ──
# -rdynamic нужен для backtrace_symbols() — чтобы видеть имена функций
# в стеке вызовов. Применяем ко всем исполняемым файлам.
set(CMAKE_EXE_LINKER_FLAGS "${CMAKE_EXE_LINKER_FLAGS} -rdynamic")

# ── Options ──
option(WITH_TORCH "Enable libtorch support (builds libtensor_cpu.so)" OFF)
option(WITH_SOLVER "Enable SMT solver (Z3) for formal verification" OFF)
option(TRUST_BUILD_VSIX "Build trust-lang VSIX package (requires Node.js)" ON)

if(WITH_TORCH)
    set(LIBTORCH_ROOT "${CMAKE_SOURCE_DIR}/contrib/libtorch")
    set(LIBTORCH_INCLUDE_DIRS
        ${LIBTORCH_ROOT}/include
        ${LIBTORCH_ROOT}/include/torch/csrc/api/include
    )
    set(LIBTORCH_LIB_DIRS ${LIBTORCH_ROOT}/lib)
endif()

# ── Module path ──
list(APPEND CMAKE_MODULE_PATH "${CMAKE_CURRENT_SOURCE_DIR}/cmake")

# ── Modules ──
include(version)
include(dependencies)
include(generate_files)
include(generate_parsers)
include(package)

# ── Configuration display ──
message(STATUS "═══════════════════════════════════════════════════")
message(STATUS "Project Configuration:")
message(STATUS "  C++ Compiler: ${CMAKE_CXX_COMPILER}")
message(STATUS "  C Compiler:   ${CMAKE_C_COMPILER}")
message(STATUS "  C++ Standard: ${CMAKE_CXX_STANDARD}")
message(STATUS "  Build Type:   ${CMAKE_BUILD_TYPE}")
message(STATUS "  Build Dir:    ${CMAKE_BINARY_DIR}")
message(STATUS "  lit:          ${LIT_EXECUTABLE}")
message(STATUS "  Flex:         ${FLEX_EXECUTABLE}")
message(STATUS "  nlohmann_json: ${nlohmann_json_VERSION}")
message(STATUS "  LLVM:         ${LLVM_VERSION}")
message(STATUS "  Version:      ${TRUST_VERSION_FULL}")
message(STATUS "═══════════════════════════════════════════════════")

# ── Interface targets ──
add_library(trust_include_dirs INTERFACE)
target_include_directories(trust_include_dirs INTERFACE ${PROJECT_INCLUDE_DIR} ${CMAKE_BINARY_DIR}/include)

# ── Unified LLVM system include target ──
add_library(trust_llvm_dirs INTERFACE)
target_include_directories(trust_llvm_dirs SYSTEM INTERFACE ${LLVM_INCLUDE_DIRS})

# ── Subdirectories ──
add_subdirectory(include/trust)

if(WITH_TORCH)
    add_subdirectory(include/precompiled)
endif()

add_subdirectory(include/location)
add_subdirectory(include/module_loader)
add_subdirectory(include/diag)
add_subdirectory(include/ast)
add_subdirectory(include/types)
add_subdirectory(include/semantic)
add_subdirectory(include/transpiler)
add_subdirectory(include/utils)
add_subdirectory(include/pipeline)
add_subdirectory(include/solver)
add_subdirectory(include/runtime)
add_subdirectory(include/debug)
add_subdirectory(include/lsp)
add_subdirectory(include/playground)
add_subdirectory(include/vscode)
add_subdirectory(include/syntax)

# ── Общие пути для тестовых данных ──
# Определены здесь (до test-поддиректорий), чтобы быть доступными во всех test/CMakeLists.txt
set(TEST_DATA_DIR "${CMAKE_BINARY_DIR}/test_data" CACHE INTERNAL "Test data directory")
set(TEST_SRC      "${CMAKE_SOURCE_DIR}/test/unit/debug/simple_example.src" CACHE INTERNAL "Trust source file")
set(TEST_CPP      "${TEST_DATA_DIR}/.trust/simple_example.cpp" CACHE INTERNAL "Transpiled C++ file")
set(TEST_MAP      "${TEST_DATA_DIR}/.trust/simple_example.map" CACHE INTERNAL "Source map file")
set(TEST_BINARY   "${TEST_DATA_DIR}/simple_example" CACHE INTERNAL "Compiled ELF binary")

# ── Enable CTest (must precede add_test in the test subdirectories) ──
enable_testing()

# ── Distribution archive test (uses the package target + check_package.cmake) ──
add_test(NAME package_target_test
    COMMAND ${CMAKE_COMMAND} -P ${CMAKE_BINARY_DIR}/cmake/check_package.cmake
)
# Fixture: examples_tests (final stage) runs only if ALL prior suites pass.
# FIXTURES_SETUP examples_deps also set on unit_tests/lit_tests/vscode_* in their
# subdirectories; see FIXTURES_REQUIRED on examples_tests below.
set_tests_properties(package_target_test PROPERTIES
    TIMEOUT 600
    FIXTURES_SETUP examples_deps
)

# ── Unit tests ──
add_subdirectory(test/unit)

# ── LIT tests ──
add_subdirectory(test/lit)

# ── VSCode extension tests ──
add_subdirectory(test/vscode)

# ── Top-level test target ──
# Builds all test binaries (unit_tests, trust, trust-dap, trust-lsp and the
# generated lit.cfg.py) and then runs every registered CTest test via
# `ctest --output-on-failure`. Each suite (unit_tests, lit_tests, vscode_unit,
# vscode_dap, vscode_lsp) is a single CTest test.
# CTest is the single entry point for running tests.
add_custom_target(run_tests
    COMMAND ${CMAKE_CTEST_COMMAND} --test-dir ${CMAKE_BINARY_DIR} --output-on-failure
    DEPENDS unit_tests trust trust-dap trust-lsp generate_lit_cfg_py
    COMMENT "Run all tests via CTest (unit + lit + vscode)"
)

# ── Examples as a final CTest stage ──
# examples/ contains standalone programs (run/pause individually, e.g. for
# debugging) — NOT lit tests. Each is run one-by-one via `trust --run`.
# Registered in test/examples LAST (after unit/lit/vscode subdirs) and gated by
# the `examples_deps` fixture, so CTest numbers it sequentially last and runs it
# only after ALL prior suites (package_target_test, unit_tests, lit_tests, vscode_*).
add_subdirectory(test/examples)

# ── Playground integration (после examples) ──
# Реальная цепочка server→worker→lsp→worker→server. Запускается только после
# examples_tests (фикстура playground_integration_deps).
add_subdirectory(test/playground_integration)
