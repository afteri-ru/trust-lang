#pragma once
// Фикстура для ast_slots_guard С НАМЕРЕННЫМ нарушением (self-test, ожидание WILL_FAIL):
//   - у BadNode есть поле-слот вне таблицы;
//   - у MissingMethodNode есть непустые строки, но нет AST_SLOT_METHOD.
#define AST_SLOT_CLASSES(C)                  \
    C(BadNode, SLOTS_BadNode)                \
    C(MissingMethodNode, SLOTS_MissingMethod)

#define AST_SLOT_BASE_CLASSES(C)

#define SLOTS_BadNode(X) X(Ptr, m_registered, Traversed, None)
#define SLOTS_MissingMethod(X) X(Ptr, m_value, Traversed, None)
