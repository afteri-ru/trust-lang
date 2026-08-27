# include/vscode/CMakeLists.txt
# VS Code Extension Build System

# Версия и хеш берутся из cmake/version.cmake (включён в корневом CMakeLists.txt ДО
# этого подкаталога): TRUST_VERSION, TRUST_GIT_HASH, TRUST_VERSION_FULL.
# Не дублируем чтение VERSION и git rev-parse здесь - см. AGENTS.md (single source of truth).
# TRUST_VERSION_FULL = "X.Y.Z-<git_hash>" и используется и для имени VSIX, и для встроенной
# версии (package.json + extension.vsixmanifest), чтобы поле Installation -> Version
# в Extensions view отображало версию с хешем сборки.

set(VSIX_ID "trust-lang")

# -- Paths --
set(VSIX_STAGING "${CMAKE_BINARY_DIR}/_vsix_staging/trust-lang/extension")
set(VSIX_SOURCE_DIR "${PROJECT_INCLUDE_DIR}/vscode")
set(VSIX_PACKAGER_DIR "${VSIX_SOURCE_DIR}/packager")
# Distribution artifacts (tarball + .vsix) live in a dedicated _build/dist directory.
set(VSIX_DIST_DIR "${CMAKE_BINARY_DIR}/dist")
file(MAKE_DIRECTORY "${VSIX_DIST_DIR}")
set(VSIX_FILE "${VSIX_DIST_DIR}/trust-lang-${TRUST_VERSION_FULL}.vsix")

# -- Stage extension sources --
add_custom_target(copy-vscode-sources
    COMMENT "Staging trust-lang extension sources"
    COMMAND ${CMAKE_COMMAND} -E remove_directory ${VSIX_STAGING}
    COMMAND ${CMAKE_COMMAND} -E make_directory ${VSIX_STAGING}
    COMMAND ${CMAKE_COMMAND} -E copy_directory
        ${VSIX_SOURCE_DIR}/extensions/trust-lang
        ${VSIX_STAGING}
    COMMAND ${CMAKE_COMMAND} -E copy_directory
        ${VSIX_SOURCE_DIR}/syntaxes
        ${VSIX_STAGING}/syntaxes
    # Тема собирается из базы Default Light+ (snapshot) + Trust-переопределений, чтобы быть
    # самодостаточной (НЕ зависеть от `extends`, который в части окружений не срабатывает и
    # оставляет C++/другие языки монохромными). См. packager/merge_theme.py.
    COMMAND ${CMAKE_COMMAND} -E env python3
        ${VSIX_PACKAGER_DIR}/merge_theme.py
        ${VSIX_SOURCE_DIR}/extensions/trust-lang/themes/trust-base-light.json
        ${VSIX_SOURCE_DIR}/extensions/trust-lang/themes/trust-color-theme.src.json
        ${VSIX_STAGING}/themes/trust-color-theme.json
    # Упаковщик package_vsix.cmake НЕ читает .vscodeignore (копирует весь staging), поэтому
    # исходники темы (base + overrides) исключаем здесь - в VSIX идёт только merged-тема.
    COMMAND ${CMAKE_COMMAND} -E remove -f
        ${VSIX_STAGING}/themes/trust-color-theme.src.json
        ${VSIX_STAGING}/themes/trust-base-light.json
)

# -- Set extension version and ID --
add_custom_target(set-vscode-version
    COMMENT "Setting extension version to ${TRUST_VERSION_FULL} and ID to ${VSIX_ID}"
    COMMAND ${CMAKE_COMMAND}
        -D PACKAGE_JSON_FILE=${VSIX_STAGING}/package.json
        -D NEW_VERSION=${TRUST_VERSION_FULL}
        -D NEW_ID=${VSIX_ID}
        -P ${VSIX_PACKAGER_DIR}/set_version.cmake
    DEPENDS copy-vscode-sources
)

# -- Install npm dependencies --
add_custom_target(npm-install-vscode-deps
    COMMENT "Installing npm dependencies for VS Code extension"
    COMMAND npm install --omit=dev --no-audit --no-fund
    WORKING_DIRECTORY ${VSIX_STAGING}
    DEPENDS set-vscode-version
)

# -- Build VSIX package (controlled by TRUST_BUILD_VSIX) --
# `ALL`: пакет собирается как часть обычной сборки (`cmake --build _build`),
# т.е. по Ctrl+B вместе с остальными артефактами. Результат (.vsix) кладётся
# в ${VSIX_DIST_DIR} (_build/dist) - см. package_vsix.cmake (VSIX_OUTPUT_DIR).
# Отключить можно через -DTRUST_BUILD_VSIX=OFF.
if(TRUST_BUILD_VSIX)
    add_custom_target(vsix-trust-lang ALL
        COMMENT "Building trust-lang.vsix"
        COMMAND ${CMAKE_COMMAND}
            -D VSIX_SOURCE_DIR=${VSIX_STAGING}
            -D VSIX_OUTPUT_DIR=${VSIX_DIST_DIR}
            -D VSIX_OUTPUT_FILE=${VSIX_FILE}
            -D VSIX_TEMPLATE_DIR=${VSIX_PACKAGER_DIR}
            -D VSIX_ID=${VSIX_ID}
            -D VSIX_DISPLAY_NAME="Trust Lang"
            -D VSIX_DESCRIPTION="Trust Lang support and debugger."
            -D VSIX_PUBLISHER=rsashka
            -D VSIX_VERSION=${TRUST_VERSION_FULL}
            -P ${VSIX_PACKAGER_DIR}/package_vsix.cmake
        DEPENDS copy-vscode-sources set-vscode-version npm-install-vscode-deps
    )
endif()
