# include/location/CMakeLists.txt
# Location — низкоуровневые примитивы позиций и теги файлов:
#   TaggedFile / TaggedLocation / Location / RangeType / MapperRange
# Header-only, зависит только от utils (FAULT/EXPECT).

add_library(location_lib INTERFACE)
target_link_libraries(location_lib INTERFACE utils_lib trust_include_dirs)
