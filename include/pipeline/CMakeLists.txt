# include/pipeline/CMakeLists.txt
# Pipeline — transpiler pipeline library

# ── Pipeline library ──
add_library(pipeline_lib STATIC
    ${PROJECT_SOURCE_DIR}/pipeline/pipeline.cpp
    ${PROJECT_SOURCE_DIR}/pipeline/pipeline_parser.cpp
    pipeline_parser.hpp
)
target_link_libraries(pipeline_lib PUBLIC parser_lib trust_include_dirs)
target_link_libraries(pipeline_lib PRIVATE nlohmann_json::nlohmann_json)
target_link_libraries(pipeline_lib PRIVATE CLI11::CLI11)
target_include_directories(pipeline_lib PRIVATE ${CMAKE_BINARY_DIR}/include)

# generate_options_h должен быть собран до pipeline_lib — заголовочный файл с опциями
# генерируется в include/pipeline/options.h.in → ${CMAKE_BINARY_DIR}/include/pipeline/options.h
add_dependencies(pipeline_lib generate_options_h)
