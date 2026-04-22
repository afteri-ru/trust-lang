# include/diag/CMakeLists.txt
# Diag library — диагностика и опции

# Report inherited configuration from parent project
if(NOT CMAKE_CURRENT_SOURCE_DIR STREQUAL CMAKE_SOURCE_DIR)
    message(STATUS "[diag] Inheriting from parent: compiler=${CMAKE_CXX_COMPILER}, standard=${CMAKE_CXX_STANDARD}")
endif()

# ── Библиотека diag_lib ─────────────────────────────────────────────────
add_library(diag_lib STATIC
    ${CMAKE_SOURCE_DIR}/src/diag/context.cpp
    ${CMAKE_SOURCE_DIR}/src/diag/diag.cpp
    ${CMAKE_SOURCE_DIR}/src/diag/options.cpp
)

target_include_directories(diag_lib PUBLIC
    ${CMAKE_CURRENT_SOURCE_DIR}/..
)

# ── Diag unit tests ──
add_executable(diag_unit_tests
    ${CMAKE_SOURCE_DIR}/unittest/diag/diag_test.cpp
    ${CMAKE_SOURCE_DIR}/unittest/diag/options_test.cpp
)
target_link_libraries(diag_unit_tests PRIVATE diag_lib GTest::gtest GTest::gtest_main)
target_include_directories(diag_unit_tests PRIVATE ${CMAKE_SOURCE_DIR}/unittest/diag)
target_link_trust_includes(diag_unit_tests)

add_custom_target(run_diag_unittest
    COMMAND $<TARGET_FILE:diag_unit_tests>
    DEPENDS diag_unit_tests
    WORKING_DIRECTORY ${CMAKE_CURRENT_BINARY_DIR}
    COMMENT "Running diag unit tests..."
    USES_TERMINAL
)
