# test/unit/CMakeLists.txt - единый исполняемый файл для всех юнит-тестов

# -- All test sources --
set(TEST_SOURCES
    unittest.cpp
    diag/basic_test.cpp
    diag/error_limit_test.cpp
    diag/options_test.cpp
    diag/flag_values_test.cpp
    diag/diag_format_test.cpp
    diag/diagnostic_location_test.cpp
    diag/context_stack_test.cpp
    diag/expansion_chain_test.cpp
    diag/one_entry_all_formats_test.cpp
    session/context_test.cpp
    session/context_bitpack_test.cpp
    sourcemap/mapping_test.cpp
    sourcemap/mapping_reader_test.cpp
    sourcemap/mapping_extended_test.cpp
    sourcemap/save_and_read_test.cpp
    sourcemap/mapping_completeness_test.cpp
    sourcemap/range_order_test.cpp
    transpiler/src_map_cli_test.cpp
    attrs/parse_test.cpp
    attrs/attr_args_test.cpp
    ast/kind_dispatch_test.cpp
    ast/type_access_test.cpp
    ast/clone_test.cpp
    ast/decl_id_test.cpp
    ast/text_storage_test.cpp
    ast/text_model_test.cpp
    ast/binary_op_test.cpp
    ast/alias_kind_test.cpp
    ast/definitions_content_test.cpp
    ast/slots_test.cpp
    ast/destructure_target_test.cpp
    ir/hir/hir_verify_test.cpp
    pipeline/module_registry_test.cpp
    pipeline/module_loader_test.cpp
    pipeline/module_path_test.cpp
    pipeline/module_export_test.cpp
    #  parser/token_gen_test.cpp
    pipeline/pipeline_test.cpp
    pipeline/pipeline_symbol_test.cpp
    pipeline/uninit_inference_test.cpp
    pipeline/package_test.cpp
    pipeline/link_plan_test.cpp
    pipeline/stdlib_package_test.cpp
    pipeline/compile_test.cpp
    pipeline/backend_stage_test.cpp
    pipeline/target_info_test.cpp
    target/target_contract_test.cpp
    verify/report_test.cpp
    verify/certificate_test.cpp
    driver/parse_test.cpp
    driver/analysis_options_test.cpp
    debug/mi_parse_test.cpp
    debug/mi_transport_test.cpp
    semantic/analysis_zones_test.cpp
    semantic/semantic_test.cpp
    semantic/semantic_model_test.cpp
    semantic/hook_registry_test.cpp
    semantic/semantic_table_test.cpp
    semantic/semantic_funcdecl_test.cpp
    semantic/delegation_check_test.cpp
    semantic/symbol_collector_test.cpp
    symbols/scope_test.cpp
    symbols/scope_stack_test.cpp
    symbols/module_context_test.cpp
    symbols/import_sites_test.cpp
    symbols/alias_registry_test.cpp
    symbols/decl_registry_test.cpp
    symbols/module_surface_test.cpp
    symbols/symbol_arena_test.cpp
    symbols/symbol_table_overload_test.cpp
    symbols/symbol_visibility_test.cpp
    symbols/site_visibility_behavior_test.cpp
    symbols/name_resolver_test.cpp
    symbols/scope_path_test.cpp
    ir/cfg_test.cpp
    ir/analysis_manager_test.cpp
    ir/mir_build_test.cpp
    ir/dataflow_test.cpp
    ir/places_test.cpp
    ir/cfg_verify_test.cpp
    analysis/runtime_checks_test.cpp
    semantic/scope_liveness_test.cpp
    semantic/debug_scope_test.cpp
    semantic/multilevel_var_test.cpp
    semantic/format_check_test.cpp
    semantic/type_set_test.cpp
    semantic/pod_predicate_test.cpp
    semantic/internal_error_policy_test.cpp
    semantic/arg_casts_test.cpp
    semantic/verify_typed_ast_test.cpp
    transpiler/codegen_literal_test.cpp
    transpiler/codegen_stmt_test.cpp
    transpiler/codegen_func_test.cpp
    transpiler/codegen_modes_test.cpp
    transpiler/type_name_render_test.cpp
    transpiler/cpp_name_test.cpp
    transpiler/cpp_template_params_test.cpp
    transpiler/runtime_type_tag_test.cpp
    transpiler/transpile_lsp_test.cpp
    transpiler/transpile_lsp_mapping_test.cpp
    # pipeline/pipelineopt_test.cpp
    types/type_alias_test.cpp
    types/type_names_test.cpp
    types/type_kind_layout_test.cpp
    types/type_storage_key_test.cpp
    types/storage_key_test.cpp
    types/builtin_definition_test.cpp
    types/type_helpers_test.cpp
    types/type_invariant_test.cpp
    types/type_identity_test.cpp
    types/type_traits_test.cpp
    types/reftype_test.cpp
    types/type_method_test.cpp
    types/method_key_test.cpp
    types/call_signature_test.cpp
    types/fn_identity_test.cpp
    types/overload_resolve_test.cpp
    types/method_signatures_test.cpp
    types/template_unify_test.cpp
    types/type_intern_key_test.cpp
    types/type_zone_access_test.cpp
    types/type_write_access_test.cpp
    types/type_read_view_test.cpp
    types/array_type_test.cpp
    types/tensor_type_test.cpp
    types/native_type_test.cpp
    types/template_pack_test.cpp
    types/template_subst_test.cpp
    types/trust_flag_test.cpp
    types/runtime_symbols_test.cpp
    types/stack_check_test.cpp
    types/operator_registry_test.cpp
    stdlib/iterator_test.cpp
    stdlib/generator_test.cpp
    stdlib/async_task_test.cpp
    stdlib/async_pool_test.cpp
    stdlib/async_mutex_test.cpp
    stdlib/async_generator_test.cpp
    runtime/rational_test.cpp
    runtime/big_integer_test.cpp
    runtime/dict_test.cpp
    runtime/range_test.cpp
    runtime/checked_cast_test.cpp
    runtime/any_convert_test.cpp
    runtime/enum_test.cpp
    runtime/stack_check_runtime_test.cpp
    runtime/trusted_cpp_test.cpp
    runtime/trusted_cpp_shared_test.cpp
    runtime/trusted_cpp_shared_weak_test.cpp
    runtime/trusted_cpp_correctness_test.cpp
    runtime/trusted_cpp_unique_test.cpp
    runtime/trusted_cpp_concepts_test.cpp
    runtime/trusted_cpp_selfref_test.cpp
    runtime/trusted_cpp_api_test.cpp
    lsp/lsp_handler_test.cpp
    lsp/lsp_hover_test.cpp
    lsp/lsp_completion_test.cpp
    lsp/lsp_codeaction_test.cpp
    lsp/html_emit_test.cpp
    lsp/builtin_catalog_test.cpp
    lsp/lsp_options_test.cpp
    playground/server_test.cpp
    playground/config_test.cpp
    playground/dispatch_test.cpp
    playground/security_test.cpp
    playground/worker_test.cpp
    utils/cache_test.cpp
    utils/file_io_test.cpp
    utils/strings_test.cpp
    utils/time_test.cpp
    utils/transport_test.cpp
    utils/error_test.cpp
    utils/io_test.cpp
    utils/trace_test.cpp
    utils/text_interner_test.cpp
    utils/ident_codec_test.cpp
    solver/smt_printer_test.cpp
    solver/smt_printer_z3_test.cpp
    solver/smt_op_test.cpp
    solver/smt_assembler_test.cpp
    # Syntax tests
    ${CMAKE_SOURCE_DIR}/test/unit/syntax/lexer_test.cpp
    ${CMAKE_SOURCE_DIR}/test/unit/syntax/lexer_macro_test.cpp
    ${CMAKE_SOURCE_DIR}/test/unit/syntax/lexer_trust_test.cpp
    ${CMAKE_SOURCE_DIR}/test/unit/syntax/macro_core_test.cpp
    ${CMAKE_SOURCE_DIR}/test/unit/syntax/macro_options_test.cpp
    ${CMAKE_SOURCE_DIR}/test/unit/syntax/macro_mapping_test.cpp
    ${CMAKE_SOURCE_DIR}/test/unit/syntax/parser_test.cpp
    ${CMAKE_SOURCE_DIR}/test/unit/syntax/term_text_api_test.cpp
    ${CMAKE_SOURCE_DIR}/test/unit/syntax/parser_recovery_test.cpp
    ${CMAKE_SOURCE_DIR}/test/unit/syntax/parser_stmt_test.cpp
    ${CMAKE_SOURCE_DIR}/test/unit/syntax/parser_expr_test.cpp
    ${CMAKE_SOURCE_DIR}/test/unit/syntax/parser_decl_test.cpp
    ${CMAKE_SOURCE_DIR}/test/unit/syntax/parser_filling_test.cpp
    ${CMAKE_SOURCE_DIR}/test/unit/syntax/parser_trust_test.cpp
    ${CMAKE_SOURCE_DIR}/test/unit/syntax/predef_pragma_test.cpp
    ${CMAKE_SOURCE_DIR}/test/unit/syntax/debug_macro_test.cpp
    ${CMAKE_SOURCE_DIR}/test/unit/syntax/module_name_test.cpp
    ${CMAKE_SOURCE_DIR}/test/unit/syntax/module_header_test.cpp
    # Formatter tests
    formatter/format_test.cpp
    formatter/format_comments_test.cpp
    formatter/format_config_test.cpp
    formatter/format_contract_test.cpp
    formatter/format_tail_test.cpp
)

# Tensor tests (require WITH_TORCH)
if(WITH_TORCH)
    list(APPEND TEST_SOURCES runtime/tensor_test.cpp)
endif()

add_executable(unit_tests ${TEST_SOURCES})


# -- Torch configuration for unit_tests --
if(WITH_TORCH)
    target_include_directories(unit_tests SYSTEM PRIVATE ${LIBTORCH_INCLUDE_DIRS})
    target_include_directories(unit_tests PRIVATE ${TRUST_SRC_DIR}/include/runtime)
    target_link_directories(unit_tests PRIVATE ${LIBTORCH_LIB_DIRS})
    target_link_libraries(unit_tests PRIVATE torch c10 torch_cpu)
    target_compile_options(unit_tests PRIVATE -frtti)
    target_sources(unit_tests PRIVATE ${TRUST_SRC_DIR}/runtime/tensor.cpp)
endif()

# -- Link all libraries --
target_link_libraries(unit_tests PRIVATE
    ast_lib
    diag_lib
    semantic_lib
    transpiler_lib
    types_lib
    pipeline_lib
    builddriver_lib
    module_loader_lib
    debug_lib
    lsp_lib
    playground_lib
    solver_lib
    syntax_lib
    formatter_lib
    # LLVM headers are used directly by sourcemap/hash tests; the target centralizes the
    # -idirafter handling (see the trust_llvm_dirs comment in the root CMakeLists).
    trust_llvm_dirs
    gmp_interface
    GTest::gtest
)

# -- Test data directory --
# TEST_DATA_DIR определён как CACHE-переменная в корневом CMakeLists.txt.
# Локально не переопределяем - используем глобальную.
file(MAKE_DIRECTORY "${TEST_DATA_DIR}")

target_include_directories(unit_tests PRIVATE
    ${CMAKE_BINARY_DIR}/include
    ${CMAKE_BINARY_DIR}/syntax
    ${CMAKE_CURRENT_SOURCE_DIR}
    ${CMAKE_SOURCE_DIR}/include/runtime
    ${CMAKE_SOURCE_DIR}/include/stdlib/core
)

target_compile_definitions(unit_tests PRIVATE
    TEST_DATA_DIR="${TEST_DATA_DIR}"
    TRUST_BINARY_PATH="${CMAKE_BINARY_DIR}/trust"
    TRUST_RUNTIME_STATIC_PATH="${CMAKE_BINARY_DIR}/trust-runtime.a"
    TRUST_RUNTIME_SHARED_PATH="${CMAKE_BINARY_DIR}/trust-runtime.so"
    TRUST_RATIONAL_HPP_PATH="${PROJECT_INCLUDE_DIR}/stdlib/core/builtin/rational.hpp"
)

# -- Attach trust module --
target_trust_module(unit_tests)

# Unit tests link/run against the built trust-runtime libraries.
add_dependencies(unit_tests trust_runtime trust_runtime_static)

target_compile_options(unit_tests PRIVATE
    -Wno-unused-result
    -Wno-error=old-style-cast
    -Wno-error=switch
)

# stack_check_runtime_test.cpp автономно тестирует trust::stack_check: для
# get_stack_limit()/check_stack_limit() нужна секция .stack_sizes (только этот файл).
set_source_files_properties(
    ${CMAKE_CURRENT_SOURCE_DIR}/runtime/stack_check_runtime_test.cpp
    PROPERTIES COMPILE_OPTIONS "-fstack-size-section"
)

# -- Symlink tensor plugin for runtime loading (only when Torch is enabled) --
if(WITH_TORCH)
    add_custom_command(TARGET unit_tests POST_BUILD
        COMMAND ${CMAKE_COMMAND} -E create_symlink
            ${CMAKE_BINARY_DIR}/libtensor_cpu.so
            ${CMAKE_CURRENT_BINARY_DIR}/libtensor_cpu.so
        COMMENT "Creating symlink for libtensor_cpu.so"
    )
endif()

# -- Integration test executables for debug component --

# gdb_debuggee - standalone программа, используемая как debuggee для GDB
add_executable(gdb_debuggee
    ${CMAKE_CURRENT_SOURCE_DIR}/debug/gdb_debuggee.cpp
)
target_include_directories(gdb_debuggee PRIVATE ${CMAKE_SOURCE_DIR}/include)
target_link_libraries(gdb_debuggee PRIVATE utils_lib)
target_compile_options(gdb_debuggee PRIVATE -g -fno-pie)
target_link_options(gdb_debuggee PRIVATE -fno-pie -no-pie)

# gdb_main - test GDB via GdbDebug wrapper
add_executable(gdb_main
    ${CMAKE_CURRENT_SOURCE_DIR}/debug/gdb_main.cpp
)
target_link_libraries(gdb_main PRIVATE debug_lib)
target_include_directories(gdb_main PRIVATE
    ${CMAKE_CURRENT_SOURCE_DIR}/debug
    ${CMAKE_SOURCE_DIR}/include
)
# -- CTest registration --
# Единый бинарник unit_tests (одна линковка), но в тестовом прогоне его можно разбить
# на ШАРДЫ через --gtest_filter (TRUST_UNIT_SHARDS, default 4), чтобы `ctest -j`
# параллелил их на разных ядрах. Фильтры покрывают ВСЕ первые буквы A-Z (+ '_' как
# catch-all) — новый сьют никогда не останется «непокрытым»; префиксы шардов не
# пересекаются между собой. При TRUST_UNIT_SINGLE / TRUST_UNIT_SHARDS<=1 — как раньше,
# одна компактная запись CTest.
set(TRUST_UNIT_SHARDS 4 CACHE STRING "number of parallel CTest shards for unit_tests (0/1 = single)")
option(TRUST_UNIT_SINGLE "register unit_tests as ONE compact CTest entry" OFF)

# trust_add_test_suite требует TIMEOUT/FIXTURES_SETUP; шард — та же программа с фильтром.
function(trust_register_unit_shard _name _filter)
    trust_add_test_suite(${_name}
        TIMEOUT 300
        WORKING_DIRECTORY ${CMAKE_CURRENT_BINARY_DIR}
        FIXTURES_SETUP "${TRUST_SUITE_FIXTURES}"
        COMMAND $<TARGET_FILE:unit_tests> --gtest_filter=${_filter}
    )
endfunction()

if(TRUST_UNIT_SINGLE OR NOT TRUST_UNIT_SHARDS GREATER 1)
    trust_add_test_suite(unit_tests
        TIMEOUT 300
        WORKING_DIRECTORY ${CMAKE_CURRENT_BINARY_DIR}
        # Final stage examples_tests/package tests run only after the early suites pass.
        FIXTURES_SETUP "${TRUST_SUITE_FIXTURES}"
        COMMAND $<TARGET_FILE:unit_tests>
    )
else()
    # Шарды по первым буквам сьютов (суммы тестов: 554 / 501 / 609 / 657).
    trust_register_unit_shard(unit_tests_shard1 "P*:E*:G*:H*:I*:K*:N*:U*:V*:W*:_*")
    trust_register_unit_shard(unit_tests_shard2 "M*:C*:L*:B*")
    trust_register_unit_shard(unit_tests_shard3 "S*:R*:O*:D*:F*")
    trust_register_unit_shard(unit_tests_shard4 "T*:A*")
endif()