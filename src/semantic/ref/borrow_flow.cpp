// src/semantic/ref/borrow_flow.cpp
// Flow-sensitive анализ инвалидации производных представлений (см.
// include/semantic/ref/borrow_flow.hpp). CFG + forward may-анализ живости + backward liveness.

#include "semantic/ref/borrow_flow.hpp"
#include "semantic/core/local_name.hpp"

#include "ir/cfg.hpp"
#include "ir/dataflow.hpp"
#include "ir/place_root.hpp"
#include "ast/ast_nodes.hpp"
#include "ast/binary_op.hpp"
#include "ast/ident_name.hpp"
#include "ast/ref_syntax.hpp"
#include "attrs/attr_args.hpp"
#include "attrs/attr_builtin.hpp"
#include "attrs/attr_pool.hpp"
#include "semantic/diag.hpp"
#include "session/context.hpp"
#include "types/typekind.hpp"
#include "utils/error.hpp"
#include "utils/utils.hpp"

#include <algorithm>
#include <cstdint>
#include <map>
#include <set>
#include <string>
#include <utility>
#include <vector>

namespace trust::semantic {
namespace {

using flow::ControlFlowGraph;
using flow::Direction;
using flow::FlowBlock;

/// Регион времени жизни (порядок: scope < thread < program).
enum class Region : std::uint8_t { Scope = 0, Thread = 1, Program = 2 };

[[nodiscard]] std::string_view regionName(Region r) {
    switch (r) {
    case Region::Scope:
        return "scope";
    case Region::Thread:
        return "thread";
    case Region::Program:
        return "program";
    }
    FAULT("unknown lifetime region value {}", static_cast<int>(r));
}

/// Одно производное представление (view).
struct Instance {
    const AstNodeBase* place = nullptr;  ///< место представления (VarDecl / цель присваивания) = точка рождения
    const AstNodeBase* source = nullptr; ///< корневое место источника
    const AstNodeBase* scope = nullptr;  ///< скоуп рождения (kill на ScopeExit)
    bool weak = false;                   ///< weak-наблюдатель (иначе @[borrowed])
};

/// Факты одной функции/лямбды, собранные статически по AST.
struct FnScan {
    std::vector<Instance> inst;
    std::map<const AstNodeBase*, std::vector<int>> byPlace;                      ///< view place -> instances
    std::map<const AstNodeBase*, int> rebindAt;                                  ///< AssignOp -> instance
    std::map<const AstNodeBase*, const AstNodeBase*> callReceiver;               ///< CallExpr -> receiver root
    std::map<const AstNodeBase*, RefType> declKind;                              ///< source kind (shared/unique/...)
    std::map<const AstNodeBase*, Region> declRegion;                             ///< место -> регион
    std::map<const AstNodeBase*, std::vector<const AstNodeBase*>> escapeOnWrite; ///< AssignOp -> утёкшие view places
    std::map<const AstNodeBase*, const AstNodeBase*> returnView;                 ///< JumpStmt(return) -> view place
};

struct ViewBits {
    using State = std::vector<std::uint8_t>;
};

/// Backward liveness: «представление используется позже на некотором пути».
struct Liveness {
    using State = ViewBits::State;
    static constexpr Direction direction = Direction::Backward;

    const FnScan& fs;
    std::size_t count;

    [[nodiscard]] State boundary() const { return State(count, 0); }
    [[nodiscard]] State bottom() const { return boundary(); }
    [[nodiscard]] State unreachableState() const { return boundary(); }

    [[nodiscard]] bool join(State& dst, const State& src) const {
        bool changed = false;
        for (std::size_t i = 0; i < count; ++i) {
            if (src[i] != 0 && dst[i] == 0) {
                dst[i] = 1;
                changed = true;
            }
        }
        return changed;
    }

    [[nodiscard]] State transferElement(const MirInstruction& e, State state) const {
        const auto clearPlace = [&](const AstNodeBase* place) {
            for (std::size_t i = 0; i < count; ++i) {
                if (fs.inst[i].place == place) {
                    state[i] = 0;
                }
            }
        };
        switch (e.kind) {
        case MirOp::Read:
            for (std::size_t i = 0; i < count; ++i) {
                if (fs.inst[i].place == e.place) {
                    state[i] = 1;
                }
            }
            break;
        case MirOp::Declare:
        case MirOp::Write:
        case MirOp::Move:
            // def представления (перепривязка/сброс). Мутация источника НЕ является def:
            // последующее использование уже инвалидированного представления должно остаться
            // видимым для liveness (иначе "используется после мутации" теряется).
            clearPlace(e.place);
            break;
        case MirOp::ScopeExit:
            for (std::size_t i = 0; i < count; ++i) {
                if (fs.inst[i].scope == e.scope) {
                    state[i] = 0;
                }
            }
            break;
        case MirOp::Call:
        case MirOp::Assert:
        case MirOp::Invariant:
        case MirOp::Terminator:
            // Контракт/вызов не переопределяют представления: чтения условия контракта
            // учтены отдельными Read-элементами.
            break;
        }
        return state;
    }
};

/// Диагностика, отложенная до сходимости (ключ дедупликации).
struct DiagKey {
    const AstNodeBase* site = nullptr;
    DiagId id = DiagId::BorrowOwnerMutated;
    const AstNodeBase* view = nullptr;

    friend bool operator<(const DiagKey& a, const DiagKey& b) {
        const std::less<const AstNodeBase*> less;
        if (a.site != b.site) {
            return less(a.site, b.site);
        }
        if (a.id != b.id) {
            return static_cast<int>(a.id) < static_cast<int>(b.id);
        }
        return less(a.view, b.view);
    }
};

} // namespace

struct Forward;

class Analyzer {
  public:
    Analyzer(Context& ctx, const flow::MirModule& mir)
    : m_ctx(ctx)
    , m_attrs(ctx.attrs())
    , m_mir(mir) {}

    void run();

  private:
    Context& m_ctx;
    const AttrPool& m_attrs;
    const flow::MirModule& m_mir;

    std::set<std::string> m_markedClasses;
    std::set<std::string> m_markedMethods;
    std::vector<const FuncDecl*> m_functions;
    std::map<const FuncDecl*, FnScan> m_scans;
    mutable std::set<DiagKey> m_reported;

    void collectMarks(const AstNodeBase& node);
    void scanFunction(const FuncDecl& fn);
    void scanNode(const AstNodePtr& node, FnScan& fs, const AstNodeBase* scope);

    [[nodiscard]] const AstNodeBase* sourceRootOfInit(const AstNodeBase* init);
    [[nodiscard]] bool varHasBorrowedAttr(const VarDecl& var) const;
    [[nodiscard]] bool varIsMarkedClassType(const VarDecl& var) const;
    [[nodiscard]] bool exprIsBorrowed(const AstNodeBase* init) const;
    [[nodiscard]] bool isViewPlace(const FnScan& fs, const AstNodeBase* decl) const;
    [[nodiscard]] static std::string normalizeMethodName(std::string_view name);
    [[nodiscard]] Region regionOfVar(const VarDecl& var) const;

    [[nodiscard]] static const AstNodeBase* resolveSource(const FnScan& fs, const AstNodeBase* place);
    void addInstance(FnScan& fs, const AstNodeBase* place, const AstNodeBase* source, const AstNodeBase* scope, bool weak);
    void analyzeFunction(const FuncDecl& fn, const ControlFlowGraph& cfg);
    void checkLifetime(const VarDecl& var, std::optional<RefType> kind);

    [[nodiscard]] bool markReported(const AstNodeBase* site, DiagId id, const AstNodeBase* view) const {
        return m_reported.insert(DiagKey{site, id, view}).second;
    }

    void reportMoved(const AstNodeBase* site, const AstNodeBase* view, const std::string& source) const {
        if (markReported(site, DiagId::BorrowOwnerMoved, view)) {
            m_ctx.report(site->range(), DiagId::BorrowOwnerMoved, "cannot move/swap source '{}' while a derived view is alive", source);
        }
    }
    void reportMutated(const AstNodeBase* site, const AstNodeBase* view, const std::string& source) const {
        if (markReported(site, DiagId::BorrowOwnerMutated, view)) {
            m_ctx.report(site->range(), DiagId::BorrowOwnerMutated, "cannot mutate source '{}' while a derived view is alive", source);
        }
    }
    void reportBorrowed(const AstNodeBase* site, const AstNodeBase* view, const std::string& source, const std::string& dep) const {
        if (markReported(site, DiagId::Borrowed, view)) {
            m_ctx.report(site->range(), DiagId::Borrowed,
                         "cannot change the owner '{}' while dependent variable(s) '{}' are alive; release the borrow first (e.g. '{} = _;')", source, dep,
                         dep);
        }
    }
    void reportRegion(const AstNodeBase* site, const AstNodeBase* view, const std::string& viewName, Region viewRegion, const std::string& source,
                      Region sourceRegion) const {
        if (markReported(site, DiagId::BorrowRegionMismatch, view)) {
            m_ctx.report(site->range(), DiagId::BorrowRegionMismatch, "derived view '{}' (region '{}') outlives its source '{}' (region '{}')", viewName,
                         regionName(viewRegion), source, regionName(sourceRegion));
        }
    }
    void reportLifetime(const AstNodeBase* site, const AstNodeBase* view, const std::string& viewName, Region own, const std::string& area) const {
        if (markReported(site, DiagId::BorrowRegionMismatch, view)) {
            m_ctx.report(site->range(), DiagId::BorrowRegionMismatch, "derived view '{}' (region '{}') exceeds its declared lifetime area '{}'", viewName,
                         regionName(own), area);
        }
    }
    void reportEscapeStored(const AstNodeBase* site, const AstNodeBase* view, const std::string& source) const {
        if (markReported(site, DiagId::BorrowEscape, view)) {
            m_ctx.report(site->range(), DiagId::BorrowEscape, "derived view escapes its scope (stored into a non-view place); source '{}'", source);
        }
    }
    void reportEscapeReturned(const AstNodeBase* site, const AstNodeBase* view, const std::string& source) const {
        if (markReported(site, DiagId::BorrowEscape, view)) {
            m_ctx.report(site->range(), DiagId::BorrowEscape, "derived view '{}' escapes its scope (returned); source '{}'", bareLocalName(requiredText(*view)),
                         source);
        }
    }

    friend struct Forward;
};

std::string Analyzer::normalizeMethodName(std::string_view name) {
    std::string r(name);
    if (!r.empty() && r[0] == '.') {
        r.erase(0, 1);
    }
    if (!r.empty() && r.back() == '^') {
        r.pop_back();
    }
    return r;
}

bool Analyzer::varHasBorrowedAttr(const VarDecl& var) const {
    if (var.has_attr(m_attrs, attr::Borrowed)) {
        return true;
    }
    const AstNodeBase* typeNode = var.m_type.get();
    return typeNode != nullptr && typeNode->as_attr() != nullptr && typeNode->as_attr()->has_attr(m_attrs, attr::Borrowed);
}

bool Analyzer::varIsMarkedClassType(const VarDecl& var) const {
    if (!var.m_type) {
        return false;
    }
    // Тип объявления может быть маркером ссылки (RefMake/RefTake) без текста: такой тип не
    // является именем помеченного класса (виды ссылок проверяются отдельно).
    const HasText* typeText = var.m_type->as_text();
    if (typeText == nullptr) {
        return false;
    }
    const std::string t(typeText->text());
    for (const auto& c : m_markedClasses) {
        if (t == c) {
            return true;
        }
        if (t.size() > c.size() && t.compare(0, c.size(), c) == 0 && t[c.size()] == '<') {
            return true;
        }
    }
    return false;
}

bool Analyzer::exprIsBorrowed(const AstNodeBase* init) const {
    if (init == nullptr) {
        return false;
    }
    if (init->kind() == ParserToken::Kind::MemberAccess) {
        const auto& b = static_cast<const Binary&>(*init);
        if (b.m_right && b.m_right->kind() == ParserToken::Kind::CallExpr) {
            const auto& call = static_cast<const CallExpr&>(*b.m_right);
            if (call.m_callee && call.m_callee->kind() == ParserToken::Kind::Ident) {
                return m_markedMethods.count(normalizeMethodName(requiredText(*call.m_callee))) != 0;
            }
        }
        return false;
    }
    if (init->kind() == ParserToken::Kind::CallExpr) {
        const auto& call = static_cast<const CallExpr&>(*init);
        return call.m_callee && call.m_callee->kind() == ParserToken::Kind::Ident && m_markedClasses.count(std::string(requiredText(*call.m_callee))) != 0;
    }
    return false;
}

bool Analyzer::isViewPlace(const FnScan& fs, const AstNodeBase* decl) const {
    return decl != nullptr && fs.byPlace.count(decl) != 0;
}

const AstNodeBase* Analyzer::sourceRootOfInit(const AstNodeBase* init) {
    if (init == nullptr) {
        return nullptr;
    }
    if (init->kind() == ParserToken::Kind::CallExpr) {
        const auto& call = static_cast<const CallExpr&>(*init);
        if (call.m_callee && call.m_callee->kind() == ParserToken::Kind::Ident && m_markedClasses.count(std::string(requiredText(*call.m_callee))) != 0) {
            if (call.m_args && !call.m_args->empty()) {
                return flow::placeRoot((*call.m_args)[0].get());
            }
        }
    }
    return flow::placeRoot(init);
}

Region Analyzer::regionOfVar(const VarDecl& var) const {
    if (var.has_attr(m_attrs, attr::ThreadLocal)) {
        return Region::Thread;
    }
    const std::string name(var.text());
    if (name.find("::") != std::string::npos) {
        return Region::Program;
    }
    return Region::Scope;
}

const AstNodeBase* Analyzer::resolveSource(const FnScan& fs, const AstNodeBase* place) {
    // Транзитивность (view-of-view): сводим к КОРНЕВОМУ источнику.
    std::set<const AstNodeBase*> seen;
    while (place != nullptr && seen.insert(place).second) {
        const auto it = fs.byPlace.find(place);
        if (it == fs.byPlace.end() || it->second.empty()) {
            break;
        }
        const Instance& v = fs.inst[static_cast<std::size_t>(it->second.front())];
        if (v.source == place) {
            break;
        }
        place = v.source;
    }
    return place;
}

void Analyzer::addInstance(FnScan& fs, const AstNodeBase* place, const AstNodeBase* source, const AstNodeBase* scope, bool weak) {
    if (place == nullptr || source == nullptr || place == source) {
        return;
    }
    const int idx = static_cast<int>(fs.inst.size());
    fs.inst.push_back(Instance{.place = place, .source = source, .scope = scope, .weak = weak});
    fs.byPlace[place].push_back(idx);
}

void Analyzer::collectMarks(const AstNodeBase& node) {
    if (node.kind() == ParserToken::Kind::ClassDecl) {
        const auto& cls = static_cast<const ClassDecl&>(node);
        if (cls.has_attr(m_attrs, attr::Borrowed)) {
            m_markedClasses.insert(std::string(cls.text()));
        }
    }
    if (node.kind() == ParserToken::Kind::FuncDecl) {
        const auto& fn = static_cast<const FuncDecl&>(node);
        if (fn.has_attr(m_attrs, attr::Borrowed)) {
            m_markedMethods.insert(normalizeMethodName(fn.text()));
        }
    }
    std::vector<AstNodePtr*> children;
    const_cast<AstNodeBase&>(node).collectChildren(children);
    for (AstNodePtr* child : children) {
        if (child && *child) {
            collectMarks(**child);
        }
    }
}

void Analyzer::scanFunction(const FuncDecl& fn) {
    FnScan fs;
    if (fn.m_body) {
        for (const AstNodePtr& stmt : *fn.m_body) {
            scanNode(stmt, fs, nullptr);
        }
    }
    m_scans[&fn] = std::move(fs);
}

void Analyzer::scanNode(const AstNodePtr& node, FnScan& fs, const AstNodeBase* scope) {
    if (!node) {
        return;
    }
    if (node->is<FuncDecl>() || node->is<RecordDecl>() || node->is<ClassDecl>()) {
        return; // тела вложенных функций/лямбд — отдельный анализ
    }
    if (node->kind() == ParserToken::Kind::ScopeBlock) {
        scope = node.get();
    }
    if (node->is<VarDecl>()) {
        const auto& var = *node->as<VarDecl>();
        std::optional<RefType> kind = refKindOfAttr(m_attrs, var);
        if (!kind.has_value() && var.m_type) {
            kind = refKindOfTypeSpec(var.m_type.get(), m_attrs);
        }
        checkLifetime(var, kind);
        if (kind.has_value()) {
            fs.declKind[&var] = *kind;
        }
        fs.declRegion[&var] = regionOfVar(var);
        if (kind.has_value()) {
            const bool owner = *kind == RefType::kUnique || *kind == RefType::kShared;
            TRUST_DEBUG("borrow", "decl '{}' kind={} owner={} region={}", bareLocalName(var.text()), refTypeName(*kind), owner ? "yes" : "no",
                        regionName(regionOfVar(var)));
        }
        const AstNodeBase* init = var.m_initializer.get();
        const AstNodeBase* source = nullptr;
        bool weak = false;
        if (init != nullptr && init->is<RefMakeExpr>()) {
            const auto& seq = *init->as<Sequence>();
            if (!seq.m_body.empty()) {
                source = flow::placeRoot(seq.m_body[0].get());
            }
            weak = true;
        } else if (varHasBorrowedAttr(var) || varIsMarkedClassType(var) || exprIsBorrowed(init) || isViewPlace(fs, flow::placeRoot(init))) {
            source = sourceRootOfInit(init);
        }
        if (source != nullptr) {
            if (!weak) {
                source = resolveSource(fs, source);
            }
            addInstance(fs, &var, source, scope, weak);
        }
    }
    if (node->kind() == ParserToken::Kind::AssignOp) {
        const auto& b = *node->as<Binary>();
        const AstNodeBase* lhs = flow::placeRoot(b.m_left.get());
        const AstNodeBase* rhs = b.m_right.get();
        const bool reset = rhs == nullptr || isNoneMarker(rhs);
        const AstNodeBase* rhsRoot = reset ? nullptr : sourceRootOfInit(rhs);
        const bool rhsWeak = !reset && rhs->is<RefMakeExpr>();
        const bool rhsBorrowed = !reset && (isViewPlace(fs, flow::placeRoot(rhs)) || exprIsBorrowed(rhs));
        if (lhs != nullptr) {
            // lhs — проекция (`s.field`, `a[i]`): представление уходит в агрегат, а не
            // перепривязывает переменную.
            const bool projectedLhs = b.m_left && b.m_left->kind() != ParserToken::Kind::Ident;
            const AstNodeBase* rhsView = (rhs != nullptr && rhs->kind() == ParserToken::Kind::Ident) ? flow::placeRoot(rhs) : nullptr;
            if (rhsBorrowed && projectedLhs && rhsView != nullptr && fs.byPlace.count(rhsView) != 0) {
                fs.escapeOnWrite[node.get()].push_back(rhsView);
            } else if ((rhsBorrowed || rhsWeak) && rhsRoot != nullptr) {
                const AstNodeBase* resolved = rhsWeak ? rhsRoot : resolveSource(fs, rhsRoot);
                addInstance(fs, lhs, resolved, scope, rhsWeak);
                fs.rebindAt[node.get()] = static_cast<int>(fs.inst.size()) - 1;
            }
        }
    }
    if (node->kind() == ParserToken::Kind::MemberAccess) {
        const auto& b = *node->as<Binary>();
        if (b.m_right && b.m_right->kind() == ParserToken::Kind::CallExpr) {
            const auto& call = *b.m_right->as<CallExpr>();
            const bool isConst = call.has_attr(m_attrs, attr::ReadOnly) || (call.m_callee && requiredText(*call.m_callee).find('^') != std::string_view::npos);
            if (!isConst) {
                if (const AstNodeBase* receiver = flow::placeRoot(b.m_left.get())) {
                    fs.callReceiver[b.m_right.get()] = receiver;
                }
            }
        }
    }
    if (node->is<JumpStmt>()) {
        const auto& jump = *node->as<JumpStmt>();
        if (jump.kind() == ParserToken::Kind::ReturnStmt && jump.m_value) {
            if (const AstNodeBase* view = flow::placeRoot(jump.m_value.get()); view && fs.byPlace.count(view) != 0) {
                fs.returnView[&jump] = view;
            }
        }
    }
    std::vector<AstNodePtr*> children;
    node->collectChildren(children);
    for (AstNodePtr* child : children) {
        if (child && *child) {
            scanNode(*child, fs, scope);
        }
    }
}

struct Forward {
    using State = ViewBits::State;
    static constexpr Direction direction = Direction::Forward;

    Analyzer& analyzer;
    const FnScan& fs;
    const std::vector<std::vector<State>>& liveAfter;

    [[nodiscard]] State boundary() const { return State(fs.inst.size(), 0); }
    [[nodiscard]] State bottom() const { return boundary(); }
    [[nodiscard]] State unreachableState() const { return boundary(); }

    [[nodiscard]] bool join(State& dst, const State& src) const {
        bool changed = false;
        for (std::size_t i = 0; i < dst.size(); ++i) {
            if (src[i] != 0 && dst[i] == 0) {
                dst[i] = 1;
                changed = true;
            }
        }
        return changed;
    }

    [[nodiscard]] static std::string nameOf(const AstNodeBase* place) { return place ? bareLocalName(requiredText(*place)) : std::string("?"); }

    void mutateSource(const MirInstruction& e, State& state, const State& after) const {
        if (e.place == nullptr) {
            return;
        }
        std::vector<std::string> weakViews;
        std::vector<std::string> dependentViews;
        for (std::size_t i = 0; i < fs.inst.size(); ++i) {
            if (state[i] == 0 || fs.inst[i].source != e.place) {
                continue;
            }
            const Instance& v = fs.inst[i];
            // Точность «последнее использование» применяется к weak-представлениям; для
            // @[borrowed] сохраняется контрактная семантика (диагностика в точке мутации).
            const bool neededLater = !v.weak || (i < after.size() && after[i] != 0);
            if (neededLater) {
                (v.weak ? weakViews : dependentViews).push_back(nameOf(v.place));
            }
            state[i] = 0; // представление инвалидировано мутацией
        }
        const std::string sourceName = nameOf(e.place);
        TRUST_DEBUG("borrow", "mutate '{}' op={} live-view={}", sourceName, e.kind == MirOp::Move ? "swap" : "assign", weakViews.empty() ? "no" : "yes");
        if (!weakViews.empty()) {
            if (e.kind == MirOp::Move) {
                const auto kind = fs.declKind.find(e.place);
                TRUST_DEBUG("borrow", "R4 source-moved '{}' kind={} while view alive", sourceName,
                            kind == fs.declKind.end() ? "value" : refTypeName(kind->second));
                if (analyzer.markReported(e.site, DiagId::BorrowOwnerMoved, e.place)) {
                    analyzer.m_ctx.report(e.site->range(), DiagId::BorrowOwnerMoved, "cannot move/swap source '{}' while a derived view is alive", sourceName);
                }
            } else {
                const auto kind = fs.declKind.find(e.place);
                const bool shared = kind != fs.declKind.end() && kind->second == RefType::kShared;
                if (!shared && analyzer.markReported(e.site, DiagId::BorrowOwnerMutated, e.place)) {
                    analyzer.m_ctx.report(e.site->range(), DiagId::BorrowOwnerMutated, "cannot mutate source '{}' while a derived view is alive", sourceName);
                }
            }
        }
        if (!dependentViews.empty() && analyzer.markReported(e.site, DiagId::Borrowed, e.place)) {
            std::string joined;
            for (std::size_t i = 0; i < dependentViews.size(); ++i) {
                if (i != 0) {
                    joined += ", ";
                }
                joined += "'" + dependentViews[i] + "'";
            }
            analyzer.m_ctx.report(e.site->range(), DiagId::Borrowed,
                                  "cannot change the owner '{}' while dependent variable(s) {} are alive; release the borrow first (e.g. '{} = _;')",
                                  sourceName, joined, dependentViews.front());
        }
    }

    State step(const MirInstruction& e, State state, const State& after) const {
        if (e.kind == MirOp::Declare) {
            const auto it = fs.byPlace.find(e.place);
            if (it != fs.byPlace.end()) {
                for (int idx : it->second) {
                    state[static_cast<std::size_t>(idx)] = 1;
                    const Instance& v = fs.inst[static_cast<std::size_t>(idx)];
                    TRUST_DEBUG("borrow", "view '{}' -> source '{}' born-epoch=0 region={}", nameOf(v.place), nameOf(v.source), v.weak ? "scope" : "borrowed");
                    if (v.weak) {
                        const auto viewRegion = fs.declRegion.find(v.place);
                        if (viewRegion != fs.declRegion.end()) {
                            const auto sourceRegion = fs.declRegion.find(v.source);
                            // Источник вне тела функции — параметр (scope) или глобал (program).
                            Region sr = Region::Scope;
                            if (sourceRegion != fs.declRegion.end()) {
                                sr = sourceRegion->second;
                            } else if (v.source->is<VarDecl>()) {
                                const auto& sourceVar = *v.source->as<VarDecl>();
                                sr = sourceVar.has_attr(analyzer.m_attrs, attr::ThreadLocal) ? Region::Thread : Region::Program;
                            }
                            if (viewRegion->second > sr) {
                                analyzer.reportRegion(v.place, v.place, nameOf(v.place), viewRegion->second, nameOf(v.source), sr);
                            }
                        }
                    }
                }
            }
            return state;
        }
        if (e.kind == MirOp::Write || e.kind == MirOp::Move) {
            const auto bp = fs.byPlace.find(e.place);
            if (bp != fs.byPlace.end()) {
                for (int idx : bp->second) {
                    state[static_cast<std::size_t>(idx)] = 0;
                }
            }
            const auto rebind = fs.rebindAt.find(e.site);
            if (rebind != fs.rebindAt.end()) {
                state[static_cast<std::size_t>(rebind->second)] = 1;
            }
            const auto esc = fs.escapeOnWrite.find(e.site);
            if (esc != fs.escapeOnWrite.end()) {
                for (const AstNodeBase* view : esc->second) {
                    const auto found = fs.byPlace.find(view);
                    if (found == fs.byPlace.end() || found->second.empty()) {
                        continue;
                    }
                    const Instance& v = fs.inst[static_cast<std::size_t>(found->second.front())];
                    // Escape опасен для НЕвладеющих @[borrowed]-копий; weak безопасен (refcount).
                    if (!v.weak) {
                        analyzer.reportEscapeStored(e.site, view, nameOf(v.source));
                    }
                }
            }
            mutateSource(e, state, after);
            return state;
        }
        if (e.kind == MirOp::ScopeExit) {
            for (std::size_t i = 0; i < fs.inst.size(); ++i) {
                if (fs.inst[i].scope == e.scope) {
                    state[i] = 0;
                }
            }
            return state;
        }
        if (e.kind == MirOp::Call && e.site != nullptr) {
            const auto receiver = fs.callReceiver.find(e.site);
            if (receiver != fs.callReceiver.end()) {
                MirInstruction write;
                write.kind = MirOp::Write;
                write.place = receiver->second;
                write.site = e.site;
                mutateSource(write, state, after);
            }
            return state;
        }
        return state;
    }

    State transferBlock(const FlowBlock& block, State state) const {
        for (std::size_t i = 0; i < block.elements.size(); ++i) {
            state = step(block.elements[i], state, liveAfter[block.id][i]);
        }
        const MirInstruction& term = block.terminatorNode;
        if (term.site != nullptr && term.site->is<JumpStmt>()) {
            const auto it = fs.returnView.find(term.site);
            if (it != fs.returnView.end()) {
                const auto views = fs.byPlace.find(it->second);
                if (views != fs.byPlace.end()) {
                    for (int idx : views->second) {
                        if (!fs.inst[static_cast<std::size_t>(idx)].weak) {
                            analyzer.reportEscapeReturned(term.site, it->second, nameOf(fs.inst[static_cast<std::size_t>(idx)].source));
                        }
                    }
                }
            }
        }
        return state;
    }
};

void Analyzer::checkLifetime(const VarDecl& var, std::optional<RefType> kind) {
    // @[lifetime] применим только к нативным (невладеющим) видам.
    if (!kind.has_value() || !isNativeRefKind(*kind)) {
        return;
    }
    const auto lid = m_attrs.lookup(attr::Lifetime);
    if (!lid.has_value()) {
        return;
    }
    const std::vector<std::string>* args = var.attr_args(*lid);
    if (args == nullptr && var.m_type != nullptr && var.m_type->as_attr() != nullptr) {
        args = var.m_type->as_attr()->attr_args(*lid);
    }
    if (args == nullptr || args->empty()) {
        return;
    }
    const std::string& area = args->at(0);
    Region declared = Region::Scope;
    bool known = true;
    if (area == "scope") {
        declared = Region::Scope;
    } else if (area == "thread") {
        declared = Region::Thread;
    } else if (area == "program") {
        declared = Region::Program;
    } else {
        known = false; // self/param/var/named/_ — область задаётся источником
    }
    if (!known) {
        return;
    }
    const Region own = regionOfVar(var);
    if (own > declared) {
        reportLifetime(&var, &var, bareLocalName(var.text()), own, area);
    }
}

void Analyzer::analyzeFunction(const FuncDecl& fn, const ControlFlowGraph& cfg) {
    const FnScan& fs = m_scans.at(&fn);
    if (fs.inst.empty()) {
        return;
    }
    // CFG построен ОДИН раз в MirModule; здесь только dataflow.

    const Liveness liveness{fs, fs.inst.size()};
    const auto liveRes = flow::runDataflow(cfg, liveness);

    std::vector<std::vector<ViewBits::State>> liveAfter(cfg.blocks.size());
    for (const FlowBlock& block : cfg.blocks) {
        liveAfter[block.id].assign(block.elements.size(), ViewBits::State(fs.inst.size(), 0));
        ViewBits::State cur = liveRes.out[block.id]; // backward: out = состояние на выходе блока
        for (std::size_t i = block.elements.size(); i-- > 0;) {
            liveAfter[block.id][i] = cur;
            cur = liveness.transferElement(block.elements[i], cur);
        }
    }

    const Forward forward{*this, fs, liveAfter};
    (void)flow::runDataflow(cfg, forward);
}

void Analyzer::run() {
    for (const AstNodePtr& root : m_mir.roots) {
        if (root) {
            collectMarks(*root);
        }
    }
    m_functions.reserve(m_mir.functions.size());
    for (const flow::MirFunction& function : m_mir.functions) {
        EXPECT(function.func != nullptr && "MirModule function must have a declaration");
        m_functions.push_back(function.func);
    }
    for (const FuncDecl* fn : m_functions) {
        scanFunction(*fn);
    }
    for (std::size_t i = 0; i < m_functions.size(); ++i) {
        analyzeFunction(*m_functions[i], m_mir.functions[i].cfg);
    }
}

void BorrowFlowPass::run(const flow::MirModule& mir) {
    Analyzer(m_ctx, mir).run();
}

} // namespace trust::semantic
