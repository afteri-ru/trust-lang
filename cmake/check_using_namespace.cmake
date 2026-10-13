#cmake / check_using_namespace.cmake
#CI - гейт CODESTYLE §3:
# 1) `using namespace` запрещён в любых заголовках(*.h/*.hpp/*.cppm);
#   2) `using namespace std|clang|llvm` запрещён везде.
# Проверяются только директивы в начале строки (комментарии/диагностические тексты не ловятся).
#
# Использование: cmake -DROOT=<корень> -P cmake/check_using_namespace.cmake
# Возврат: 0 - нарушений нет, 1 - есть нарушения.

include("${CMAKE_CURRENT_LIST_DIR}/guard_common.cmake")

guard_begin(using_namespace_guard)

guard_files(_files
    ROOTS "${ROOT}/src" "${ROOT}/include" "${ROOT}/test" "${ROOT}/examples"
    GLOBS "*.cpp" "*.hpp" "*.h" "*.cppm" "*.l" "*.in"
)

# Негативные фикстуры содержат нарушения намеренно; исключаем их только в позитивном прогоне.
if(NOT ROOT MATCHES "/test/guards/fixture_")
    list(FILTER _files EXCLUDE REGEX "/test/guards/fixture_")
endif()

foreach(_file IN LISTS _files)
    file(READ "${_file}" _text)

    if(_text MATCHES "(^|\n)[ \t]*using[ \t]+namespace[ \t]+(std|clang|llvm)[ \t]*;")
        guard_violation("${_file}: 'using namespace std/clang/llvm' is prohibited")
    endif()

    if(_file MATCHES "\\.(h|hpp|cppm)$" AND _text MATCHES "(^|\n)[ \t]*using[ \t]+namespace[ \t]+")
        guard_violation("${_file}: 'using namespace' is prohibited in headers")
    endif()
endforeach()

guard_end("remove prohibited 'using namespace' directives")
