// Граница «неизменяемая AST → производная форма»: глубокий клон (cloneTree/cloneTrees).
// Реализация двухфазная:
//   1) структура: shallow-copy узла по kind (x-macro) + клон ВСЕХ владеемых слотов;
//   2) ссылки: ремап невладеющих указателей по карте «оригинал → клон».
// Состав и классификация слотов НЕ дублируются здесь: единственный источник —
// класс-таблица `ast/slots/node_slots_x.hpp`, читаемая через `forEachSlot` (ast/slots/slot_engine.hpp).
#include "ast/clone.hpp"

#include "ast/ast_nodes.hpp"
#include "ast/slots/slot_engine.hpp"
#include "utils/error.hpp"

#include <memory>
#include <unordered_map>
#include <vector>

namespace trust {

namespace {

using CloneMap = std::unordered_map<const AstNodeBase*, AstNodePtr>;

/// Shallow-copy узла: копируются все поля класса, дети остаются общими до фазы 1.
[[nodiscard]] AstNodePtr shallowCopy(const AstNodeBase& node) {
    switch (node.kind()) {
#define TRUST_CLONE_COPY(name, type, category) \
    case ParserToken::Kind::name:              \
        return std::make_shared<type>(static_cast<const type&>(node));
        PARSER_TOKEN_KINDS(TRUST_CLONE_COPY)
#undef TRUST_CLONE_COPY
    case ParserToken::Kind::END:
        break;
    }
    FAULT("cloneNode: END is not a node");
}

[[nodiscard]] AstNodePtr cloneRec(const AstNodePtr& source, CloneMap& map);
[[nodiscard]] const AstNodeBase* remapPointer(const AstNodeBase* original, const CloneMap& map);
void remapTree(AstNodeBase& node, const CloneMap& map);

/// Фаза 1: клонирует ВСЕ владеемые слоты узла (Traversed + Internal).
class OwnedSlotCloner final : public AstSlotVisitor {
  public:
    explicit OwnedSlotCloner(CloneMap& map)
    : m_map(map) {}

    void owned(AstNodePtr& slot, SlotWalk /*walk*/) override {
        if (slot) {
            slot = cloneRec(slot, m_map);
        }
    }
    void nonOwning(const AstNodeBase*&) override {}
    void nonOwningFunc(const FuncDecl*&) override {}

  private:
    CloneMap& m_map;
};

/// Фаза 2: ремап невладеющих ссылок ТЕКУЩЕГО узла (без рекурсии).
class NonOwningRemapper final : public AstSlotVisitor {
  public:
    explicit NonOwningRemapper(const CloneMap& map)
    : m_map(map) {}

    void owned(AstNodePtr&, SlotWalk) override {}
    void nonOwning(const AstNodeBase*& slot) override { slot = remapPointer(slot, m_map); }
    void nonOwningFunc(const FuncDecl*& slot) override { slot = static_cast<const FuncDecl*>(remapPointer(slot, m_map)); }

  private:
    const CloneMap& m_map;
};

/// Фаза 2: рекурсивный обход владеемого поддерева клона.
class OwnedSubtreeRemapper final : public AstSlotVisitor {
  public:
    explicit OwnedSubtreeRemapper(const CloneMap& map)
    : m_map(map) {}

    void owned(AstNodePtr& slot, SlotWalk /*walk*/) override {
        if (slot) {
            remapTree(*slot, m_map);
        }
    }
    void nonOwning(const AstNodeBase*&) override {}
    void nonOwningFunc(const FuncDecl*&) override {}

  private:
    const CloneMap& m_map;
};

[[nodiscard]] AstNodePtr cloneRec(const AstNodePtr& source, CloneMap& map) {
    if (!source) {
        return nullptr;
    }
    if (const auto it = map.find(source.get()); it != map.end()) {
        return it->second;
    }
    AstNodePtr clone = shallowCopy(*source);
    // Единственное хранимое направление соответствия AST↔HIR (обратное выводится по требованию).
    clone->origin = source.get();
    map.emplace(source.get(), clone);
    OwnedSlotCloner cloner(map);
    clone->forEachSlot(cloner);
    return clone;
}

[[nodiscard]] const AstNodeBase* remapPointer(const AstNodeBase* original, const CloneMap& map) {
    if (original == nullptr) {
        return nullptr;
    }
    const auto it = map.find(original);
    return it == map.end() ? original : it->second.get();
}

void remapTree(AstNodeBase& node, const CloneMap& map) {
    NonOwningRemapper remapper(map);
    node.forEachSlot(remapper);
    OwnedSubtreeRemapper recurser(map);
    node.forEachSlot(recurser);
}

} // namespace

std::vector<AstNodePtr> cloneTrees(const std::vector<AstNodePtr>& nodes) {
    CloneMap map;
    std::vector<AstNodePtr> clones;
    clones.reserve(nodes.size());
    for (const AstNodePtr& node : nodes) {
        // Позиция сохраняется: пустой корень (удалённая директива/recovery-заглушка) остаётся
        // пустым, чтобы индексы клона совпадали с индексами входа (срез stdlib DSL и счётчики).
        clones.push_back(node ? cloneRec(node, map) : nullptr);
    }
    for (AstNodePtr& clone : clones) {
        if (clone) {
            remapTree(*clone, map);
        }
    }
    return clones;
}

} // namespace trust
