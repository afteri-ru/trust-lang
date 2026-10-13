# cmake/check_ast_slots.cmake
# CI-хранитель ЕДИНСТВЕННОГО источника состава AST-узла (ast/slots/node_slots_x.hpp).
#
# Правила:
#   1) каждое поле-слот (`AstNodePtr`, `std::vector<AstNodePtr>`, `std::optional<...>`,
#      `const AstNodeBase*`, `const FuncDecl*`, их списки/опциональные списки) объявлено
#      строкой класс-таблицы — иначе узел «потеряет» слот в обходе/клоне;
#   2) каждый класс с непустыми строками объявляет `AST_SLOT_METHOD(..., <ROWS>)`;
#   3) каждая строка-макрос из `AST_SLOT_CLASSES`/`AST_SLOT_BASE_CLASSES` определена.
#
# Использование: cmake -DROOT=<корень> -P cmake/check_ast_slots.cmake
# Возврат: 0 — нарушений нет, 1 — есть нарушения.

include("${CMAKE_CURRENT_LIST_DIR}/guard_common.cmake")

guard_begin(ast_slots_guard)

set(_table "${ROOT}/include/ast/slots/node_slots_x.hpp")
if(NOT EXISTS "${_table}")
    message(FATAL_ERROR "check_ast_slots: missing ${_table}")
endif()
file(READ "${_table}" _table_text)
# Комментарии из таблицы исключаются: упоминание имени поля в пояснении не является объявлением строки.
string(REGEX REPLACE "//[^\n]*" "" _table_text "${_table_text}")

# -- 1) Поля-слоты объявлены в таблице ----------------------------------------------------
# Исключения (с обоснованием):
#   m_trust  — слот ЛЮБОГО узла; эмитится `AstNodeBase::forEachSlot` (движок), не класс-строкой;
#   origin   — соответствие AST↔HIR, не слот (ремапится lowering-контрактом);
#   patterns/body — поля `MatchStmt::MatchCase`, агрегата строки `CaseList m_cases`.
set(_allow_fields
    "token_base.hpp:m_trust"
    "token_base.hpp:origin"
    "stmt_nodes.hpp:patterns"
    "stmt_nodes.hpp:body"
)

set(_slot_field_regex
    "^[ \t]*(mutable +)?(AstNodePtr|std::vector<AstNodePtr>|std::optional<std::vector<AstNodePtr>>|const AstNodeBase\\*|const FuncDecl\\*|std::vector<const AstNodeBase\\*>|std::optional<std::vector<const AstNodeBase\\*>>)[ \t]+([A-Za-z_][A-Za-z0-9_]*)[ \t]*(=[^|]*|\\{[^|]*)?\\|"
)

guard_files(_headers ROOTS "${ROOT}/include/ast" GLOBS "*.hpp")
if(NOT _headers)
    message(FATAL_ERROR "check_ast_slots: нет заголовков AST в ${ROOT}/include/ast")
endif()
foreach(_header IN LISTS _headers)
    get_filename_component(_name "${_header}" NAME)
    # ';' заменяется: file(STRINGS)/list-разбиение CMake режет строки по ';', а нам нужны
    # целые строки объявлений (признак объявления-поля — завершающий ';', здесь '|').
    file(READ "${_header}" _raw)
    string(REPLACE ";" "|" _raw "${_raw}")
    string(REPLACE "\n" ";" _lines "${_raw}")
    foreach(_line IN LISTS _lines)
        if(_line MATCHES "${_slot_field_regex}")
            set(_field "${CMAKE_MATCH_3}")
            if(AST_SLOTS_GUARD_DEBUG)
                message(STATUS "check_ast_slots: поле-слот ${_name}:${_field}")
            endif()
            list(FIND _allow_fields "${_name}:${_field}" _allowed)
            if(_allowed GREATER -1)
                continue()
            endif()
            string(FIND "${_table_text}" "${_field}" _pos)
            if(_pos EQUAL -1)
                guard_violation("${_name}: слот '${_field}' не объявлен в ast/slots/node_slots_x.hpp")
            endif()
        endif()
    endforeach()
endforeach()

# -- 2/3) Классы таблицы и строки-макросы -------------------------------------------------
# Пустая строка-макрос означает «у класса нет собственных слотов»: метод не нужен
# (наследуется базовая/родительская реализация).

set(_all_headers_text "")
foreach(_header IN LISTS _headers)
    file(READ "${_header}" _text)
    string(APPEND _all_headers_text "${_text}\n")
endforeach()

# Классы с kind (`AST_SLOT_CLASSES`) и абстрактные базы (`AST_SLOT_BASE_CLASSES`) —
# обе таблицы валидируются одинаково.
set(_class_entries "")
foreach(_block_regex IN ITEMS "AST_SLOT_CLASSES\\(C\\)[^#]*" "AST_SLOT_BASE_CLASSES\\(C\\)[^#]*")
    string(REGEX MATCH "${_block_regex}" _block "${_table_text}")
    string(REGEX MATCHALL "C\\([A-Za-z_][A-Za-z0-9_]*, *[A-Za-z_][A-Za-z0-9_]*\\)" _entries "${_block}")
    list(APPEND _class_entries ${_entries})
endforeach()
if(NOT _class_entries)
    message(FATAL_ERROR "check_ast_slots: таблица ${_table} не содержит AST_SLOT_CLASSES")
endif()
foreach(_entry IN LISTS _class_entries)
    string(REGEX REPLACE "^C\\(([A-Za-z_][A-Za-z0-9_]*), *([A-Za-z_][A-Za-z0-9_]*)\\)$" "\\1" _cls "${_entry}")
    string(REGEX REPLACE "^C\\(([A-Za-z_][A-Za-z0-9_]*), *([A-Za-z_][A-Za-z0-9_]*)\\)$" "\\2" _rows "${_entry}")

    if(NOT _table_text MATCHES "#define ${_rows}\\(X\\)")
        guard_violation("таблица: строка-макрос '${_rows}' (${_cls}) не определена")
        continue()
    endif()

    # Тело строки-макроса: первая строка после `#define ROWS(X)` или та же строка.
    string(REGEX MATCH "#define ${_rows}\\(X\\)([^\n]*)" _row_head "${_table_text}")
    set(_row_body "${CMAKE_MATCH_1}")
    string(REGEX MATCH "#define ${_rows}\\(X\\)[^\n]*\n([^\n]*)" _row_next "${_table_text}")
    string(APPEND _row_body "${CMAKE_MATCH_1}")
    string(FIND "${_row_body}" "X(" _has_row)
    if(_has_row EQUAL -1)
        continue()
    endif()

    if(NOT _all_headers_text MATCHES "AST_SLOT_METHOD\\([A-Za-z_<>:]+, *${_rows}\\)")
        guard_violation("класс '${_cls}': нет AST_SLOT_METHOD(..., ${_rows}) (слоты таблицы не применяются)")
    endif()
endforeach()

guard_end("единый источник состава узлов нарушен")
