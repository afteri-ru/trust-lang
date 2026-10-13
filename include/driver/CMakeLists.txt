# include/driver/CMakeLists.txt
# Драйверная модель опций (driver/options.hpp) и arity-aware CLI общих опций
# (driver/cli.hpp, driver/analysis_options.hpp) для бинарников trust/trust-lsp/trust-dap/playground.

trust_component(driver
    PUBLIC_HEADERS analysis_options.hpp cli.hpp options.hpp
    SOURCES cli.cpp
    LINK_PUBLIC diag_lib trust_include_dirs
    LINK_PRIVATE utils_lib
)
