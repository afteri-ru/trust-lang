# include/types/CMakeLists.txt
# Types library — type system definitions

add_library(types_lib STATIC
    ${PROJECT_SOURCE_DIR}/types/types.cpp
    ${PROJECT_SOURCE_DIR}/types/convert.cpp
    ${PROJECT_SOURCE_DIR}/types/numbers.cpp
    ${PROJECT_SOURCE_DIR}/types/integers.cpp
    ${PROJECT_SOURCE_DIR}/types/strings.cpp
    ${PROJECT_SOURCE_DIR}/types/templates.cpp
    ${PROJECT_SOURCE_DIR}/types/tensors.cpp
    ${PROJECT_SOURCE_DIR}/types/rational.cpp
)

target_link_libraries(types_lib PUBLIC diag_lib gmp_interface trust_include_dirs)

