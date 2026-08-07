# cmake/package.cmake
# Distribution archive target.
#
# Produces a self-contained tarball for installation/distribution with an
# identifier that embeds the build attributes: version, git hash, target OS and
# architecture, build date. The archive name and the file list are platform
# derived (CMAKE_SYSTEM_NAME / CMAKE_SYSTEM_PROCESSOR), so the same logic works
# for a native Linux build, a WSL2 build and, later, a native Windows build.

# ── Target OS tag (lowercase, short) ──
if(CMAKE_SYSTEM_NAME STREQUAL "Linux")
    set(TRUST_PKG_OS "linux")
elseif(CMAKE_SYSTEM_NAME STREQUAL "Windows")
    set(TRUST_PKG_OS "windows")
elseif(CMAKE_SYSTEM_NAME STREQUAL "Darwin")
    set(TRUST_PKG_OS "darwin")
else()
    string(TOLOWER "${CMAKE_SYSTEM_NAME}" TRUST_PKG_OS)
endif()

# ── Architecture tag (normalized) ──
set(_pkg_arch "${CMAKE_SYSTEM_PROCESSOR}")
if(_pkg_arch MATCHES "x86_64|AMD64|amd64|x64")
    set(TRUST_PKG_ARCH "x86_64")
elseif(_pkg_arch MATCHES "aarch64|arm64|ARM64")
    set(TRUST_PKG_ARCH "aarch64")
else()
    string(TOLOWER "${_pkg_arch}" TRUST_PKG_ARCH)
endif()

# ── Archive identity ──
set(TRUST_PKG_STEM "trust-lang-${TRUST_VERSION_FULL}-${TRUST_PKG_OS}-${TRUST_PKG_ARCH}")
# Distribution artifacts (tarball + .vsix) live in a dedicated _build/dist directory.
set(TRUST_PKG_DIST_DIR "${CMAKE_BINARY_DIR}/dist")
file(MAKE_DIRECTORY "${TRUST_PKG_DIST_DIR}")
set(TRUST_PKG_ARCHIVE "${TRUST_PKG_DIST_DIR}/${TRUST_PKG_STEM}.tar.gz")
set(TRUST_PKG_STAGING "${CMAKE_BINARY_DIR}/package/${TRUST_PKG_STEM}")

message(STATUS "Package stem:  ${TRUST_PKG_STEM}")
message(STATUS "Package arch:  ${TRUST_PKG_ARCHIVE}")

# ── Build-time script (configured once, run via cmake -P at build time) ──
configure_file(
    ${CMAKE_CURRENT_SOURCE_DIR}/cmake/make_package.cmake.in
    ${CMAKE_BINARY_DIR}/cmake/make_package.cmake
    @ONLY
)

# ── Package target ──
add_custom_target(package
    COMMAND ${CMAKE_COMMAND} -P ${CMAKE_BINARY_DIR}/cmake/make_package.cmake
    DEPENDS trust trust-lsp trust-dap trust-playground trust_runtime trust_runtime_static
    COMMENT "Building distribution archive ${TRUST_PKG_ARCHIVE}"
)

# ── Test script (configured here; the add_test registration lives in the root
# CMakeLists.txt after enable_testing(), because add_test requires it) ──
configure_file(
    ${CMAKE_CURRENT_SOURCE_DIR}/cmake/check_package.cmake.in
    ${CMAKE_BINARY_DIR}/cmake/check_package.cmake
    @ONLY
)
