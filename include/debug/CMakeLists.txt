# include/debug/CMakeLists.txt
# Debug: Trust-lang debug system (source mapping + GDB/MI bridge)

trust_component(debug
    INTERNAL_DIRS dap gdb mi
    SOURCES dap/dap_handler.cpp dap/dap_transport.cpp
        gdb/gdb_debug.cpp mi/mi_parse.cpp mi/mi_transport.cpp
    LINK_PUBLIC trust_include_dirs sourcemap_lib
    LINK_PRIVATE driver_lib msgpack-c-static
)

# -- trust-dap: DAP server для trust-lang debugging --
# Собирается только при TRUST_BUILD_DAP (по умолчанию OFF). debug_lib выше остаётся
# всегда - он нужен lsp_lib. TU условной цели заявляется явно (см. cmake/component.cmake).
trust_claim_sources(debug/dap/trust_dap.cpp)
if(TRUST_BUILD_DAP)
    add_executable(trust-dap
        ${TRUST_SRC_DIR}/debug/dap/trust_dap.cpp
    )

    target_link_libraries(trust-dap PRIVATE
        debug_lib
        nlohmann_json::nlohmann_json
    )

    target_include_directories(trust-dap PRIVATE
        ${CMAKE_BINARY_DIR}/include
        ${PROJECT_INCLUDE_DIR}
    )
endif()
