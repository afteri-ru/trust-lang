// Generic-потребители обхода AST: состав узла берётся из класс-таблицы слотов
// (`ast/slots/node_slots_x.hpp`) через `forEachSlot` класса (генерируется `AST_SLOT_METHOD`,
// см. ast/slots/slot_engine.hpp). Обход идёт по слотам класса.
#include "ast/ast_nodes.hpp"
#include "ast/slots/slot_engine.hpp"

#include <vector>

namespace trust {

namespace {

/// Собирает слоты, видимые generic-обходу (`SlotWalk::Traversed`). Указатели на слоты
/// позволяют заменять узлы при обходе (раскрытие ContextMacro, ErrorExpr-recovery).
class TraversedSlotCollector final : public AstSlotVisitor {
  public:
    explicit TraversedSlotCollector(std::vector<AstNodePtr*>& out)
    : m_out(out) {}

    void owned(AstNodePtr& slot, SlotWalk walk) override {
        if (walk == SlotWalk::Traversed) {
            m_out.push_back(&slot);
        }
    }
    void nonOwning(const AstNodeBase*&) override {}
    void nonOwningFunc(const FuncDecl*&) override {}

  private:
    std::vector<AstNodePtr*>& m_out;
};

} // namespace

void AstNodeBase::collectChildren(std::vector<AstNodePtr*>& out) {
    TraversedSlotCollector collector(out);
    forEachSlot(collector);
}

std::vector<AstNodePtr> AstNodeBase::children() const {
    std::vector<AstNodePtr*> slots;
    const_cast<AstNodeBase*>(this)->collectChildren(slots);
    std::vector<AstNodePtr> out;
    out.reserve(slots.size());
    for (AstNodePtr* slot : slots) {
        if (slot != nullptr && *slot != nullptr) {
            out.push_back(*slot);
        }
    }
    return out;
}

} // namespace trust
