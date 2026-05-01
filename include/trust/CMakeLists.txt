# include/trust/CMakeLists.txt
# C++20 module infrastructure (trust module)

# ── PCM build configuration ──
set(TRUST_PCM_DIR "${CMAKE_CURRENT_BINARY_DIR}")
set(TRUST_PCM "${TRUST_PCM_DIR}/trust.pcm")
set(TRUST_PCM_OBJ "${TRUST_PCM_DIR}/trust_module.o")
set(TRUST_CPPM "${PROJECT_SOURCE_DIR}/include/trust.cppm")

set(TRUST_MODULE_HEADERS
    types/types.hpp
    types/type_info.hpp
    types/forward.hpp
    types/value.hpp
    types/numbers.hpp
    types/integers.hpp
    types/strings.hpp
    types/templates.hpp

    types/rational.hpp
    types/tensors.hpp

    stdlib/buildin.hpp

    gencpp/ast_visitor.hpp
    gencpp/ast.hpp
    gencpp/symbol_table.hpp
    gencpp/ast_builder.hpp
    gencpp/ast_validator.hpp
    gencpp/ast_printer.hpp
    gencpp/cpp_generator.hpp
    gencpp/semantic_analyzer.hpp
)
list(TRANSFORM TRUST_MODULE_HEADERS PREPEND "${PROJECT_INCLUDE_DIR}/")

set(TRUST_MODULE_GENERATED_HEADERS
    ${CMAKE_BINARY_DIR}/include/trust/version.h
)

# Generated parser output files
set(TRUST_PARSER_GEN_DIR "${CMAKE_BINARY_DIR}/include/parser")
set(TRUST_PARSER_GEN_OUTPUTS
    "${TRUST_PARSER_GEN_DIR}/token.gen.hash"
    "${TRUST_PARSER_GEN_DIR}/token.gen.all"
    "${TRUST_PARSER_GEN_DIR}/token.gen.name_all"
    "${TRUST_PARSER_GEN_DIR}/token.gen.flags_all"
    "${TRUST_PARSER_GEN_DIR}/token.gen.lookup_all"
    "${TRUST_PARSER_GEN_DIR}/flex.gen.h"
    "${TRUST_PARSER_GEN_DIR}/token.gen.def"
    "${TRUST_PARSER_GEN_DIR}/parser.gen.y"
)

set(TRUST_COMPILE_OPTIONS
    -std=c++${CMAKE_CXX_STANDARD}
    -I${PROJECT_INCLUDE_DIR}
    -I${TRUST_PCM_DIR}
    -I${CMAKE_BINARY_DIR}/include
)

# Step 1: Precompile module to PCM
add_custom_command(
    OUTPUT "${TRUST_PCM}"
    COMMAND ${CMAKE_CXX_COMPILER} ${TRUST_COMPILE_OPTIONS}
            -x c++-module --precompile
            ${TRUST_CPPM}
            -o ${TRUST_PCM}
    DEPENDS ${TRUST_CPPM} ${TRUST_MODULE_HEADERS} ${TRUST_MODULE_GENERATED_HEADERS} ${TRUST_PARSER_GEN_OUTPUTS}
    COMMENT "Precompiling C++${CMAKE_CXX_STANDARD} module -> ${TRUST_PCM}"
    VERBATIM
)

# Step 2: Compile PCM to object file for linking
add_custom_command(
    OUTPUT "${TRUST_PCM_OBJ}"
    COMMAND ${CMAKE_CXX_COMPILER} ${TRUST_COMPILE_OPTIONS}
            -fmodule-file=trust=${TRUST_PCM}
            -x c++-module
            ${TRUST_CPPM}
            -c -o ${TRUST_PCM_OBJ}
    DEPENDS ${TRUST_PCM} ${TRUST_CPPM}
    COMMENT "Compiling module PCM to object file -> ${TRUST_PCM_OBJ}"
    VERBATIM
)

add_custom_target(trust_pcm_target_all ALL DEPENDS ${TRUST_PCM_OBJ})
add_dependencies(trust_pcm_target_all parser_lib)

# ── Function to attach trust module to a target ──
function(target_trust_module target)
    set(_trust_pcm_path "${CMAKE_BINARY_DIR}/include/trust/trust.pcm")
    set(_trust_pcm_obj "${CMAKE_BINARY_DIR}/include/trust/trust_module.o")
    add_dependencies(${target} trust_pcm_target_all)
    target_sources(${target} PRIVATE ${_trust_pcm_obj})
    set_source_files_properties(${_trust_pcm_obj} PROPERTIES
        EXTERNAL_OBJECT true
        GENERATED true
    )
    target_compile_options(${target} PRIVATE
        -fmodule-file=trust=${_trust_pcm_path}
    )
endfunction()
