# cmake/check_name_domains.cmake
# CI-хранитель единых доменов имён (см. MEMORY: types/symbols/utils).
#
# Правила:
#   1) storage-ключ строится/разбирается ТОЛЬКО в types/storage_key.*: разделитель `\x1f`
#      и маркер import-hidden `~` не должны появляться в других файлах (ключ - не текст имени).
#   2) trust-имя собирается только `utils::qualifyTrustName`: запрещена склейка
#      `<имя> + "::" + <остаток>` вне allowlist (C++-имена и тексты диагностик - отдельный домен).
#   3) namespace-путь строится инкрементально в одной точке: `+= "::"` разрешён только в allowlist.
#
# Использование: cmake -DROOT=<корень> -P cmake/check_name_domains.cmake
# Возврат: 0 - нарушений нет, 1 - есть нарушения.

include("${CMAKE_CURRENT_LIST_DIR}/guard_common.cmake")

guard_begin(name_domains_guard)

# -- 1. Storage-ключ: единственная точка -------------------------------------------------
set(storage_key_allow
    "include/types/type/storage_key.hpp"
    "src/types/type/storage_key.cpp"
)

# -- 2. Склейка trust-имени (`X + "::" + Y`): только C++-домен и тексты -------------------
set(qualify_allow
    "src/semantic/verify/verify_typed_ast.cpp"   # ключ поля диагностики `Kind::field`, не имя языка
    "src/semantic/decl/class_decl_analyzer.cpp" # native C++-имя члена (`@[include]`-делегация)
    "src/transpiler/emit/decl_emit.cpp"        # C++-имя сгенерированного кода
    "src/transpiler/emit/type_decl_emit.cpp"   # C++-имя сгенерированного кода (типы)
    "src/transpiler/emit/decl_func_emit.cpp"   # C++-имя функции (модульный namespace + trust-namespace)
    "src/transpiler/emit/expr_literal_emit.cpp" # C++-имя embed-маркера (модульный namespace)
    "src/transpiler/emit/member_emit.cpp"      # C++-имя сгенерированного кода
    "src/types/registry/type_printer.cpp"          # C++-проекция имени (kCpp) - printType/cppNamespacePath
    "src/transpiler/emit/emit_ctx.cpp"         # C++-имя свободного оператора (namespace модуля)
    "include/transpiler/emit/emit_common.hpp"  # C++-имя с модульным namespace (единая точка cppName)
    "include/utils/strings/strings_names.hpp"     # единственный C++-маппер сегментов (name_to_cpp/cpp_to_name)
)

# -- 3. Инкрементальное построение пути (`+= "::"`): только в перечисленных точках --------
set(path_allow
    "src/semantic/core/namespace_scope.cpp"   # namespacePath(): сборка пути из сегментов скоупов
    "src/semantic/core/macro_expander.cpp"     # раскрытие квалифицированной метки `func::`
    "src/transpiler/emit/emit_ctx.cpp"         # C++-имя
    "include/transpiler/emit/emit_common.hpp"  # C++-имя с модульным namespace (единая точка cppName)
    "src/types/registry/type_printer.cpp"          # C++-имя типа: модульный namespace ПЕРВЫМ сегментом
    "src/ast/nodes/ast_decl.cpp"                # текст сигнатуры
    "include/utils/strings/strings_names.hpp"     # C++-маппер сегментов
    "include/syntax/types.h"              # текст типа
)

guard_files(_sources ROOTS "${ROOT}/src" "${ROOT}/include" GLOBS "*.cpp" "*.hpp" "*.h")

foreach(_file IN LISTS _sources)
    file(RELATIVE_PATH _rel "${ROOT}" "${_file}")

    list(FIND storage_key_allow "${_rel}" _storage_allowed)
    if(_storage_allowed EQUAL -1)
        # `\x1f` в CMake-regex (экранирование: 4 обратных слэша в строке = 1 в regex);
        # kStorageKeySep/kImportHiddenMark - именованные маркеры того же формата.
        file(STRINGS "${_file}" _sep_hits REGEX "\\\\x1f|kStorageKeySep|kImportHiddenMark")
        if(_sep_hits)
            guard_violation("${_rel}: storage-ключ строится/разбирается вне types/storage_key.*")
        endif()
    endif()

    list(FIND qualify_allow "${_rel}" _qualify_allowed)
    if(_qualify_allowed EQUAL -1)
        file(STRINGS "${_file}" _glue_hits REGEX "\\+[ \\t]*\"::\"[ \\t]*\\+")
        if(_glue_hits)
            guard_violation("${_rel}: trust-имя склеено вручную (используйте utils::qualifyTrustName)")
        endif()
    endif()

    list(FIND path_allow "${_rel}" _path_allowed)
    if(_path_allowed EQUAL -1)
        file(STRINGS "${_file}" _append_hits REGEX "\\+=[ \\t]*\"::\"")
        if(_append_hits)
            guard_violation("${_rel}: namespace-путь собирается вручную (+= \"::\")")
        endif()
    endif()
endforeach()

guard_end("домены имён нарушены (см. cmake/check_name_domains.cmake)")
