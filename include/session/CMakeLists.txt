# include/session/CMakeLists.txt
# Session facade `Context`: объединяет SourceMapWriter, DiagnosticEngine, Options, AttrPool,
# TypeRegistry (невладеюще), ModuleLoader (невладеюще) и Macro. Вынесен из diag, чтобы
# чистые диагностики/опции не зависели от хранения исходников и AST-атрибутов.

add_library(session_lib STATIC
    ${PROJECT_SOURCE_DIR}/session/context.cpp
)

target_link_libraries(session_lib PUBLIC
    trust_include_dirs
    diag_lib
    sourcemap_lib
    attrs_lib
    utils_lib
)

# Shared precompiled header (opt-in: TRUST_USE_PCH; no-op when OFF).
target_link_libraries(session_lib PRIVATE trust_pch)
