# include/stdlib/core/CMakeLists.txt
# Core standard library package: `stdlib.trust` in share/trust.
# The package is built by the compiler itself: CMake only knows the package directory
# (the one holding trust.toml) and the output archive path - never the file list or the
# archive layout. The layout is described by the manifest ([[layout.layers]]).

set(TRUST_STDLIB_PACKAGE_DIR "${CMAKE_CURRENT_SOURCE_DIR}")
set(TRUST_STDLIB_PACKAGE_ARCHIVE "${CMAKE_BINARY_DIR}/share/trust/stdlib.trust")

add_custom_target(trust_stdlib_package ALL
    COMMAND ${CMAKE_COMMAND} -E make_directory "${CMAKE_BINARY_DIR}/share/trust"
    COMMAND $<TARGET_FILE:trust> --package "${TRUST_STDLIB_PACKAGE_DIR}" -o "${TRUST_STDLIB_PACKAGE_ARCHIVE}"
    DEPENDS trust
    COMMENT "Building standard library package -> ${TRUST_STDLIB_PACKAGE_ARCHIVE}"
    VERBATIM
)

# Self-registration: the distribution aggregate target depends on every registered
# package without a hardcoded list in the root/package scripts.
set_property(GLOBAL APPEND PROPERTY TRUST_PACKAGE_TARGETS trust_stdlib_package)