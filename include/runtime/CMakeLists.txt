# include/runtime/CMakeLists.txt
# C++20 module infrastructure (trust.runtime module)

# Runtime-заголовки: `runtime/trust/` — намеренно публикуемый ABI-каталог
# (include/trust/*.hpp в пакете), поэтому он в PUBLIC_DIRS, а не в INTERNAL_DIRS.
# `runtime/torch/` — compiler-side torch-адаптер (`TorchTensor`), в пакет не входит и
# другими компонентами не включается (internal-зона). Rational/BigInteger — header-only в
# stdlib (`stdlib/builtin/*.hpp`), TU рантайма они больше не содержат. Компилируемая
# реализация tensor собирается в плагин tensor_cpu; её TU помечается явно.
trust_component(runtime
    INTERFACE
    INTERNAL_DIRS torch
    PUBLIC_DIRS trust
    LINK_PUBLIC trust_include_dirs
)

trust_claim_sources(runtime/tensor.cpp)

# -- PCM build configuration --
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

# -- Library: trust-runtime (shared trust-runtime.so + static trust-runtime.a) --
# One runtime implementation built in two flavours: the dynamic library and the static library.
# The shared one is produced as trust-runtime.so, the static one as trust-runtime.a (both under the
# same OUTPUT_NAME 'trust-runtime', PREFIX ""). Public runtime headers (e.g. trust/assert.hpp)
# are NOT embedded into the libraries: they are delivered by the standard library package
# (share/trust/stdlib.trust, include/trust/). Generated programs link against the static library
# by default (the executable carries the runtime code and needs no trust-runtime.so at run time).
function(trust_add_runtime_flavour _variant)
    set(_srcs
        ${TRUST_SRC_DIR}/utils/io/io.cpp
        ${TRUST_SRC_DIR}/utils/proc/backtrace.cpp
    )
    if(_variant STREQUAL "SHARED")
        set(_target trust_runtime)
        set(_outdir_prop LIBRARY_OUTPUT_DIRECTORY)
    else()
        set(_target trust_runtime_static)
        set(_outdir_prop ARCHIVE_OUTPUT_DIRECTORY)
    endif()
    add_library(${_target} ${_variant} ${_srcs})
    # Заголовки встроенных типов публикуются как "trust/<h>.hpp" (include/runtime/trust/...):
    # include/runtime нужен ТОЛЬКО для компиляции самой runtime-библиотеки (реализация по
    # умолчанию). В общие include-пути компилятора он не входит: переопределяемые заголовки
    # не должны быть доступны компилятору как обычные файлы.
    target_include_directories(${_target} PRIVATE ${PROJECT_INCLUDE_DIR} ${PROJECT_INCLUDE_DIR}/runtime)
    target_link_libraries(${_target} PUBLIC gmp_interface)
    set_target_properties(${_target} PROPERTIES
        OUTPUT_NAME "trust-runtime"
        PREFIX ""
        ${_outdir_prop} ${CMAKE_BINARY_DIR}
    )
    # The trust CLI needs the built .so/.a at link time, so building
    # `trust` must also build this flavour.
    add_dependencies(trust ${_target})
endfunction()

trust_add_runtime_flavour(SHARED)
trust_add_runtime_flavour(STATIC)

# -- Library: tensor_cpu (torch-dependent, shared plugin) --
if(WITH_TORCH)
    add_library(tensor_cpu SHARED
        ${TRUST_SRC_DIR}/runtime/tensor.cpp
    )

    target_include_directories(tensor_cpu PUBLIC
        ${PROJECT_INCLUDE_DIR}
    )
    # `trust/tensor.hpp` is delivered as `include/trust/tensor.hpp` (package layout) and
    # includes `trust/tensor_backend.hpp`; inside the source tree those headers live under
    # include/runtime. This directory is needed only to compile the plugin itself.
    target_include_directories(tensor_cpu PRIVATE
        ${PROJECT_INCLUDE_DIR}/runtime
    )
    target_include_directories(tensor_cpu SYSTEM PUBLIC
        ${LIBTORCH_INCLUDE_DIRS}
    )

    target_link_directories(tensor_cpu PUBLIC
        ${LIBTORCH_LIB_DIRS}
    )

    target_link_libraries(tensor_cpu PUBLIC
        runtime_lib
        torch_pch
        torch
        c10
        torch_cpu
    )

    # SHARED library is PIC by default; an explicit -fPIC is NOT added here: the libtorch PCH
    # is built WITH -fPIC (cmake/torch_pch.cmake), и clang отвергает смешивание PCH и TU с
    # разным PIC ("is pie differs"), поэтому флаги PCH и этого TU должны совпадать.
    add_dependencies(tensor_cpu runtime_pcm_target_all)
endif()

# -- Function to attach runtime module to a target --
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