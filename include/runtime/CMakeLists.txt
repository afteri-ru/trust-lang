# include/runtime/CMakeLists.txt
# C++20 module infrastructure (trust.runtime module)

# ── PCM build configuration ──
set(RUNTIME_PCM_DIR "${CMAKE_CURRENT_BINARY_DIR}")
set(RUNTIME_PCM "${RUNTIME_PCM_DIR}/trust.runtime.pcm")
set(RUNTIME_PCM_OBJ "${RUNTIME_PCM_DIR}/trust.runtime_module.o")
set(RUNTIME_CPPM "${CMAKE_CURRENT_SOURCE_DIR}/../runtime.cppm")

set(RUNTIME_MODULE_HEADERS
)
list(TRANSFORM RUNTIME_MODULE_HEADERS PREPEND "${PROJECT_INCLUDE_DIR}/")

# Resolve GMP include dirs from gmp_interface target
get_target_property(_gmp_includes gmp_interface INTERFACE_INCLUDE_DIRECTORIES)

set(RUNTIME_COMPILE_OPTIONS
    -std=c++${CMAKE_CXX_STANDARD}
    -I${PROJECT_INCLUDE_DIR}
    -I${RUNTIME_PCM_DIR}
    -I${_gmp_includes}
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

# ── Library: runtime_lib (header-only) ──
add_library(runtime_lib INTERFACE)
target_link_libraries(runtime_lib INTERFACE types_lib trust_include_dirs)

# ── Library: trust-runtime.so (dynamic) ──
# Runtime implementation as a shared library. Public runtime headers (e.g.
# trust/rational.hpp) are embedded into it via #embed into ELF sections named
# after the header path; the pipeline extracts them back out when needed.
add_library(trust_runtime SHARED
    ${PROJECT_SOURCE_DIR}/runtime/rational.cpp
    ${PROJECT_SOURCE_DIR}/runtime/trust_headers.cpp
    ${PROJECT_SOURCE_DIR}/utils/io.cpp
    ${PROJECT_SOURCE_DIR}/utils/backtrace.cpp
)
target_include_directories(trust_runtime PUBLIC ${PROJECT_INCLUDE_DIR})
target_link_libraries(trust_runtime PUBLIC gmp_interface)
set_target_properties(trust_runtime PROPERTIES
    OUTPUT_NAME "trust-runtime"
    PREFIX ""
    LIBRARY_OUTPUT_DIRECTORY ${CMAKE_BINARY_DIR}
)
# trust_headers.cpp uses C23 #embed — suppress the extension warning
set_source_files_properties(
    ${PROJECT_SOURCE_DIR}/runtime/trust_headers.cpp
    PROPERTIES COMPILE_OPTIONS "-Wno-c23-extensions"
)

# The trust CLI needs the built .so/.a at runtime (extracts embedded headers),
# so building `trust` must also build trust_runtime.
add_dependencies(trust trust_runtime)

# ── Library: trust-runtime.a (static) ──
# The same runtime implementation, but as a static library. Generated programs
# are linked against it by default (static linking is self-contained: the
# executable carries the runtime code and needs no trust-runtime.so at run time).
# The public headers are still embedded (via #embed into ELF sections of the
# object members); the pipeline extracts them from the .a archive member.
add_library(trust_runtime_static STATIC
    ${PROJECT_SOURCE_DIR}/runtime/rational.cpp
    ${PROJECT_SOURCE_DIR}/runtime/trust_headers.cpp
    ${PROJECT_SOURCE_DIR}/utils/io.cpp
    ${PROJECT_SOURCE_DIR}/utils/backtrace.cpp
)
target_include_directories(trust_runtime_static PUBLIC ${PROJECT_INCLUDE_DIR})
target_link_libraries(trust_runtime_static PUBLIC gmp_interface)
set_target_properties(trust_runtime_static PROPERTIES
    OUTPUT_NAME "trust-runtime"
    PREFIX ""
    ARCHIVE_OUTPUT_DIRECTORY ${CMAKE_BINARY_DIR}
)
set_source_files_properties(
    ${PROJECT_SOURCE_DIR}/runtime/trust_headers.cpp
    PROPERTIES COMPILE_OPTIONS "-Wno-c23-extensions"
)

# The trust CLI may be configured to link generated programs with the static
# library, so building `trust` must also build trust_runtime_static.
add_dependencies(trust trust_runtime_static)

# ── Library: tensor_cpu (torch-dependent, shared plugin) ──
if(WITH_TORCH)
    add_library(tensor_cpu SHARED
        ${PROJECT_SOURCE_DIR}/runtime/tensor.cpp
    )

    target_include_directories(tensor_cpu PUBLIC
        ${PROJECT_INCLUDE_DIR}
    )
    target_include_directories(tensor_cpu SYSTEM PUBLIC
        ${LIBTORCH_INCLUDE_DIRS}
    )

    target_link_directories(tensor_cpu PUBLIC
        ${LIBTORCH_LIB_DIRS}
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
    )

    target_compile_definitions(tensor_cpu PRIVATE
        TRUST_PLUGIN_DIR="${CMAKE_BINARY_DIR}"
    )

    add_dependencies(tensor_cpu runtime_pcm_target_all)
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