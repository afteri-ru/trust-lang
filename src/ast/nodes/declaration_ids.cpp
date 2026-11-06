#include "ast/declaration_ids.hpp"

#include "ast/ast_nodes.hpp"
#include "ast/slots/slot_engine.hpp"
#include "location/decl_id.hpp"
#include "target/decl_cpp_facts.hpp"

#include <cstdint>
#include <string_view>
#include <vector>

namespace trust {

namespace {

/// Объявление-носитель id (R23): только `Decl`-классы. Иные узлы объявлениями этой оси не являются.
[[nodiscard]] Decl* declarationOf(AstNodeBase& node) {
    if (node.is<VarDecl>()) {
        return node.as<VarDecl>();
    }
    if (node.is<FuncDecl>()) {
        return node.as<FuncDecl>();
    }
    if (node.is<ClassDecl>()) {
        return node.as<ClassDecl>();
    }
    if (node.is<RecordDecl>()) {
        return node.as<RecordDecl>();
    }
    return nullptr;
}

[[nodiscard]] const Decl* declarationOf(const AstNodeBase& node) {
    if (node.is<VarDecl>()) {
        return node.as<VarDecl>();
    }
    if (node.is<FuncDecl>()) {
        return node.as<FuncDecl>();
    }
    if (node.is<ClassDecl>()) {
        return node.as<ClassDecl>();
    }
    if (node.is<RecordDecl>()) {
        return node.as<RecordDecl>();
    }
    return nullptr;
}

/// Первое объявление-носитель id в поддереве (pre-order): `TypeDecl`-обёртка несёт объявление
/// (`ClassDecl`/`RecordDecl`) ребёнком; alias-`TypeDecl` такого ребёнка не имеет.
[[nodiscard]] const Decl* declarationIn(const AstNodeBase& node) {
    if (const Decl* decl = declarationOf(node)) {
        return decl;
    }
    for (const AstNodePtr& child : node.children()) {
        if (child) {
            if (const Decl* decl = declarationIn(*child)) {
                return decl;
            }
        }
    }
    return nullptr;
}

/// Сборщик ВСЕХ owned-слотов (Traversed + Internal): объявления блока интерфейса и типовых
/// параметров живут в Internal-слотах и в `children()` не попадают. Клон обходит узлы тем же
/// способом (`AstSlotVisitor`), поэтому идентичность назначается всем объявлениям дерева.
class OwnedChildCollector final : public AstSlotVisitor {
  public:
    void owned(AstNodePtr& slot, SlotWalk) override {
        if (slot) {
            m_nodes.push_back(slot.get());
        }
    }
    void nonOwning(const AstNodeBase*&) override {}
    void nonOwningFunc(const FuncDecl*&) override {}

    std::vector<AstNodeBase*> m_nodes;
};

} // namespace

DeclId declarationIdOf(const AstNodeBase& node) noexcept {
    if (const Decl* decl = declarationOf(node)) {
        return decl->declId();
    }
    // `TypeDecl`-обёртка: своя идентичность — у alias (нет объявления-ребёнка); у типа с телом
    // идентичность принадлежит `ClassDecl`/`RecordDecl` (одна идентичность на объявление).
    if (node.kind() == ParserToken::Kind::TypeDecl) {
        const Binary& wrapper = *node.as<Binary>();
        if (wrapper.declId().isValid()) {
            return wrapper.declId();
        }
        if (const Decl* child = declarationIn(node)) {
            return child->declId();
        }
    }
    return DeclId{};
}

void assignDeclarationIds(const std::vector<AstNodePtr>& roots, std::string_view moduleLogicalName, DeclCppFacts* declFacts) {
    const std::uint32_t ownerHash = declarationOwnerHash(moduleLogicalName);
    std::uint32_t ordinal = 0;
    // Pre-order (LIFO, дети кладутся в обратном порядке): порядок обхода фиксирован, поэтому повторный
    // вызов даёт те же id (идемпотентность), а порядок загрузки независимых модулей на id не влияет.
    std::vector<AstNodeBase*> stack;
    for (auto it = roots.rbegin(); it != roots.rend(); ++it) {
        if (*it) {
            stack.push_back(it->get());
        }
    }
    while (!stack.empty()) {
        AstNodeBase* node = stack.back();
        stack.pop_back();
        if (Decl* decl = declarationOf(*node)) {
            decl->m_declId = DeclId::fromParts(ownerHash, ++ordinal);
            // Пересменка временного носителя C++-имени конвертера в хранилище (R1): узел имени НЕ хранит.
            if (declFacts != nullptr && decl->m_pendingNativeCppName.has_value()) {
                declFacts->setCppName(decl->m_declId, std::move(*decl->m_pendingNativeCppName));
                decl->m_pendingNativeCppName.reset();
            }
        } else if (node->kind() == ParserToken::Kind::TypeDecl && declarationIn(*node) == nullptr) {
            // `TypeDecl` без объявления-ребёнка (alias/`::= <RHS без тела>`) — это САМО объявление
            // (R23): id живёт на обёртке. Для типа с телом id остаётся у ClassDecl/RecordDecl,
            // чтобы у одного объявления была ровно одна идентичность.
            node->as<Binary>()->m_declId = DeclId::fromParts(ownerHash, ++ordinal);
        }
        // Обход ВСЕХ owned-слотов (как клон): объявления блока интерфейса/типовых параметров
        // находятся в Internal-слотах и обязаны получить DeclId (R23).
        OwnedChildCollector collector;
        node->forEachSlot(collector);
        for (auto it = collector.m_nodes.rbegin(); it != collector.m_nodes.rend(); ++it) {
            stack.push_back(*it);
        }
    }
}

} // namespace trust
