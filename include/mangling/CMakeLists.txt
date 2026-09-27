# include/mangling/CMakeLists.txt
# `mangling` - единый источник правил именования (C++-идентификаторы, суффиксы перегрузок,
# линковочные символы, схемы временных имён). Лист: зависит только от `utils` и `types`.
# Потребители - анализатор (semantic) и кодогенерация (transpiler).

add_library(mangling_lib STATIC
    ${PROJECT_SOURCE_DIR}/mangling/mangling.cpp
)

target_link_libraries(mangling_lib PUBLIC utils_lib types_lib trust_include_dirs)

# Shared precompiled header (opt-in: TRUST_USE_PCH; no-op when OFF).
target_link_libraries(mangling_lib PRIVATE trust_pch)
