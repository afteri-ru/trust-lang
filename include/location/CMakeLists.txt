# include/location/CMakeLists.txt
# Location — низкоуровневые примитивы позиций и тегов файлов:
#   TaggedFile / TaggedLocation / Location / RangeType / MapperRange
# Header-only; зависит только от utils (FAULT/EXPECT).

trust_component(location
    INTERFACE
    PUBLIC_HEADERS
        location.hpp module_index.hpp module_name.hpp module_path.hpp namespace_alias.hpp
    LINK_PUBLIC utils_lib trust_include_dirs
)
