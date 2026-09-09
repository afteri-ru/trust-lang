# include/precompiled/CMakeLists.txt
# Precompiled headers: libtorch (WITH_TORCH) and the project PCH (TRUST_USE_PCH).
#
# trust_pch is ALWAYS defined as an INTERFACE target, so component libraries can link it
# unconditionally; with TRUST_USE_PCH=OFF it carries no options and no PCH is built.
# Release builds must use TRUST_USE_PCH=OFF (enforced in the root CMakeLists.txt).

if(WITH_TORCH)
    # Build include flags string
    string(REPLACE ";" ";-I" TORCH_PCH_INCLUDE_FLAGS "-I${LIBTORCH_INCLUDE_DIRS}")

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
    target_include_directories(torch_pch SYSTEM INTERFACE ${LIBTORCH_INCLUDE_DIRS})
    target_compile_options(torch_pch INTERFACE -include-pch ${TORCH_PCH_OUTPUT})
    add_dependencies(torch_pch torch_pch_target)
endif()

# ── Project PCH (opt-in: TRUST_USE_PCH; mandatory OFF in release builds) ──────────────
add_library(trust_pch INTERFACE)

if(TRUST_USE_PCH)
    # Mirror the macro defining the traced build mode, so the PCH matches the consumer TUs.
    if(TRUST_ENABLE_TRACE)
        set(_trust_pch_defs -DTRUST_TRACE_ENABLED=1)
    else()
        set(_trust_pch_defs -DTRUST_TRACE_ENABLED=0)
    endif()

    set(TRUST_PCH_INPUT ${CMAKE_CURRENT_SOURCE_DIR}/trust_pch.hpp)
    set(TRUST_PCH_OUTPUT ${CMAKE_CURRENT_BINARY_DIR}/trust_pch.hpp.pch)
    set(TRUST_PCH_DEP ${CMAKE_CURRENT_BINARY_DIR}/trust_pch.hpp.d)

    # Build the PCH with the SAME language options as the consumer TUs (same standard, same
    # defines, same project include dirs). Deliberately no -fno-modules: the PCH is linked only
    # to non-module targets (module consumers such as unit_tests are excluded).
    # -MD -MF + DEPFILE: track every transitively included header, so the PCH is rebuilt
    # (and all consumer TUs recompiled) whenever a header it covers changes - a stale PCH would
    # silently produce wrong builds.
    add_custom_command(
        OUTPUT ${TRUST_PCH_OUTPUT}
        COMMAND ${CMAKE_CXX_COMPILER} -x c++-header
            -I${PROJECT_INCLUDE_DIR}
            -I${PROJECT_INCLUDE_DIR}/runtime
            -I${CMAKE_BINARY_DIR}/include
            ${_trust_pch_defs}
            -std=c++${CMAKE_CXX_STANDARD}
            -MD -MF ${TRUST_PCH_DEP}
            -c ${TRUST_PCH_INPUT}
            -o ${TRUST_PCH_OUTPUT}
        DEPENDS ${TRUST_PCH_INPUT}
        DEPFILE ${TRUST_PCH_DEP}
        COMMENT "Building project PCH (C++${CMAKE_CXX_STANDARD})"
        VERBATIM
    )

    add_custom_target(trust_pch_target DEPENDS ${TRUST_PCH_OUTPUT})
    add_dependencies(trust_pch trust_pch_target)
    target_compile_options(trust_pch INTERFACE -include-pch ${TRUST_PCH_OUTPUT})
endif()
