# include/types/CMakeLists.txt
# Types library - type system infrastructure

trust_component(types
    INTERNAL_DIRS call func names registry subst type
    PUBLIC_HEADERS group.hpp intrinsics.hpp type_system.hpp runtime_symbols.hpp type_data.hpp type_id.hpp
                   typekind.hpp types.hpp call.hpp names.hpp type_read.hpp
    SOURCES
        type/group.cpp
        registry/registry.cpp registry/type_store.cpp registry/type_store_projection.cpp registry/type_store_shape.cpp
        registry/type_name_index.cpp
        registry/named_type_registrar.cpp registry/builtin_core_builder.cpp
        registry/builtin_core_builder_registration.cpp registry/builtin_core_builder_container_methods.cpp
        registry/builtin_registry.cpp
        registry/structural_interner.cpp registry/structural_interner_containers.cpp registry/structural_interner_ref.cpp
        registry/type_printer.cpp registry/type_printer_cpp.cpp registry/type_printer_trust.cpp registry/type_printer_ref.cpp
        registry/method_table.cpp registry/method_table_lookup.cpp registry/type_read.cpp
        subst/template_subst.cpp
        func/fn_identity.cpp func/fn_identity_code.cpp
        type/ref_type.cpp type/storage_key.cpp type/site_visibility.cpp
    LINK_PUBLIC attrs_lib location_lib trust_include_dirs
    LINK_PRIVATE diag_lib
)
