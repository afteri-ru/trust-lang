# include/toolchain/CMakeLists.txt
# Слой C++-сборки: build-файлы (Makefile/build.conf), компиляция/линковка, кеш `--run`,
# поиск рантайм-библиотек и план линковки. Не зависит от `semantic`/`transpiler`/`ir`:
# вход — `toolchain::BuildRequest` (пути юнитов, рантайм-заголовки, link-libs, параметры сборки).

trust_component(toolchain
    INTERNAL_DIRS build cache runtime
    PUBLIC_HEADERS build.hpp cache.hpp
    SOURCES
        build/build.cpp build/compile.cpp build/link_plan.cpp cache/run_cache.cpp
        runtime/runtime_locator.cpp
    LINK_PUBLIC driver_lib trust_include_dirs
    LINK_PRIVATE package_lib utils_lib
    # Шаблон Makefile генерируется в <build>/include/toolchain/makefile_build.hpp
    # (см. cmake/generate_files.cmake).
    DEPENDS generate_makefile_header
)
