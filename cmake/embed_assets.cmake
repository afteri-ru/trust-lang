# cmake/embed_assets.cmake
# Генерация C++-инициализатора байтового массива из файла-ассета.
#
# Ассет превращается в список байтов `0xNN,...`, который TU включает внутри инициализатора массива:
#     static const unsigned char kX[] = { #include "lsp/x.inc" };
# Это стандартный C++ без расширений компилятора, поэтому ассеты встраиваются одинаково clang/gcc
# (R47: целевая матрица — clang + g++, стандарт из целевого контракта).
#
# Детерминизм: содержимое .inc зависит только от БАЙТОВ ассета (путь в комментарии —
# относительный), поэтому повторная конфигурация не меняет результат (R42).
#
# Использование: trust_embed_asset(<out.inc> FILE <абсолютный путь к ассету>)
include_guard(GLOBAL)

function(trust_embed_asset out_file)
    cmake_parse_arguments(E "" "FILE" "" ${ARGN})
    if(NOT E_FILE)
        message(FATAL_ERROR "trust_embed_asset: FILE не задан")
    endif()
    if(NOT IS_ABSOLUTE "${E_FILE}")
        message(FATAL_ERROR "trust_embed_asset: FILE должен быть абсолютным путём ('${E_FILE}')")
    endif()
    if(NOT EXISTS "${E_FILE}")
        message(FATAL_ERROR "trust_embed_asset: ассет '${E_FILE}' не найден")
    endif()
    set(_asset_abs "${E_FILE}")

    file(READ "${_asset_abs}" _hex HEX)
    # Массив нулевого размера некорректен в C++ — пустой ассет является ошибкой.
    if(_hex STREQUAL "")
        message(FATAL_ERROR "trust_embed_asset: ассет '${E_FILE}' пуст (массив нулевого размера не C++)")
    endif()

    string(REGEX REPLACE "([0-9a-f][0-9a-f])" "0x\\1," _bytes "${_hex}")
    # 16 байт в строке — читаемость диффа сгенерированного файла.
    string(REGEX REPLACE "((0x..,){16})" "\\1\n" _bytes "${_bytes}")
    # Хвостовая запятая снимается: .inc — готовый СПИСОК инициализатора, который TU дополняет
    # (например `, 0}` для NUL-терминированной лицензии); вторая запятая была бы синтаксической ошибкой.
    string(REGEX REPLACE "[\n\r]+$" "" _bytes "${_bytes}")
    string(REGEX REPLACE ",$" "" _bytes "${_bytes}")

    file(RELATIVE_PATH _asset_rel "${CMAKE_SOURCE_DIR}" "${_asset_abs}")
    get_filename_component(_out_dir "${out_file}" DIRECTORY)
    file(MAKE_DIRECTORY "${_out_dir}")
    file(WRITE "${out_file}"
        "// Сгенерировано из ${_asset_rel} (cmake/embed_assets.cmake). Не редактировать.\n${_bytes}\n")

    # Правка ассета обязана перегенерировать .inc на следующей конфигурации.
    set_property(DIRECTORY APPEND PROPERTY CMAKE_CONFIGURE_DEPENDS "${_asset_abs}")
endfunction()