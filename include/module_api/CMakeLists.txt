# include/module_api/CMakeLists.txt
# module_api - НИЗКОУРОВНЕВЫЙ интерфейс загрузки модулей (аналог Clang `ExternalASTSource`).
# Интерфейс объявлен ниже реализации (`module_loader`): благодаря этому `ast`/`syntax` зависят
# от интерфейса, а не от фасада `session`/`module_loader` (R34). Header-only (INTERFACE).

trust_component(module_api
    INTERFACE
    PUBLIC_HEADERS module_resolver.hpp
    LINK_PUBLIC trust_include_dirs location_lib diag_lib
)
