# test/CMakeLists.txt — Подготовка общих тестовых данных
#
# Набор целей для генерации тестовых артефактов: транспилированный .cpp,
# source-map .map и скомпилированный ELF-бинарник.
#
# Все артефакты собираются в TEST_DATA_DIR.
# Цели используются как зависимые для VSCode-интеграционных тестов (LSP/DAP)
# и могут быть переиспользованы unit-тестами (test/unit).
#
# Пути TEST_SRC/TEST_CPP/TEST_MAP/TEST_BINARY определены в корневом CMakeLists.txt
# как CACHE-переменные, и доступны во всех поддиректориях.

# ── 1. Транспиляция .src → .cpp + .map ──
# Зависит: simple_transpiler (определён в test/unit/CMakeLists.txt)
add_custom_target(simple_transpile
    DEPENDS simple_transpiler
    COMMAND ${CMAKE_COMMAND} -E make_directory "${TEST_DATA_DIR}/.trust"
    COMMAND $<TARGET_FILE:simple_transpiler>
            "${TEST_SRC}"
            --emit-cpp "${TEST_CPP}"
            --emit-source-map "${TEST_MAP}"
            --temp-dir "${TEST_DATA_DIR}"
    COMMENT "Transpile: simple_example.src → .cpp + .map"
)

# ── 2. Компиляция .cpp → ELF бинарник ──
# Зависит: simple_transpile
add_custom_target(simple_compile
    DEPENDS simple_transpile
    COMMAND ${CMAKE_CXX_COMPILER}
            -g -std=c++23 -O0 -fno-pie -no-pie
            -Wno-return-type -Wno-unused-command-line-argument
            -o "${TEST_BINARY}"
            "${TEST_CPP}"
    COMMENT "Compile: simple_example.cpp → ELF binary"
)

# ── 3. Цель-агрегатор для всех тестов, использующих test_data ──
add_custom_target(prepare_test_data
    DEPENDS simple_compile
    COMMENT "Prepare test data: transpile + compile simple_example.src"
)

