# include/pipeline/CMakeLists.txt
# Pipeline - transpiler pipeline library + CLI executable `trust`.

set(SYNTAX_GEN_DIR "${CMAKE_BINARY_DIR}/syntax")

trust_component(pipeline
    INTERNAL_DIRS archive backend dsl modules paths run sourcemap
    PUBLIC_HEADERS pipeline.hpp cli.hpp commands.hpp
    SOURCES
        pipeline.cpp commands.cpp cli.cpp
        backend/backend_stage.cpp backend/backend_solver.cpp backend/backend_cpp.cpp
        backend/backend_verify.cpp backend/checks.cpp backend/emit.cpp
        dsl/dsl.cpp modules/modules.cpp paths/paths.cpp run/run.cpp sourcemap/source_map.cpp
        archive/archive.cpp
    LINK_PUBLIC
        solver_lib driver_lib symbols_lib ir_lib session_lib semantic_lib trust_include_dirs
    LINK_PRIVATE
        # package/package.hpp + manifest.hpp используются только реализацией pipeline
        # (src/pipeline/**), в публичных заголовках pipeline их нет -> PRIVATE.
        package_lib
        toolchain_lib verify_lib transpiler_lib syntax_lib module_loader_lib
        formatter_lib trust_llvm_dirs
    # build/include (для trust/version.h) приходит транзитивно через trust_include_dirs.
    INCLUDE_PRIVATE ${SYNTAX_GEN_DIR} ${CMAKE_SOURCE_DIR}
    # Syntax headers trigger -Wswitch and old-style-cast warnings - suppress them
    COMPILE_OPTIONS -Wno-error=switch -Wno-error=old-style-cast
    DEPENDS generate_makefile_header gen_syntax_parsers
)

# source_map.cpp uses C23 #embed (LICENSE) - suppress the extension warning.
# Ассеты stdlib здесь не вшиваются: prelude читается из пакета `share/trust/stdlib.trust`
# через package::readResourcePackageEntry (см. pipeline.cpp).
set_source_files_properties(
    ${TRUST_SRC_DIR}/pipeline/sourcemap/source_map.cpp
    PROPERTIES COMPILE_OPTIONS "-Wno-c23-extensions"
)

# -- trust executable --
add_executable(trust
    ${TRUST_SRC_DIR}/trust.cpp
)
# diag не зависит от module_loader (ModuleLoader внедряется в Context из Pipeline) и не владеет
# TypeRegistry - циклическая зависимость, требовавшая --start-group/--end-group, отсутствует.
target_link_libraries(trust PRIVATE
    pipeline_lib
    module_loader_lib
    diag_lib
    ast_lib
    # src/trust.cpp напрямую включает package/package.hpp: зависимость объявляется явно
    # (pipeline_lib линкует package_lib как PRIVATE — в его публичных заголовках package нет).
    package_lib
)
