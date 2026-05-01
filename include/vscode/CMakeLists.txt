# include/vscode/CMakeLists.txt
# VS Code Extension Build System

# ── Read version from root VERSION file and git hash ──
file(READ "${PROJECT_SOURCE_DIR}/VERSION" PROJECT_VERSION_RAW)
string(STRIP "${PROJECT_VERSION_RAW}" PROJECT_VERSION)

execute_process(
    COMMAND git rev-parse --short HEAD
    WORKING_DIRECTORY ${PROJECT_SOURCE_DIR}
    OUTPUT_VARIABLE VSIX_GIT_HASH
    OUTPUT_STRIP_TRAILING_WHITESPACE
)

set(VSIX_ID "${VSIX_GIT_HASH}")

# ── Paths ──
set(VSIX_STAGING "${CMAKE_BINARY_DIR}/_vsix_staging/trust-lang/extension")
set(VSIX_SOURCE_DIR "${PROJECT_SOURCE_DIR}/include/vscode")
set(VSIX_PACKAGER_DIR "${VSIX_SOURCE_DIR}/packager")
set(VSIX_FILE "${CMAKE_BINARY_DIR}/trust-lang.vsix")

# ── Stage extension sources ──
add_custom_target(copy-vscode-sources
    COMMENT "Staging trust-lang extension sources"
    COMMAND ${CMAKE_COMMAND} -E remove_directory ${VSIX_STAGING}
    COMMAND ${CMAKE_COMMAND} -E make_directory ${VSIX_STAGING}
    COMMAND ${CMAKE_COMMAND} -E copy_directory
        ${VSIX_SOURCE_DIR}/extensions/trust-lang
        ${VSIX_STAGING}
    COMMAND ${CMAKE_COMMAND} -E make_directory ${VSIX_STAGING}/syntaxes
    COMMAND ${CMAKE_COMMAND} -E copy_directory
        ${VSIX_SOURCE_DIR}/syntaxes
        ${VSIX_STAGING}/syntaxes
)

# ── Set extension version and ID ──
add_custom_target(set-vscode-version
    COMMENT "Setting extension version to ${PROJECT_VERSION} and ID to ${VSIX_ID}"
    COMMAND ${CMAKE_COMMAND}
        -D PACKAGE_JSON_FILE=${VSIX_STAGING}/package.json
        -D NEW_VERSION=${PROJECT_VERSION}
        -D NEW_ID=${VSIX_ID}
        -P ${VSIX_PACKAGER_DIR}/set_version.cmake
    DEPENDS copy-vscode-sources
)

# ── Build VSIX package ──
add_custom_target(vsix-trust-lang ALL
    COMMENT "Building trust-lang.vsix"
    COMMAND ${CMAKE_COMMAND}
        -D VSIX_SOURCE_DIR=${VSIX_STAGING}
        -D VSIX_OUTPUT_DIR=${CMAKE_BINARY_DIR}
        -D VSIX_OUTPUT_FILE=${VSIX_FILE}
        -D VSIX_TEMPLATE_DIR=${VSIX_PACKAGER_DIR}
        -D VSIX_ID=${VSIX_ID}
        -D VSIX_DISPLAY_NAME="Trust Lang"
        -D VSIX_PUBLISHER=trust-lang
        -D VSIX_VERSION=${PROJECT_VERSION}
        -P ${VSIX_PACKAGER_DIR}/package_vsix.cmake
    DEPENDS copy-vscode-sources set-vscode-version
)