# include/session/CMakeLists.txt
# Session facade `Context`: объединяет SourceMapWriter, DiagnosticEngine, Options, AttrPool,
# TypeRegistry (невладеюще), ModuleLoader (невладеюще) и Macro. Вынесен из diag, чтобы
# чистые диагностики/опции не зависели от хранения исходников и AST-атрибутов.

trust_component(session
    PUBLIC_HEADERS context.hpp run_services.hpp
    SOURCES context.cpp
    LINK_PUBLIC trust_include_dirs diag_lib sourcemap_lib attrs_lib utils_lib types_lib target_lib module_api_lib
)
