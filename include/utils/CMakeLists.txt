# include/utils/CMakeLists.txt
# Utils — общие вспомогательные утилиты (заголовочные файлы, без библиотеки)

# ── Doxygen documentation ──
include(doxygen)

generate_component_docs(utils
    HEADERS
        utils.hpp
        error.hpp
        cache.hpp
        strings.hpp
        elf.hpp
    PROJECT_BRIEF "Общие вспомогательные утилиты"
    NO_LIBRARY
)
