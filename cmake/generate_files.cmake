# cmake/generate_files.cmake
# Configure_file and custom targets for generated files

configure_file(
    ${CMAKE_CURRENT_SOURCE_DIR}/include/trust/version.h.in.cmake
    ${CMAKE_BINARY_DIR}/include/trust/version.h
    @ONLY
)

configure_file(
    ${CMAKE_CURRENT_SOURCE_DIR}/include/pipeline/options.h.in
    ${CMAKE_BINARY_DIR}/include/pipeline/options.h
    @ONLY
)
add_custom_target(generate_options_h
    DEPENDS "${CMAKE_BINARY_DIR}/include/pipeline/options.h"
)

configure_file(
    ${CMAKE_CURRENT_SOURCE_DIR}/test/lit/lit.cfg.py.in
    ${CMAKE_BINARY_DIR}/test/lit/lit.cfg.py
    @ONLY
)
add_custom_target(generate_lit_cfg_py
    DEPENDS "${CMAKE_BINARY_DIR}/test/lit/lit.cfg.py"
)