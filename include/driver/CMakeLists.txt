# include/driver/CMakeLists.txt
# Драйверная модель опций (driver/options.hpp) и arity-aware CLI общих опций
# (driver/cli.hpp, driver/analysis_options.hpp) для бинарников trust/trust-lsp/trust-dap/playground.

add_library(driver_lib STATIC
    ${PROJECT_SOURCE_DIR}/driver/cli.cpp
)

target_link_libraries(driver_lib PUBLIC
    diag_lib
    utils_lib
    trust_include_dirs
)

# Shared precompiled header (opt-in: TRUST_USE_PCH; no-op when OFF).
target_link_libraries(driver_lib PRIVATE trust_pch)
