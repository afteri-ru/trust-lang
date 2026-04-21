# include/types/CMakeLists.txt
# Types library — type system definitions

if(NOT CMAKE_CURRENT_SOURCE_DIR STREQUAL CMAKE_SOURCE_DIR)
    message(STATUS "[types] Inheriting from parent: compiler=${CMAKE_CXX_COMPILER}, standard=${CMAKE_CXX_STANDARD}")
endif()

add_library(types_lib STATIC
    ${CMAKE_SOURCE_DIR}/src/types/type_info.cpp
)

target_include_directories(types_lib PUBLIC
    ${CMAKE_CURRENT_SOURCE_DIR}/..
)