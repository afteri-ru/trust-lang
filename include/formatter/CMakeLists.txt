# include/formatter/CMakeLists.txt
# Formatter - pretty-printing исходного текста Trust (шаг PipelineSteps::Format).

set(SYNTAX_GEN_DIR "${CMAKE_BINARY_DIR}/syntax")

# -- Formatter library --
add_library(formatter_lib STATIC
    ${PROJECT_SOURCE_DIR}/formatter/format.cpp
    ${PROJECT_SOURCE_DIR}/formatter/config.cpp
)

target_link_libraries(formatter_lib PUBLIC
    trust_include_dirs
    syntax_lib
    diag_lib
    utils_lib
)

target_include_directories(formatter_lib PRIVATE
    ${CMAKE_BINARY_DIR}/include
    ${SYNTAX_GEN_DIR}
)

# Syntax headers trigger -Wswitch / old-style-cast warnings - suppress them.
target_compile_options(formatter_lib PRIVATE
    -Wno-error=switch
    -Wno-error=old-style-cast
)

add_dependencies(formatter_lib gen_syntax_parsers)
