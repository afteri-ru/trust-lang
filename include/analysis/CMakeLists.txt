# include/analysis/CMakeLists.txt
# Продукты анализа, читаемые БЭКЕНДАМИ и верификатором: поведенческие режимы компилятора
# (BehavioralModes + значения опций) и классификация рантайм-проверок (RuntimeCheckKind).
# Header-only: правила анализа живут в `semantic`, структура тела — в `ir`, символы — в `symbols`.

trust_component(analysis
    INTERFACE
    PUBLIC_HEADERS modes.hpp runtime_checks.hpp
    LINK_PUBLIC ast_lib trust_include_dirs
)
