# include/attrs/CMakeLists.txt
# Attribute system: AttrId/Attr value types, AttrPool registry and built-in names.
# Pure value/registry layer - depends only on location (MapperRange) and utils.
# The @[...] parser (attr_parser) lives in ast: it needs Context/Diagnostics.

add_library(attrs_lib STATIC
    ${PROJECT_SOURCE_DIR}/attrs/attr_pool.cpp
    ${PROJECT_SOURCE_DIR}/attrs/attr_builtin.cpp
)

target_link_libraries(attrs_lib PUBLIC
    location_lib
    utils_lib
    trust_include_dirs
)

# Shared precompiled header (opt-in: TRUST_USE_PCH; no-op when OFF).
target_link_libraries(attrs_lib PRIVATE trust_pch)
