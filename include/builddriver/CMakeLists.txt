# include/builddriver/CMakeLists.txt
# Драйвер сборки/запуска: оркестрация поверх артефактов `pipeline` + вызов `toolchain`.
# Нижестоящий слой (зависит от `pipeline` и `toolchain`), поэтому не создаёт цикла графа.
# Библиотека (не executable): точка входа CLI (`trust`) линкует её вместе с `pipeline`.

trust_component(builddriver
    INTERNAL_DIRS build
    PUBLIC_HEADERS build.hpp
    SOURCES
        build/build.cpp build/archive.cpp
    LINK_PUBLIC
        driver_lib location_lib
    LINK_PRIVATE
        pipeline_lib toolchain_lib utils_lib session_lib package_lib trust_include_dirs
)
