# cmake/generate_files.cmake

configure_file(
    ${CMAKE_CURRENT_SOURCE_DIR}/include/trust/version.h.in.cmake
    ${CMAKE_BINARY_DIR}/include/trust/version.h
    @ONLY
)

# Generate makefile_build.hpp with the Makefile template embedded as a C++ string
configure_file(
    ${CMAKE_CURRENT_SOURCE_DIR}/include/pipeline/makefile_build.hpp.in
    ${CMAKE_BINARY_DIR}/include/pipeline/makefile_build.hpp
    @ONLY
)
add_custom_target(generate_makefile_header
    DEPENDS "${CMAKE_BINARY_DIR}/include/pipeline/makefile_build.hpp"
)

configure_file(
    ${CMAKE_CURRENT_SOURCE_DIR}/test/lit/lit.cfg.py.in
    ${CMAKE_BINARY_DIR}/test/lit/lit.cfg.py
    @ONLY
)
add_custom_target(generate_lit_cfg_py
    DEPENDS "${CMAKE_BINARY_DIR}/test/lit/lit.cfg.py"
)