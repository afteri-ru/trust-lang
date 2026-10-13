#pragma once
// Фикстура self-test для cmake/check_ast_slots.cmake (ожидается провал guard'а).
#include "ast/slots/node_slots_x.hpp"

class BadNode {
  public:
    AST_SLOT_METHOD(AstNodeBase, SLOTS_BadNode)
    AstNodePtr m_registered;
    AstNodePtr m_orphan; // нарушение: поле-слот не объявлено в таблице
};

class MissingMethodNode {
  public:
    AstNodePtr m_value; // нарушение: строки таблицы есть, AST_SLOT_METHOD отсутствует
};
