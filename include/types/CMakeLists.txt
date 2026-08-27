# include/types/CMakeLists.txt
# Types library - type system infrastructure

add_library(types_lib STATIC
    ${PROJECT_SOURCE_DIR}/types/group.cpp
    ${PROJECT_SOURCE_DIR}/types/registry.cpp
    ${PROJECT_SOURCE_DIR}/types/builtin_types.cpp
    ${PROJECT_SOURCE_DIR}/types/structural.cpp
    ${PROJECT_SOURCE_DIR}/types/method_table.cpp
)

target_link_libraries(types_lib PUBLIC diag_lib location_lib trust_include_dirs)