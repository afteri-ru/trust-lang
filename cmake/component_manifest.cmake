# cmake/component_manifest.cmake
# Разбор манифеста trust_component(...) из include/<component>/CMakeLists.txt.
#
# Единственный парсер манифеста: его используют CI-гейты (public_api, internal_boundary,
# internal_memory, component_deps). Гейты выполняются в script-режиме `cmake -P` и не имеют
# доступа к целям/свойствам конфигурации, поэтому читают манифест из текста CMakeLists.
#
# trust_manifest_parse(<cmakelists> <prefix>) кладёт в PARENT_SCOPE:
#   <prefix>_NAME          имя компонента (первый аргумент trust_component)
#   <prefix>_TARGET        TARGET <name> (пусто, если не задан)
#   <prefix>_INTERFACE     "TRUE"/"" — header-only цель
#   <prefix>_NO_LIB        "TRUE"/"" — компонент без библиотечной цели
#   <prefix>_PUBLIC_HEADERS / _PUBLIC_DIRS / _INTERNAL_DIRS
#   <prefix>_LINK_PUBLIC / _LINK_PRIVATE
# Значения вида ${...} и $<...> — подстановки CMake, а не имена: пропускаются.

set(TRUST_MANIFEST_KEYWORDS
    TARGET INTERFACE NO_LIB
    PUBLIC_HEADERS PUBLIC_DIRS INTERNAL_DIRS SOURCES
    LINK_PUBLIC LINK_PRIVATE
    INCLUDE_PUBLIC INCLUDE_PRIVATE SYSTEM_INCLUDE_PRIVATE
    COMPILE_OPTIONS PUBLIC_COMPILE_OPTIONS DEPENDS
)

# Ключевые слова, значения которых нужны гейтам. Остальные (SOURCES, INCLUDE_*, OPTIONS,
# DEPENDS) остаются в TRUST_MANIFEST_KEYWORDS как границы секций, но не собираются.
set(TRUST_MANIFEST_VALUE_KEYS
    PUBLIC_HEADERS PUBLIC_DIRS INTERNAL_DIRS LINK_PUBLIC LINK_PRIVATE
)

function(trust_manifest_parse _cmakelists _prefix)
    if(NOT EXISTS "${_cmakelists}")
        message(FATAL_ERROR "trust_manifest_parse: файл не найден: ${_cmakelists}")
    endif()

    file(READ "${_cmakelists}" _content)
    # Комментарии не несут данных манифеста, но содержат скобки и не-ASCII текст, на котором
    # file(STRINGS) разрывает строки — поэтому они удаляются до разбора блока.
    string(REGEX REPLACE "#[^\n]*" "" _content "${_content}")

    string(REGEX MATCH "trust_component[(][ ]*([A-Za-z0-9_-]+)" _head "${_content}")
    if(NOT _head)
        message(FATAL_ERROR "trust_manifest_parse: trust_component(...) не найден в ${_cmakelists}")
    endif()
    set(_name "${CMAKE_MATCH_1}")

    # Закрывающая скобка обязана стоять на отдельной строке: иначе конец блока неоднозначен
    # (значение манифеста может содержать ')') и разбор молча обрезал бы манифест.
    string(FIND "${_content}" "trust_component(" _start)
    string(FIND "${_content}" "\n)" _close)
    if(_start EQUAL -1 OR _close EQUAL -1 OR _close LESS _start)
        message(FATAL_ERROR
            "trust_manifest_parse: trust_component(...) должен закрываться ')' на отдельной строке (${_cmakelists})")
    endif()
    math(EXPR _len "${_close} - ${_start}")
    string(SUBSTRING "${_content}" ${_start} ${_len} _block)

    separate_arguments(_tokens UNIX_COMMAND "${_block}")

    foreach(_key IN LISTS TRUST_MANIFEST_VALUE_KEYS)
        set(_values_${_key} "")
    endforeach()
    set(_target "")
    set(_interface "")
    set(_no_lib "")

    set(_current "")
    foreach(_tok IN LISTS _tokens)
        if(_tok MATCHES "^trust_component[(]" OR _tok STREQUAL ")" OR _tok STREQUAL "(")
            continue()
        endif()
        if(_tok IN_LIST TRUST_MANIFEST_KEYWORDS)
            set(_current "${_tok}")
            if(_tok STREQUAL "INTERFACE")
                set(_interface TRUE)
            elseif(_tok STREQUAL "NO_LIB")
                set(_no_lib TRUE)
            endif()
            continue()
        endif()
        if(_tok MATCHES "^[$]")
            continue()
        endif()
        if(_current STREQUAL "TARGET")
            set(_target "${_tok}")
        elseif(_current IN_LIST TRUST_MANIFEST_VALUE_KEYS)
            list(APPEND _values_${_current} "${_tok}")
        endif()
    endforeach()

    set(${_prefix}_NAME "${_name}" PARENT_SCOPE)
    set(${_prefix}_TARGET "${_target}" PARENT_SCOPE)
    set(${_prefix}_INTERFACE "${_interface}" PARENT_SCOPE)
    set(${_prefix}_NO_LIB "${_no_lib}" PARENT_SCOPE)
    foreach(_key IN LISTS TRUST_MANIFEST_VALUE_KEYS)
        set(${_prefix}_${_key} "${_values_${_key}}" PARENT_SCOPE)
    endforeach()
endfunction()
