# unittest/cli/CMakeLists.txt

add_executable(cli_unit_tests
    cli_test.cpp
    cliopt_test.cpp
)
target_link_libraries(cli_unit_tests PRIVATE cli_lib GTest::gtest_main)
target_link_trust_includes(cli_unit_tests)
target_include_directories(cli_unit_tests PRIVATE ${CMAKE_BINARY_DIR}/include)
