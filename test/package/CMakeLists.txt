# test/package/CMakeLists.txt
# Distribution archive / Debian package tests.
#
# Registered in a dedicated subdirectory added LAST (after test/examples and
# test/integration; see the root CMakeLists.txt), so their CTest numbers are the
# final ones. They still run only after the unit/lit/vscode suites pass: those suites
# SET UP the `package_deps` fixture, and these tests REQUIRE it (by analogy with
# integration_tests requiring integration_deps). If any prior suite fails, CTest
# marks them "Not Run".

add_test(NAME package_target_test
    COMMAND ${CMAKE_COMMAND} -P ${CMAKE_BINARY_DIR}/cmake/check_package.cmake
)
set_tests_properties(package_target_test PROPERTIES
    TIMEOUT 600
    FIXTURES_REQUIRED package_deps
)

# -- Debian package test (only when dpkg tooling is available) --
if(DPKG_DEB_EXECUTABLE)
    add_test(NAME deb_package_test
        COMMAND ${CMAKE_COMMAND} -P ${CMAKE_BINARY_DIR}/cmake/check_deb.cmake
    )
    set_tests_properties(deb_package_test PROPERTIES
        TIMEOUT 600
        FIXTURES_REQUIRED package_deps
    )
endif()
