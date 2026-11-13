# cmake/generate_files.cmake

configure_file(
    ${CMAKE_CURRENT_SOURCE_DIR}/include/trust/version.h.in.cmake
    ${CMAKE_BINARY_DIR}/include/trust/version.h
    @ONLY
)

# Generate makefile_build.hpp with the Makefile template embedded as a C++ string
configure_file(
    ${CMAKE_CURRENT_SOURCE_DIR}/include/toolchain/build/makefile_build.hpp.in
    ${CMAKE_BINARY_DIR}/include/toolchain/makefile_build.hpp
    @ONLY
)
add_custom_target(generate_makefile_header
    DEPENDS "${CMAKE_BINARY_DIR}/include/toolchain/makefile_build.hpp"
)

# Компилятор C++ по умолчанию — нейтральный сгенерированный заголовок пакета `trust`
# (не принадлежит `toolchain`: значение нужно CLI/драйверу, а `pipeline` не должен зависеть
# от `toolchain` — E20/R34).
configure_file(
    ${CMAKE_CURRENT_SOURCE_DIR}/include/trust/default_compiler.h.in.cmake
    ${CMAKE_BINARY_DIR}/include/trust/default_compiler.h
    @ONLY
)

configure_file(
    ${CMAKE_CURRENT_SOURCE_DIR}/test/lit/lit.cfg.py.in
    ${CMAKE_BINARY_DIR}/test/lit/lit.cfg.py
    @ONLY
)
add_custom_target(generate_lit_cfg_py
    DEPENDS "${CMAKE_BINARY_DIR}/test/lit/lit.cfg.py"
)