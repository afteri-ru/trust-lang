# include/formatter/CMakeLists.txt
# Formatter - pretty-printing исходного текста Trust (этап форматирования драйвера).

trust_component(formatter
    INTERNAL_DIRS impl
    PUBLIC_HEADERS command.hpp format.hpp
    SOURCES format.cpp token_gap.cpp emitter.cpp config.cpp command.cpp
    LINK_PUBLIC trust_include_dirs driver_lib syntax_lib
    LINK_PRIVATE session_lib utils_lib
    # build/include (для trust/version.h) приходит транзитивно через trust_include_dirs.
    INCLUDE_PRIVATE ${CMAKE_BINARY_DIR}/syntax
    # Syntax headers trigger -Wswitch / old-style-cast warnings - suppress them.
    COMPILE_OPTIONS -Wno-error=switch -Wno-error=old-style-cast
    DEPENDS gen_syntax_parsers
)
