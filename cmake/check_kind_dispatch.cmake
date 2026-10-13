# cmake/check_kind_dispatch.cmake
# CI-хранитель единственного механизма kind-диспетчеризации (ast/kind_dispatch.hpp).
#
# Правило: switch/case по ParserToken::Kind разрешён ТОЛЬКО в allowlist-файлах, где решение
# принято и обосновано. Любое новое место диспетчеризации (в т.ч. `case Kind::X` через
# `using Kind = ParserToken::Kind`) ломает сборку - иначе инвариант «один механизм»
# деградирует в договорённость.
#
# Использование: cmake -DROOT=<корень> -P cmake/check_kind_dispatch.cmake
# Возврат: 0 - нарушений нет, 1 - есть нарушения.

include("${CMAKE_CURRENT_LIST_DIR}/guard_common.cmake")

guard_begin(kind_dispatch_guard)

# Файлы, которым разрешена собственная kind-диспетчеризация (с обоснованием):
set(allow
    "include/ast/kind_dispatch.hpp"      # сам механизм: dispatchEmit (switch по всем kinds)
    "include/ast/token.hpp"              # ParserToken::name(): исчерпывающий switch (все kinds)
    "include/ast/ast_nodes.hpp"          # is<T>(): исчерпывающий switch (все kinds + END)
    "include/ast/token_base.hpp"         # canHaveImmutableQualifier(): частичный предикат (макрос)
    "include/ast/nodes/expr_nodes.hpp"         # is_block_kind(): исчерпывающий switch (все kinds + END)
    "include/semantic/typer/type_inference.hpp" # literalType(): частичный предикат литералов (помечен макросом)
    "src/ast/nodes/clone.cpp"                  # структурный клон: kind→класс shallow-copy
    "src/ir/hir/lowering.cpp"               # lowering HIR: kind→класс трансформация
    "src/semantic/core/capture_last_result.cpp" # пре-семантическое переписывание $^: kind→scope-обход
    "src/transpiler/transpiler.cpp"      # предикат top-level определений single-file режима
    "src/solver/impl/smt_expr_enc.cpp"        # SMT-кодирование выражений (частичное, с диагностикой)
    "src/module_loader/module_export.cpp" # предикат экспортируемых объявлений
)

guard_files(_sources ROOTS "${ROOT}/src" "${ROOT}/include" GLOBS "*.cpp" "*.hpp")

foreach(_file IN LISTS _sources)
    file(RELATIVE_PATH _rel "${ROOT}" "${_file}")
    list(FIND allow "${_rel}" _allowed)
    if(_allowed GREATER -1)
        continue()
    endif()

    file(STRINGS "${_file}" _case_hits REGEX "case[ \t]+(ParserToken::)?Kind::")
    file(STRINGS "${_file}" _switch_hits REGEX "switch[ \t]*\\([^)]*kind\\(\\)")
    file(STRINGS "${_file}" _selfkind_hits REGEX "(^|[^A-Za-z0-9_])self_kind([^A-Za-z0-9_]|$)")

    if(_case_hits)
        guard_violation("${_rel}: case по Kind вне allowlist")
    endif()
    if(_switch_hits)
        guard_violation("${_rel}: switch по kind() вне allowlist")
    endif()
    if(_selfkind_hits)
        guard_violation("${_rel}: if-цепочка self_kind запрещена")
    endif()

    # В точках диспетчеризации (маркеры KIND_DISPATCH_GUARD_BEGIN/END) запрещены и
    # if-цепочки по kind(): они не покрываются проверкой case/switch.
    file(STRINGS "${_file}" _guard_lines)
    set(_in_dispatch_block FALSE)
    foreach(_line IN LISTS _guard_lines)
        if(_line MATCHES "KIND_DISPATCH_GUARD_BEGIN")
            set(_in_dispatch_block TRUE)
        elseif(_line MATCHES "KIND_DISPATCH_GUARD_END")
            set(_in_dispatch_block FALSE)
        elseif(_in_dispatch_block AND _line MATCHES "(^|[^A-Za-z0-9_])(if|switch)[ \\t]*\\([^)]*kind\\(\\)")
            guard_violation("${_rel}: if/switch по kind() внутри KIND_DISPATCH_GUARD-блока")
        endif()
    endforeach()

endforeach()

guard_end("kind-диспетчеризация вне ast/kind_dispatch.hpp запрещена (обновите allowlist с обоснованием)")
