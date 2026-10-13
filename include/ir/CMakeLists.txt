# include/ir/CMakeLists.txt
# Семейство производных представлений (HIR/MIR) + движок flow-анализов.
# HIR: lowering по клону AST (hir/build.cpp, hir/lowering.cpp) и структурный верификатор
# (hir/verify.cpp); MIR: представление/CFG и структурный верификатор (mir/cfg.cpp,
# mir/cfg_verify.cpp). Потребители — flow-анализы `semantic`, верификатор `solver`,
# C++-бэкенд (`transpiler`) и оркестратор `pipeline`; кодоген MIR не читает.

trust_component(ir
    INTERNAL_DIRS hir mir
    PUBLIC_HEADERS
        analysis_manager.hpp cfg.hpp dataflow.hpp hir_module.hpp hir_verify.hpp lowering.hpp
        place_root.hpp places.hpp
    SOURCES hir/build.cpp hir/lowering.cpp hir/verify.cpp mir/cfg.cpp mir/cfg_verify.cpp
    LINK_PUBLIC ast_lib diag_lib utils_lib trust_include_dirs
    LINK_PRIVATE attrs_lib session_lib types_lib
)
