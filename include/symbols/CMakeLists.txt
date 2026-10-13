# include/symbols/CMakeLists.txt
# Символы и видимость имён: SymbolTable (фасад над зонами: ScopeStack, DeclarationRegistry,
# NameResolver, ModuleSurface, ImportSiteRegistry, AliasRegistry, ModuleContext, SymbolArena),
# SymbolIndex — проекция объявлений для LSP. Общий продукт `semantic` и `lsp`; кодоген таблицу
# не читает (использует факты, материализованные в полях AST).

trust_component(symbols
    INTERNAL_DIRS decl import table
    PUBLIC_HEADERS decl_binders.hpp reference_index.hpp scope_path.hpp symbol_index.hpp symbol_table.hpp
    SOURCES
        table/symbol_table.cpp decl/decl_registry.cpp decl/name_resolver.cpp
        table/scope_path.cpp table/scope_stack.cpp import/alias_registry.cpp
        decl/module_surface.cpp import/import_sites.cpp decl/module_context.cpp
        table/symbol_key_factory.cpp table/reference_index.cpp
    LINK_PUBLIC ast_lib types_lib location_lib utils_lib trust_include_dirs
    LINK_PRIVATE sourcemap_lib
)
