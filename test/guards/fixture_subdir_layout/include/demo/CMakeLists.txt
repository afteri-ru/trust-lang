# include/demo/CMakeLists.txt (негативная фикстура subdir_layout_guard)

trust_component(demo
    PUBLIC_HEADERS a.hpp b.hpp c.hpp d.hpp e.hpp
    INCLUDE_PUBLIC ${CMAKE_CURRENT_SOURCE_DIR}
)
