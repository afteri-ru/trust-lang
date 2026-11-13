# cmake/torch_pch.cmake
# Precompiled header for libtorch headers (WITH_TORCH). Project TUs compile from fully explicit
# includes; only libtorch headers are precompiled. Included from the root CMakeLists.txt: a PCH is a
# build helper, not a component, so it lives under cmake/ next to the other build modules.

if(WITH_TORCH)
    string(REPLACE ";" ";-I" TORCH_PCH_INCLUDE_FLAGS "-I${LIBTORCH_INCLUDE_DIRS}")

    set(TORCH_PCH_INPUT ${CMAKE_CURRENT_LIST_DIR}/torch_pch.hpp)
    set(TORCH_PCH_OUTPUT ${CMAKE_BINARY_DIR}/torch_pch.hpp.pch)

    # -fno-modules: libtorch headers are incompatible with the project's C++ modules.
    add_custom_command(
        OUTPUT ${TORCH_PCH_OUTPUT}
        COMMAND ${CMAKE_CXX_COMPILER} -x c++-header
            ${TORCH_PCH_INCLUDE_FLAGS}
            -std=c++${CMAKE_CXX_STANDARD}
            -fPIC
            -fno-modules
            -c ${TORCH_PCH_INPUT}
            -o ${TORCH_PCH_OUTPUT}
        DEPENDS ${TORCH_PCH_INPUT}
        COMMENT "Building libtorch PCH (C++${CMAKE_CXX_STANDARD}, no modules)"
        VERBATIM
    )

    add_custom_target(torch_pch_target DEPENDS ${TORCH_PCH_OUTPUT})

    add_library(torch_pch INTERFACE)
    target_include_directories(torch_pch SYSTEM INTERFACE ${LIBTORCH_INCLUDE_DIRS})
    target_compile_options(torch_pch INTERFACE -include-pch ${TORCH_PCH_OUTPUT})
    add_dependencies(torch_pch torch_pch_target)
endif()
