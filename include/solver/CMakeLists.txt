# include/solver/CMakeLists.txt
# SMT-LIB 2 solver component.
# Мост AST→SMT (TrustToSmt): HIR — данные (факты семантики), MIR — структура тела; режимы —
# данные `analysis::BehavioralModes`. Зависимости от `semantic` нет.

trust_component(solver
    INTERNAL_DIRS impl smt vc
    PUBLIC_HEADERS smt_ast.hpp smt_printer.hpp solver.hpp trust_to_smt.hpp
    SOURCES
        smt/smt_printer.cpp vc/sort_mapper.cpp impl/solver.cpp impl/trust_to_smt.cpp
        impl/smt_expr_enc.cpp impl/smt_vcgen.cpp impl/smt_assembler.cpp
    LINK_PUBLIC diag_lib ir_lib analysis_lib types_lib ast_lib session_lib
    LINK_PRIVATE sourcemap_lib
)

# Условный TU: компилируется только в конфигурации WITH_SOLVER=ON (заявляется явно).
trust_claim_sources(solver/impl/solver_z3.cpp)

if(WITH_SOLVER)
    # Include Z3 implementation
    target_sources(solver_lib PRIVATE ${TRUST_SRC_DIR}/solver/impl/solver_z3.cpp)
    target_link_libraries(solver_lib PRIVATE ${Z3_LIBRARIES})
    target_include_directories(solver_lib SYSTEM PRIVATE ${Z3_INCLUDE_DIRS})
    target_compile_definitions(solver_lib PUBLIC TRUST_HAS_Z3)

    message(STATUS "Solver: Z3 enabled (${Z3_VERSION_STRING})")
else()
    message(STATUS "Solver: stub only (use -DWITH_SOLVER=ON for Z3)")
endif()
