// src/semantic/borrow_check.cpp
// Реализация BorrowCheckHook - статический анализатор заимствования умных ссылок
// (см. include/semantic/borrow_check.hpp). Структурная детекция по объявлению/инициализатору.

#include "semantic/borrow_check.hpp"

#include "attrs/attr_pool.hpp"
#include "ast/binary_op.hpp"
#include "ast/ident_name.hpp"
#include "ast/ref_syntax.hpp"
#include "diag/options.hpp"
#include "utils/trace.hpp"

#include <optional>
#include <string>

namespace trust {

BorrowCheckHook::BorrowCheckHook(AnalysisContext& actx)
: m_actx(actx) {
    m_frames.emplace_back(); // глобальный скоуп (depth >= 1)
}

void BorrowCheckHook::enterScope() {
    m_frames.emplace_back();
    m_epoch.push();
}

void BorrowCheckHook::exitScope() {
    if (m_frames.size() > 1) {
        m_frames.pop_back();
    }
    m_epoch.pop();
}

BorrowRegion BorrowCheckHook::regionOfStorage(Storage storage) noexcept {
    switch (storage) {
    case Storage::Local:
        return BorrowRegion::Scope;
    case Storage::ThreadLocal:
        return BorrowRegion::Thread;
    case Storage::Static:
    case Storage::Global:
        return BorrowRegion::Program;
    }
    return BorrowRegion::Scope;
}

std::string BorrowCheckHook::bareName(std::string_view name) {
    if (!name.empty() && name.front() == '$') {
        return std::string(name.substr(1));
    }
    return std::string(name);
}

BorrowRegion BorrowCheckHook::regionOfVar(const VarDecl& var) const {
    const AttrPool& attrs = m_actx.ctx().attrs();
    if (var.has_attr(attrs, attr::ThreadLocal)) {
        return BorrowRegion::Thread;
    }
    const std::string name(var.text());
    if (name.find("::") != std::string::npos) {
        return BorrowRegion::Program; // статическая переменная области имён
    }
    if (m_actx.currentFunc() != nullptr) {
        return BorrowRegion::Scope; // локальная/временная
    }
    return BorrowRegion::Program; // модуль/глобал
}

std::string BorrowCheckHook::sourceOf(const AstNodeBase* node) {
    if (node == nullptr) {
        return {};
    }
    if (node->kind() == ParserToken::Kind::Ident) {
        const auto& id = *node->as<IdentName>();
        const std::string t(id.text());
        return (t.empty() || t == "_") ? std::string{} : bareName(t);
    }
    if (node->kind() == ParserToken::Kind::ArrayAccess || node->kind() == ParserToken::Kind::MemberAccess) {
        const auto& b = *node->as<Binary>();
        return sourceOf(b.m_left.get());
    }
    if (node->is<RefMakeExpr>() || node->is<RefTakeExpr>()) {
        const auto& s = *node->as<Sequence>();
        if (!s.m_body.empty()) {
            return sourceOf(s.m_body[0].get());
        }
        return {};
    }
    return {};
}

const BorrowCheckHook::SmartInfo* BorrowCheckHook::findSmart(const std::string& bare) const {
    for (auto it = m_frames.rbegin(); it != m_frames.rend(); ++it) {
        auto found = it->smart.find(bare);
        if (found != it->smart.end()) {
            return &found->second;
        }
    }
    return nullptr;
}

const BorrowCheckHook::Frame* BorrowCheckHook::frameWithBorrow(const std::string& dep, std::pair<std::string, int64_t>* out) const {
    for (auto it = m_frames.rbegin(); it != m_frames.rend(); ++it) {
        auto found = it->borrows.find(dep);
        if (found != it->borrows.end()) {
            if (out != nullptr) {
                *out = found->second;
            }
            return &*it;
        }
    }
    return nullptr;
}

bool BorrowCheckHook::hasBorrows(const std::string& anchor) const {
    for (auto it = m_frames.rbegin(); it != m_frames.rend(); ++it) {
        for (const auto& kv : it->borrows) {
            if (kv.second.first == anchor) {
                return true;
            }
        }
    }
    return false;
}

void BorrowCheckHook::analyzeVar(const VarDecl& var) {
    const AttrPool& attrs = m_actx.ctx().attrs();
    const std::string name = bareName(var.text());
    if (name.empty() || name == "_") {
        return;
    }
    m_epoch.declare(name);

    // Вид ссылки: маркер/атрибут у переменной ИЛИ у аннотации типа.
    std::optional<RefType> kind = refKindOfAttr(attrs, var);
    if (!kind.has_value() && var.m_type) {
        kind = refKindOfTypeSpec(var.m_type.get(), attrs);
    }

    if (kind.has_value()) {
        SmartInfo info;
        info.kind = *kind;
        info.owner = (*kind == RefType::kUnique || *kind == RefType::kShared);
        info.region = regionOfVar(var);
        m_frames.back().smart[name] = info;
        TRUST_DEBUG("borrow", "decl '{}' kind={} owner={} region={} depth={}", name, refTypeName(info.kind), info.owner ? "yes" : "no",
                    borrowRegionName(info.region), m_frames.size());
    }

    // Заём (невладеющий view): `w := & x` (weak) адресует anchor `x`.
    const AstNodeBase* init = var.m_initializer.get();
    if (init != nullptr && init->kind() == ParserToken::Kind::RefMakeExpr) {
        const std::string anchor = sourceOf(init);
        if (!anchor.empty() && anchor != name) {
            // R1: region(view) <= region(anchor). View не должен переживать anchor.
            const SmartInfo* anchorInfo = findSmart(anchor);
            const BorrowRegion viewRegion = regionOfVar(var);
            if (anchorInfo != nullptr && viewRegion > anchorInfo->region) {
                TRUST_DEBUG("borrow", "R1 region-mismatch view '{}'({}) outlives anchor '{}'({})", name, borrowRegionName(viewRegion), anchor,
                            borrowRegionName(anchorInfo->region));
                m_actx.ctx().report(var.range(), semantic::DiagId::BorrowRegionMismatch,
                                    "borrowed view '{}' (region '{}') outlives its anchor '{}' (region '{}')", name, borrowRegionName(viewRegion), anchor,
                                    borrowRegionName(anchorInfo->region));
            }
            m_frames.back().borrows[name] = {anchor, m_epoch.epochOf(anchor)};
            TRUST_DEBUG("borrow", "view '{}' -> anchor '{}' born-epoch={} region={}", name, anchor, m_epoch.epochOf(anchor), borrowRegionName(viewRegion));
        }
    }
}

void BorrowCheckHook::analyzeAssign(const Binary& b) {
    if (b.m_left == nullptr) {
        return;
    }
    const bool swap = isSwapOp(b.m_op);
    const bool plain = isPlainAssignOp(b.m_op);
    if (!swap && !plain) {
        return;
    }
    // Левый объект - не только имя, но и разыменование/поле/элемент (`*s = 7`, `o.f = ...`,
    // `a[i] = ...`). Мутация адресуется КОРНЕВОМУ источнику (sourceOf: Ident / RefMakeExpr /
    // RefTakeExpr / MemberAccess / ArrayAccess); иначе событие мутации терялось и per-frame эпоха
    // anchor'а не инкрементировалась (заёмы не инвалидировались).
    const std::string lhs = sourceOf(b.m_left.get());
    if (lhs.empty() || lhs == "_") {
        return;
    }
    // R4/R6: запись (R6) / move-swap (R4) владельца при живом займе - нарушение заимствования.
    // R6 - только ЭКСКЛЮЗИВНЫЙ владелец (`unique`): мутация при живом займе нарушает
    // aliasing XOR mutability. Для `shared` мутация допустима (разделяемый доступ на одни данные,
    // истечение weak - refcount-семантика), поэтому R6 для `shared` НЕ срабатывает.
    if (hasBorrows(lhs)) {
        const SmartInfo* li = findSmart(lhs);
        if (li != nullptr && li->owner) {
            if (swap) {
                TRUST_DEBUG("borrow", "R4 owner-moved '{}' kind={} while borrowed", lhs, refTypeName(li->kind));
                m_actx.ctx().report(b.range(), semantic::DiagId::BorrowOwnerMoved, "cannot move/swap owner '{}' while it is borrowed", lhs);
            } else if (li->kind == RefType::kUnique) {
                TRUST_DEBUG("borrow", "R6 owner-mutated '{}' while borrowed", lhs);
                m_actx.ctx().report(b.range(), semantic::DiagId::BorrowOwnerMutated, "cannot mutate owner '{}' while it is borrowed", lhs);
            }
        }
    }
    // Любая запись в anchor инвалидирует рождённые до неё заёмы (per-frame эпоха).
    m_epoch.mutate(lhs, b.range());
    TRUST_DEBUG("borrow", "mutate '{}' op={} epoch={} live-borrow={}", lhs, swap ? "swap" : "assign", m_epoch.epochOf(lhs), hasBorrows(lhs) ? "yes" : "no");

    // Swap двух владельцев: проверяем и правый операнд.
    if (swap && b.m_right != nullptr) {
        const std::string rhs = sourceOf(b.m_right.get());
        if (!rhs.empty() && rhs != "_") {
            if (hasBorrows(rhs)) {
                const SmartInfo* ri = findSmart(rhs);
                if (ri != nullptr && ri->owner) {
                    TRUST_DEBUG("borrow", "R4 owner-moved '{}' kind={} while borrowed", rhs, refTypeName(ri->kind));
                    m_actx.ctx().report(b.range(), semantic::DiagId::BorrowOwnerMoved, "cannot move/swap owner '{}' while it is borrowed", rhs);
                }
            }
            m_epoch.mutate(rhs, b.range());
            TRUST_DEBUG("borrow", "mutate '{}' op=swap epoch={} live-borrow={}", rhs, m_epoch.epochOf(rhs), hasBorrows(rhs) ? "yes" : "no");
        }
    }
}

bool BorrowCheckHook::onNode(AstNodePtr& node) {
    if (node == nullptr) {
        return false;
    }
    if (node->is<VarDecl>()) {
        analyzeVar(*node->as<VarDecl>());
        return false;
    }
    if (node->kind() == ParserToken::Kind::AssignOp) {
        analyzeAssign(*node->as<Binary>());
        return false;
    }
    return false;
}

} // namespace trust
