# cmake/check_subdir_layout.cmake
# CI-хранитель раскладки подкаталогов компонента.
#
# Правила:
#   A) реализация не лежит в корне src/<component>/ — каждый TU живёт в подкаталоге
#      ответственности. Исключение — МАЛЫЙ компонент (порог в cmake/internal_dirs.cmake);
#   B) каждый подкаталог src/<component>/** классифицирован: объявлен в INTERNAL_DIRS или
#      PUBLIC_DIRS манифеста (объявленный подкаталог рекурсивен);
#   C) в корне include/<component>/ нет документов разработки (*.md), кроме MEMORY.md и README.md —
#      корень описывает только публичный интерфейс;
#   D) корень компонента не добавляется в публичные include-пути
#      (`INCLUDE_PUBLIC ${CMAKE_CURRENT_SOURCE_DIR}`): иначе включать internal-подкаталог можно
#      короткой формой `#include "dap/..."` в обход internal_boundary_guard;
#   E) объявленный подкаталог существует (в include/ или в src/) и не вложен в подкаталог другого вида.
#
# Долг, существующий до реорганизации, зафиксирован в _known_* списках ниже. Запись, которая
# больше не нарушает правило, — ошибка (allowlist обязан сокращаться), поэтому после каждого
# переноса кода соответствующие записи удаляются в том же изменении.
#
# Использование: cmake -DROOT=<корень> -P cmake/check_subdir_layout.cmake

include("${CMAKE_CURRENT_LIST_DIR}/guard_common.cmake")
include("${CMAKE_CURRENT_LIST_DIR}/component_manifest.cmake")
include("${CMAKE_CURRENT_LIST_DIR}/internal_dirs.cmake")

guard_begin(subdir_layout_guard)

# Компоненты, у которых реализация ещё лежит в корне src/<component>/ (S4 плана реорганизации).
set(_known_root_tu
    debug diag formatter lsp module_loader package pipeline playground
    solver toolchain transpiler
)
set(_known_root_tu_used "")

# Документы разработки в корне include/<component>/ (S2/S3/S4 плана реорганизации).
set(_known_root_docs
    diag/OPTIONS.md lsp/HTML.md lsp/NAVIGATION.md solver/VERIFICATION_ENVELOPE.md
)
set(_known_root_docs_used "")

guard_components(_components)
if(NOT _components)
    message(FATAL_ERROR "subdir_layout_guard: не найдено ни одного компонента в ${ROOT}/include")
endif()

foreach(_component IN LISTS _components)
    set(_include_dir "${ROOT}/include/${_component}")
    set(_src_dir "${ROOT}/src/${_component}")
    set(_manifest "${_include_dir}/CMakeLists.txt")
    trust_manifest_parse("${_manifest}" M)

    # -- D) корень компонента не публикуется как include-путь ------------------
    file(READ "${_manifest}" _manifest_text)
    if(_manifest_text MATCHES "INCLUDE_PUBLIC[ ]+[$][{]CMAKE_CURRENT_SOURCE_DIR[}]")
        guard_violation("include/${_component}/CMakeLists.txt: INCLUDE_PUBLIC CMAKE_CURRENT_SOURCE_DIR "
                        "публикует корень компонента — internal-подкаталоги станут включаемы короткой формой")
    endif()

    # -- E) объявленные подкаталоги: существуют и не конфликтуют ----------------
    foreach(_dir IN LISTS M_PUBLIC_DIRS)
        if(NOT IS_DIRECTORY "${_include_dir}/${_dir}")
            guard_violation("include/${_component}/CMakeLists.txt: публичный подкаталог '${_dir}' "
                            "отсутствует в include/${_component}/")
        endif()
    endforeach()
    foreach(_dir IN LISTS M_INTERNAL_DIRS)
        if(NOT IS_DIRECTORY "${_include_dir}/${_dir}" AND NOT IS_DIRECTORY "${_src_dir}/${_dir}")
            guard_violation("include/${_component}/CMakeLists.txt: internal-подкаталог '${_dir}' "
                            "отсутствует и в include/${_component}/, и в src/${_component}/")
        endif()
    endforeach()
    foreach(_dir IN LISTS M_PUBLIC_DIRS M_INTERNAL_DIRS)
        foreach(_other IN LISTS M_PUBLIC_DIRS M_INTERNAL_DIRS)
            if(_dir STREQUAL _other OR NOT _dir MATCHES "^${_other}/")
                continue()
            endif()
            trust_subdir_classify(_kind "${M_PUBLIC_DIRS}" "${M_INTERNAL_DIRS}" "${_dir}")
            trust_subdir_classify(_other_kind "${M_PUBLIC_DIRS}" "${M_INTERNAL_DIRS}" "${_other}")
            if(NOT _kind STREQUAL _other_kind)
                guard_violation("include/${_component}/CMakeLists.txt: подкаталог '${_dir}' (${_kind}) "
                                "вложен в подкаталог другого вида '${_other}' (${_other_kind})")
            endif()
        endforeach()
    endforeach()

    # -- A) реализация вне корня src/<component>/ ------------------------------
    trust_is_small_component(_small "${_include_dir}" "${_src_dir}")
    if(NOT _small)
        file(GLOB _root_tu "${_src_dir}/*.cpp")
        foreach(_tu IN LISTS _root_tu)
            get_filename_component(_tu_name "${_tu}" NAME)
            if(_component IN_LIST _known_root_tu)
                list(APPEND _known_root_tu_used "${_component}")
            else()
                guard_violation("src/${_component}/${_tu_name}: TU лежит в корне компонента — "
                                "перенесите его в подкаталог ответственности")
            endif()
        endforeach()
    endif()

    # -- B) подкаталоги src классифицированы ------------------------------------
    if(IS_DIRECTORY "${_src_dir}")
        file(GLOB_RECURSE _src_subdirs LIST_DIRECTORIES true "${_src_dir}/*")
        foreach(_sub IN LISTS _src_subdirs)
            if(NOT IS_DIRECTORY "${_sub}")
                continue()
            endif()
            file(RELATIVE_PATH _rel "${_src_dir}" "${_sub}")
            trust_subdir_classify(_kind "${M_PUBLIC_DIRS}" "${M_INTERNAL_DIRS}" "${_rel}")
            if(_kind STREQUAL "unknown")
                guard_violation("src/${_component}/${_rel}/: каталог не объявлен ни в INTERNAL_DIRS, "
                                "ни в PUBLIC_DIRS (подкаталог ответственности должен быть явным)")
            endif()
        endforeach()
    endif()

    # -- C) корень include/<component>/ — только интерфейс ---------------------
    file(GLOB _root_md "${_include_dir}/*.md")
    foreach(_doc IN LISTS _root_md)
        get_filename_component(_doc_name "${_doc}" NAME)
        if(_doc_name STREQUAL "MEMORY.md" OR _doc_name STREQUAL "README.md")
            continue()
        endif()
        set(_entry "${_component}/${_doc_name}")
        if(_entry IN_LIST _known_root_docs)
            list(APPEND _known_root_docs_used "${_entry}")
        else()
            guard_violation("include/${_component}/${_doc_name}: документ разработки в корне компонента — "
                            "перенесите его в подкаталог ответственности")
        endif()
    endforeach()
endforeach()

# -- Allowlist обязан сокращаться: неиспользованная запись = ошибка -------------
# Записи о компонентах, которых нет в текущем дереве (например, в негативной фикстуре),
# не проверяются: allowlist относится к реальному дереву проекта.
foreach(_entry IN LISTS _known_root_tu)
    if(NOT _entry IN_LIST _components)
        continue()
    endif()
    if(NOT _entry IN_LIST _known_root_tu_used)
        guard_violation("allowlist: запись '${_entry}' устарела — в src/${_entry}/ больше нет TU в корне; "
                        "удалите её из _known_root_tu")
    endif()
endforeach()
foreach(_entry IN LISTS _known_root_docs)
    string(REGEX REPLACE "/.*$" "" _entry_component "${_entry}")
    if(NOT _entry_component IN_LIST _components)
        continue()
    endif()
    if(NOT _entry IN_LIST _known_root_docs_used)
        guard_violation("allowlist: запись '${_entry}' устарела — документа больше нет в корне; "
                        "удалите её из _known_root_docs")
    endif()
endforeach()

guard_end("раскладка подкаталогов: реализация вне корня, подкаталоги объявлены, корень — только интерфейс")

