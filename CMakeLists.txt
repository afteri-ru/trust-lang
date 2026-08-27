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
#     name: prefixed by purpose (trust_cli, stdlib, ...)
#     CMAKE_RUNTIME_OUTPUT_DIRECTORY = ${CMAKE_BINARY_DIR} (all binaries flat).
#
#  5. Test categories
#     Tests are registered with CTest (enable_testing in the root CMakeLists.txt)
#     and run via `ctest --output-on-failure`. Each suite (unit_tests, lit_tests,
#     vscode_*) is a single CTest test; on failure `--output-on-failure` prints
#     the full report including the failing test names.
#     ┌ unit/  - C++ unit tests (single executable: unit_tests)
#     │           Sources under test/unit/, registered in test/unit/CMakeLists.txt.
#     ├ lit/   - LIT integration tests (lit.cfg.py.in → lit.cfg.py)
#     │           Sources under test/lit/, registered in test/lit/CMakeLists.txt.
#     ├ vscode/ - VSCode extension unit & integration tests
#     │           (run by jest/node, registered in test/vscode/CMakeLists.txt).
#     ├ examples/ - standalone programs (examples/*.src), run one-by-one via
#     │           `trust --run` as a late CTest stage `examples_tests` (after
#     │           unit/lit/vscode, gated via the `examples_deps` fixture). NOT lit tests.
#     ├ integration/ - playground end-to-end chain (after examples; fixture integration_deps).
#     └ package/ - Distribution archive + Debian package tests (test/package/),
#                   registered as the LAST test subdirectory, so they run only once
#                   all prior suites pass (FIXTURES_REQUIRED package_deps).
#
#  6. Debug executables (gdb_debuggee / gdb_main)
#     - Declared in test/unit/CMakeLists.txt (standalone debuggee + GDB harness).
#     - Helper executables; debug tests are not part of the CTest suite.
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
#     ----------------------------------------------------------
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
#     ----------------------------------------------------------
#     Неинтерактивные команды для запуска сборки ИИ-агентом
#     (например, Cline). Все команды выполнять из корня проекта.
#     Избегать шагов, требующих ввода или TTY.
#
#     # 1) Configure - генерация make-файлов (аналог "cmake: configure"
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
#     timeout 600 ctest --test-dir _build --output-on-failure   # все тесты (включая examples - финальная стадия)
#     timeout 600 cmake --build _build --target run_tests       # собрать бинарники + ctest
#
#     # 5a) Финальная стадия examples (examples/*.src - standalone-программы).
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

# -- Compilers --
set(CLANG_VERSION 22)
set(CMAKE_C_COMPILER clang-${CLANG_VERSION} CACHE FILEPATH "" FORCE)
set(CMAKE_CXX_COMPILER clang++-${CLANG_VERSION} CACHE FILEPATH "" FORCE)

project(trust-lang LANGUAGES C CXX)

# -- Common paths --
set(PROJECT_INCLUDE_DIR "${CMAKE_SOURCE_DIR}/include")
set(PROJECT_SOURCE_DIR "${CMAKE_SOURCE_DIR}/src")

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

# -- Linker options --
# -rdynamic нужен для backtrace_symbols() - чтобы видеть имена функций
# в стеке вызовов. Применяем ко всем исполняемым файлам.
set(CMAKE_EXE_LINKER_FLAGS "${CMAKE_EXE_LINKER_FLAGS} -rdynamic")

# -- Options --
option(WITH_TORCH "Enable libtorch support (builds libtensor_cpu.so)" OFF)
option(WITH_SOLVER "Enable SMT solver (Z3) for formal verification" OFF)
option(TRUST_BUILD_VSIX "Build trust-lang VSIX package (requires Node.js)" ON)
option(TRUST_BUILD_DAP "Build trust-dap debug adapter (requires GDB)" OFF)

if(WITH_TORCH)
    set(LIBTORCH_ROOT "${CMAKE_SOURCE_DIR}/contrib/libtorch")
    set(LIBTORCH_INCLUDE_DIRS
        ${LIBTORCH_ROOT}/include
        ${LIBTORCH_ROOT}/include/torch/csrc/api/include
    )
    set(LIBTORCH_LIB_DIRS ${LIBTORCH_ROOT}/lib)
endif()

# -- Module path --
list(APPEND CMAKE_MODULE_PATH "${CMAKE_CURRENT_SOURCE_DIR}/cmake")

# -- Modules --
include(version)

set(TRUST_LICENSE "LGPL+commercial")
set(_trust_ext_dir "${CMAKE_CURRENT_SOURCE_DIR}/include/vscode/extensions/trust-lang")
configure_file("${_trust_ext_dir}/package.json.in" "${_trust_ext_dir}/package.json" @ONLY)
configure_file("${_trust_ext_dir}/package-lock.json.in" "${_trust_ext_dir}/package-lock.json" @ONLY)
include(dependencies)
include(generate_files)
include(generate_parsers)
include(package)

# -- Configuration display --
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

# -- Interface targets --
add_library(trust_include_dirs INTERFACE)
target_include_directories(trust_include_dirs INTERFACE ${PROJECT_INCLUDE_DIR} ${CMAKE_BINARY_DIR}/include)

# -- Unified LLVM system include target --
add_library(trust_llvm_dirs INTERFACE)
target_include_directories(trust_llvm_dirs SYSTEM INTERFACE ${LLVM_INCLUDE_DIRS})

# -- Subdirectories --
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
add_subdirectory(include/formatter)

# -- Общие пути для тестовых данных --
# Определены здесь (до test-поддиректорий), чтобы быть доступными во всех test/CMakeLists.txt
set(TEST_DATA_DIR "${CMAKE_BINARY_DIR}/test_data" CACHE INTERNAL "Test data directory")
set(TEST_SRC      "${CMAKE_SOURCE_DIR}/test/unit/debug/simple_example.src" CACHE INTERNAL "Trust source file")
set(TEST_CPP      "${TEST_DATA_DIR}/.trust/simple_example.cpp" CACHE INTERNAL "Transpiled C++ file")
set(TEST_MAP      "${TEST_DATA_DIR}/.trust/simple_example.map" CACHE INTERNAL "Source map file")
set(TEST_BINARY   "${TEST_DATA_DIR}/simple_example" CACHE INTERNAL "Compiled ELF binary")

# -- Enable CTest (must precede add_test in the test subdirectories) --
enable_testing()

# -- Unit tests --
add_subdirectory(test/unit)

# -- LIT tests --
add_subdirectory(test/lit)

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
add_subdirectory(test/package)

# -- Top-level test targets (определяются ПОСЛЕ всех add_subdirectory, чтобы DEPENDS
# на все тестовые/плейграунд target'ы были валидны) --
# Список тестовых бинарников - ЕДИНСТВЕННОЕ место определения зависимостей тестов.
# Задачи редактора (VSCode tasks.json) ссылаются ТОЛЬКО на готовые target'ы
# (test-binaries / run_tests), а НЕ перечисляют бинарники вручную.
# trust-dap НЕ входит в сборку тестов (собирается отдельно и только при TRUST_BUILD_DAP=ON).
set(_run_tests_targets unit_tests trust trust-lsp trust-playground integration generate_lit_cfg_py)

# Только сборка тестовых бинарников (без запуска ctest).
add_custom_target(test-binaries
    COMMAND ${CMAKE_COMMAND} --build ${CMAKE_BINARY_DIR} --target ${_run_tests_targets}
    DEPENDS ${_run_tests_targets}
    COMMENT "Build test binaries (unit/lit/vscode/integration), no ctest"
)

# Собирает тестовые бинарники и запускает ВСЕ зарегистрированные CTest-тесты.
add_custom_target(run_tests
    COMMAND ${CMAKE_CTEST_COMMAND} --test-dir ${CMAKE_BINARY_DIR} --output-on-failure
    DEPENDS test-binaries
    COMMENT "Build test binaries, then run all tests via CTest (unit + lit + vscode + integration)"
)


# -- Упаковка VS Code расширения (VSIX) --
# Версия для имени пакета берётся из файла VERSION (см. include/vscode/packager/package-extension.sh), а не из
# жёстко заданной include/vscode/extensions/trust-lang/package.json, поэтому имя .vsix всегда
# соответствует актуальной версии проекта (не нужно править вручную при каждом изменении).
# Запуск: cmake --build _build --target extension_vsix
find_program(NPX_EXECUTABLE npx)
if(NPX_EXECUTABLE)
    add_custom_target(extension_vsix
        COMMAND bash ${CMAKE_CURRENT_SOURCE_DIR}/include/vscode/packager/package-extension.sh
        COMMENT "Package Trust VS Code extension VSIX (version from VERSION file)"
        VERBATIM
    )
else()
    message(WARNING "npx not found - target 'extension_vsix' (VS Code extension packaging) is unavailable")
endif()
