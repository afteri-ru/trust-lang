# include/module_loader/CMakeLists.txt
# Module loader library - needed by diag::Context (and pipeline_lib)

trust_component(module_loader
    INTERNAL_DIRS exports interface loader package path registry
    PUBLIC_HEADERS import_exports.hpp module_export.hpp module_loader.hpp
    SOURCES
        loader/module_loader.cpp exports/module_export.cpp exports/import_exports.cpp
        path/module_path_policy.cpp package/package_store.cpp
        interface/interface_install.cpp registry/module_registry.cpp
    LINK_PUBLIC trust_include_dirs ast_lib module_api_lib
    LINK_PRIVATE types_lib syntax_lib package_lib session_lib
    # build/include (для trust/version.h) приходит транзитивно через trust_include_dirs.
    INCLUDE_PRIVATE ${CMAKE_BINARY_DIR}/syntax
    DEPENDS generate_makefile_header gen_syntax_parsers
)

# module_loader.cpp includes syntax headers (macro.h → term.h) that trigger -Wswitch/old-style-cast
set_source_files_properties(
    ${TRUST_SRC_DIR}/module_loader/loader/module_loader.cpp
    ${TRUST_SRC_DIR}/module_loader/interface/interface_install.cpp
    PROPERTIES COMPILE_OPTIONS "-Wno-error=switch;-Wno-error=old-style-cast"
)
