# include/target/CMakeLists.txt
# Target contract: целевые (target-specific) C++-факты, вынесенные ВНЕ языковой модели
# (`types`/`ast`) — R1/R4/R37. Владелец — per-run `TargetContract`; потребители читают через
# порт `Context::target()`. Компонент НЕ зависит от `types`/`ast` (иначе ребро `types→target`).
# Целевые факты и раскладка — `ARCHITECTURE.md` §4, R37/E20.

trust_component(target
    INTERNAL_DIRS facts
    PUBLIC_HEADERS target.hpp target_info.hpp decl_cpp_facts.hpp type_cpp_facts.hpp method_delegates.hpp runtime_symbols.hpp
    SOURCES
        facts/decl_cpp_facts.cpp facts/type_cpp_facts.cpp facts/method_delegates.cpp facts/runtime_symbols.cpp
    LINK_PUBLIC location_lib runtime_lib trust_include_dirs
)
