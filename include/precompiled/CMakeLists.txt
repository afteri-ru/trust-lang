# include/precompiled/CMakeLists.txt
# Precompiled headers configuration for libtorch

if(WITH_TORCH)
    # Include directories for libtorch
    set(TORCH_PCH_INCLUDE_DIRS
        ${CMAKE_SOURCE_DIR}/contrib/libtorch/include
        ${CMAKE_SOURCE_DIR}/contrib/libtorch/include/torch/csrc/api/include
    )

    # Build include flags string
    string(REPLACE ";" ";-I" TORCH_PCH_INCLUDE_FLAGS "-I${TORCH_PCH_INCLUDE_DIRS}")

    # Generate PCH file explicitly
    set(TORCH_PCH_INPUT ${CMAKE_CURRENT_SOURCE_DIR}/torch_pch.hpp)
    set(TORCH_PCH_OUTPUT ${CMAKE_CURRENT_BINARY_DIR}/torch_pch.hpp.pch)

    # Use project's C++ standard for PCH, disable modules to avoid conflicts
    add_custom_command(
        OUTPUT ${TORCH_PCH_OUTPUT}
        COMMAND ${CMAKE_CXX_COMPILER} -x c++-header
            ${TORCH_PCH_INCLUDE_FLAGS}
            -std=c++${CMAKE_CXX_STANDARD}
            -fno-modules
            -c ${TORCH_PCH_INPUT}
            -o ${TORCH_PCH_OUTPUT}
        DEPENDS ${TORCH_PCH_INPUT}
        COMMENT "Building libtorch PCH (C++${CMAKE_CXX_STANDARD}, no modules)"
        VERBATIM
    )

    add_custom_target(torch_pch_target DEPENDS ${TORCH_PCH_OUTPUT})

    # Create interface target for consumers
    add_library(torch_pch INTERFACE)
    target_include_directories(torch_pch INTERFACE ${TORCH_PCH_INCLUDE_DIRS})
    target_compile_options(torch_pch INTERFACE -include-pch ${TORCH_PCH_OUTPUT})
    add_dependencies(torch_pch torch_pch_target)
endif()