# include/types/CMakeLists.txt
# Types library — type system definitions

add_library(types_lib STATIC
    ${PROJECT_SOURCE_DIR}/src/types/types.cpp
    ${PROJECT_SOURCE_DIR}/src/types/convert.cpp
    ${PROJECT_SOURCE_DIR}/src/types/numbers.cpp
    ${PROJECT_SOURCE_DIR}/src/types/integers.cpp
    ${PROJECT_SOURCE_DIR}/src/types/strings.cpp
    ${PROJECT_SOURCE_DIR}/src/types/templates.cpp
    ${PROJECT_SOURCE_DIR}/src/types/tensors.cpp
    ${PROJECT_SOURCE_DIR}/src/types/rational.cpp
)

target_link_libraries(types_lib PUBLIC diag_lib gmp_interface trust_include_dirs)

# Generate stdlib headers before building types_lib
add_dependencies(types_lib stdlib_run)
