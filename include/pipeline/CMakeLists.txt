# include/pipeline/CMakeLists.txt
# Pipeline - transpiler pipeline library

set(SYNTAX_GEN_DIR "${CMAKE_BINARY_DIR}/syntax")

# -- Pipeline library --
add_library(pipeline_lib STATIC
    ${PROJECT_SOURCE_DIR}/pipeline/pipeline.cpp
    ${PROJECT_SOURCE_DIR}/pipeline/pipeline_parser.cpp
    ${PROJECT_SOURCE_DIR}/pipeline/cli.cpp
    ${PROJECT_SOURCE_DIR}/pipeline/io.cpp
    ${PROJECT_SOURCE_DIR}/pipeline/runtime_locator.cpp
    ${PROJECT_SOURCE_DIR}/pipeline/build.cpp
    ${PROJECT_SOURCE_DIR}/pipeline/run.cpp
    ${PROJECT_SOURCE_DIR}/pipeline/source_map.cpp
    ${PROJECT_SOURCE_DIR}/pipeline/archive.cpp
    ${PROJECT_SOURCE_DIR}/pipeline/module_info.cpp
)
target_link_libraries(pipeline_lib PUBLIC semantic_lib transpiler_lib solver_lib trust_include_dirs)
target_link_libraries(pipeline_lib PRIVATE
    syntax_lib
    module_loader_lib
    formatter_lib
    trust_llvm_dirs
)
target_include_directories(pipeline_lib PRIVATE
    ${CMAKE_BINARY_DIR}/include
    ${SYNTAX_GEN_DIR}
    ${CMAKE_SOURCE_DIR}
)
target_compile_definitions(pipeline_lib PRIVATE
    PROJECT_INCLUDE_DIR="${PROJECT_INCLUDE_DIR}"
)

# pipeline.cpp / source_map.cpp use C23 #embed (trust/dsl.src / LICENSE) -
# suppress the extension warning.
set_source_files_properties(
    ${PROJECT_SOURCE_DIR}/pipeline/pipeline.cpp
    ${PROJECT_SOURCE_DIR}/pipeline/source_map.cpp
    PROPERTIES COMPILE_OPTIONS "-Wno-c23-extensions"
)

# Syntax headers trigger -Wswitch and old-style-cast warnings - suppress them
target_compile_options(pipeline_lib PRIVATE
    -Wno-error=switch
    -Wno-error=old-style-cast
)

add_dependencies(pipeline_lib generate_makefile_header gen_syntax_parsers)

# -- trust executable --
add_executable(trust
    ${PROJECT_SOURCE_DIR}/trust.cpp
)
# After the refactor diag no longer depends on module_loader (ModuleLoader is
# injected into Context as a non-owning pointer from Pipeline) and diag no longer
# owns TypeRegistry creation before Options - so the cyclic dep that required
# --start-group/--end-group is gone. Verified: trust links without the group.
target_link_libraries(trust PRIVATE
    pipeline_lib
    module_loader_lib
    diag_lib
    ast_lib
)