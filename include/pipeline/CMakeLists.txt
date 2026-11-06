# include/pipeline/CMakeLists.txt
# Pipeline - transpiler pipeline library + CLI executable `trust`.

set(SYNTAX_GEN_DIR "${CMAKE_BINARY_DIR}/syntax")

trust_component(pipeline
    INTERNAL_DIRS backend cli commands dsl modules run sourcemap
    PUBLIC_DIRS paths
    PUBLIC_HEADERS pipeline.hpp cli.hpp commands.hpp
    SOURCES
        run/pipeline.cpp commands/commands.cpp cli/cli.cpp
        backend/backend_stage.cpp backend/backend_solver.cpp backend/backend_cpp.cpp
        backend/backend_verify.cpp backend/backend_noop.cpp backend/checks.cpp backend/emit.cpp
        dsl/dsl.cpp modules/modules.cpp paths/paths.cpp run/run.cpp run/phases.cpp sourcemap/source_map.cpp
    LINK_PUBLIC
        solver_lib driver_lib symbols_lib ir_lib session_lib semantic_lib trust_include_dirs
    LINK_PRIVATE
        target_lib
        # package/package.hpp + manifest.hpp используются только реализацией pipeline
        # (src/pipeline/**), в публичных заголовках pipeline их нет -> PRIVATE.
        package_lib
        verify_lib transpiler_lib syntax_lib module_loader_lib
        formatter_lib trust_llvm_dirs
    # build/include (для trust/version.h) приходит транзитивно через trust_include_dirs.
    # generated/ — байтовый .inc текста LICENSE (cmake/embed_assets.cmake).
    INCLUDE_PRIVATE ${SYNTAX_GEN_DIR} ${CMAKE_SOURCE_DIR} ${CMAKE_BINARY_DIR}/generated
    # Syntax headers trigger -Wswitch and old-style-cast warnings - suppress them
    COMPILE_OPTIONS -Wno-error=switch -Wno-error=old-style-cast
    DEPENDS generate_makefile_header gen_syntax_parsers
)

# LICENSE встраивается байтовым массивом (cmake/embed_assets.cmake): стандартный C++, поэтому
# source_map.cpp собирается любым компилятором C++23.
trust_embed_asset("${CMAKE_BINARY_DIR}/generated/pipeline/license_bytes.inc" FILE "${CMAKE_SOURCE_DIR}/LICENSE")
# Ассеты stdlib здесь не вшиваются: prelude читается из пакета `share/trust/stdlib.trust`
# через package::readResourcePackageEntry (см. run/pipeline.cpp).
