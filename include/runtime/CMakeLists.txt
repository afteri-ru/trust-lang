# include/runtime/CMakeLists.txt
# C++20 module infrastructure (trust.runtime module)

if(NOT CMAKE_CURRENT_SOURCE_DIR STREQUAL CMAKE_SOURCE_DIR)
    message(STATUS "[runtime] Inheriting from parent: compiler=${CMAKE_CXX_COMPILER}, standard=${CMAKE_CXX_STANDARD}")
endif()

find_package(PkgConfig REQUIRED)
pkg_check_modules(GMP REQUIRED gmp)

# ── PCM build configuration ──
set(RUNTIME_PCM_DIR "${CMAKE_CURRENT_BINARY_DIR}")
set(RUNTIME_PCM "${RUNTIME_PCM_DIR}/trust.runtime.pcm")
set(RUNTIME_PCM_OBJ "${RUNTIME_PCM_DIR}/trust.runtime_module.o")
set(RUNTIME_CPPM "${CMAKE_CURRENT_SOURCE_DIR}/../runtime.cppm")

set(RUNTIME_MODULE_HEADERS
    runtime/rational.hpp
    runtime/var.hpp
)
list(TRANSFORM RUNTIME_MODULE_HEADERS PREPEND "${PROJECT_INCLUDE_DIR}/")

set(RUNTIME_COMPILE_OPTIONS
    -std=c++${CMAKE_CXX_STANDARD}
    -I${PROJECT_INCLUDE_DIR}
    -I${RUNTIME_PCM_DIR}
    -I${GMP_INCLUDE_DIRS}
)

# Step 1: Precompile module to PCM
add_custom_command(
    OUTPUT "${RUNTIME_PCM}"
    COMMAND ${CMAKE_CXX_COMPILER} ${RUNTIME_COMPILE_OPTIONS}
            -x c++-module --precompile
            ${RUNTIME_CPPM}
            -o ${RUNTIME_PCM}
    DEPENDS ${RUNTIME_CPPM} ${RUNTIME_MODULE_HEADERS}
    COMMENT "Precompiling C++${CMAKE_CXX_STANDARD} runtime module -> ${RUNTIME_PCM}"
    VERBATIM
)

# Step 2: Compile PCM to object file for linking
add_custom_command(
    OUTPUT "${RUNTIME_PCM_OBJ}"
    COMMAND ${CMAKE_CXX_COMPILER} ${RUNTIME_COMPILE_OPTIONS}
            -fmodule-file=trust.runtime=${RUNTIME_PCM}
            -x c++-module
            ${RUNTIME_CPPM}
            -c -o ${RUNTIME_PCM_OBJ}
    DEPENDS ${RUNTIME_PCM} ${RUNTIME_CPPM}
    COMMENT "Compiling runtime module PCM to object file -> ${RUNTIME_PCM_OBJ}"
    VERBATIM
)

add_custom_target(runtime_pcm_target_all ALL DEPENDS ${RUNTIME_PCM_OBJ})

# ── Library: runtime_lib (torch-independent, static) ──
# POSITION_INDEPENDENT_CODE is needed because runtime_lib is linked into tensor_cpu.so
add_library(runtime_lib STATIC
    ${CMAKE_SOURCE_DIR}/src/runtime/rational.cpp
    ${CMAKE_SOURCE_DIR}/src/runtime/var_conversions.cpp
)
set_target_properties(runtime_lib PROPERTIES POSITION_INDEPENDENT_CODE ON)

target_include_directories(runtime_lib PUBLIC
    ${PROJECT_INCLUDE_DIR}
    ${GMP_INCLUDE_DIRS}
)

target_link_directories(runtime_lib PUBLIC ${GMP_LIBRARY_DIRS})

target_link_libraries(runtime_lib PUBLIC
    types_lib
    ${GMP_LIBRARIES}
)

add_dependencies(runtime_lib runtime_pcm_target_all)

# ── Library: tensor_cpu (torch-dependent, shared plugin) ──
if(WITH_TORCH)
    add_library(tensor_cpu SHARED
        ${CMAKE_SOURCE_DIR}/src/runtime/tensor.cpp
    )

    set(RUNTIME_TORCH_INCLUDE_DIRS
        ${CMAKE_SOURCE_DIR}/contrib/libtorch/include
        ${CMAKE_SOURCE_DIR}/contrib/libtorch/include/torch/csrc/api/include
    )

    target_include_directories(tensor_cpu PUBLIC
        ${PROJECT_INCLUDE_DIR}
        ${RUNTIME_TORCH_INCLUDE_DIRS}
    )

    target_link_directories(tensor_cpu PUBLIC
        ${CMAKE_SOURCE_DIR}/contrib/libtorch/lib
    )

    target_link_libraries(tensor_cpu PUBLIC
        runtime_lib
        torch
        c10
        torch_cpu
    )

    set_target_properties(tensor_cpu PROPERTIES
        CXX_VISIBILITY_PRESET hidden
        VISIBILITY_INLINES_HIDDEN ON
        LIBRARY_OUTPUT_DIRECTORY "${CMAKE_BINARY_DIR}/plugins"
    )

    # Define plugin directory for development builds
    target_compile_definitions(tensor_cpu PRIVATE
        TRUST_PLUGIN_DIR="${CMAKE_BINARY_DIR}/plugins"
    )

    add_dependencies(tensor_cpu runtime_pcm_target_all)

    message(STATUS "[runtime] WITH_TORCH=ON  →  libtensor_cpu.so will be built")
else()
    message(STATUS "[runtime] WITH_TORCH=OFF  →  libtensor_cpu.so will NOT be built")
endif()

# ── Function to attach runtime module to a target ──
function(target_runtime_module target)
    set(_runtime_pcm_path "${CMAKE_CURRENT_BINARY_DIR}/trust.runtime.pcm")
    set(_runtime_pcm_obj "${CMAKE_CURRENT_BINARY_DIR}/trust.runtime_module.o")
    add_dependencies(${target} runtime_pcm_target_all)
    target_sources(${target} PRIVATE ${_runtime_pcm_obj})
    set_source_files_properties(${_runtime_pcm_obj} PROPERTIES
        EXTERNAL_OBJECT true
        GENERATED true
    )
    target_compile_options(${target} PRIVATE
        -fmodule-file=trust.runtime=${_runtime_pcm_path}
    )
endfunction()