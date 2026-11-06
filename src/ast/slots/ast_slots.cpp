// Базовая реализация `AstNodeBase::forEachSlot` — слоты, принадлежащие ЛЮБОМУ узлу.
// Состав слотов конкретных классов задаётся ЕДИНСТВЕННОЙ класс-таблицей
// `ast/slots/node_slots_x.hpp`; метод класса генерируется макросом `AST_SLOT_METHOD`
// (см. ast/slots/slot_engine.hpp). Здесь — только базовый случай.
#include "ast/token_base.hpp"

namespace trust {

void AstNodeBase::forEachSlot(AstSlotVisitor& v) {
    if (m_kind == ParserToken::Kind::END) {
        FAULT("forEachSlot: END is not a node");
    }
    // Trust-контракты принадлежат ЛЮБОМУ узлу и не входят в generic-обход (Internal).
    for (AstNodePtr& contract : m_trust) {
        if (contract) {
            v.owned(contract, SlotWalk::Internal);
        }
    }
}

} // namespace trust
