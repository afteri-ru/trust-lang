# include/solver/CMakeLists.txt
# SMT-LIB 2 solver component

set(SOLVER_SOURCES
    ${PROJECT_SOURCE_DIR}/solver/smt_printer.cpp
    ${PROJECT_SOURCE_DIR}/solver/solver.cpp
)

# Always build the base solver library (printer + stub)
add_library(solver_lib STATIC ${SOLVER_SOURCES})

target_link_libraries(solver_lib PUBLIC diag_lib)
target_include_directories(solver_lib PUBLIC ${CMAKE_CURRENT_SOURCE_DIR})

if(WITH_SOLVER)
    # Include Z3 implementation
    target_sources(solver_lib PRIVATE ${PROJECT_SOURCE_DIR}/solver/solver_z3.cpp)
    target_link_libraries(solver_lib PRIVATE ${Z3_LIBRARIES})
    target_include_directories(solver_lib SYSTEM PRIVATE ${Z3_INCLUDE_DIRS})

    message(STATUS "Solver: Z3 enabled (${Z3_VERSION_STRING})")
else()
    message(STATUS "Solver: stub only (use -DWITH_SOLVER=ON for Z3)")
endif()