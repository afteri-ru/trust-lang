# include/attrs/CMakeLists.txt
# Attribute system: AttrId/Attr value types, AttrPool registry and built-in names.
# Pure value/registry layer - depends only on location (MapperRange) and utils.
# The @[...] parser (attr_parser) lives in ast: it needs Context/Diagnostics.

trust_component(attrs
    PUBLIC_HEADERS attr_args.hpp attr_builtin.hpp attr.hpp attr_pool.hpp
    SOURCES attr_pool.cpp attr_builtin.cpp
    LINK_PUBLIC location_lib utils_lib trust_include_dirs
)
