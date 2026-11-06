// Definite assignment: CFG + forward must-dataflow по стабильным местам (узлы объявлений).

#include "semantic/ref/definite_assignment.hpp"

#include "ir/cfg.hpp"
#include "ir/dataflow.hpp"
#include "ir/places.hpp"
#include "ast/ast_nodes.hpp"
#include "ast/binary_op.hpp"
#include "attrs/attr_builtin.hpp"
#include "attrs/attr_pool.hpp"
#include "diag/diag.hpp"
#include "session/context.hpp"

#include <algorithm>
#include <cstddef>
#include <cstdint>
#include <format>
#include <string>
#include <string_view>
#include <unordered_set>
#include <vector>

namespace trust::semantic {
namespace {

using flow::ControlFlowGraph;
using flow::DataflowResult;
using flow::Direction;
using flow::FlowBlock;
using flow::PlaceId;
using flow::PlaceIdHash;
using flow::PlaceTable;

/// Набор отслеживаемых мест функции/лямбды.
struct Places {
    PlaceTable<const AstNodeBase*> table;                    ///< PlaceId -> узел объявления
    std::unordered_set<PlaceId, PlaceIdHash> preInitialized; ///< параметры/catch-bindings/захваты
    std::vector<const VarDecl*> untyped;                     ///< кандидаты `x := _` (проверка cannot infer)
};

void addPlace(Places& places, const AstNodeBase* node, bool preInitialized = false) {
    if (!node) {
        return;
    }
    const PlaceId id = places.table.add(node);
    if (preInitialized) {
        places.preInitialized.insert(id);
    }
}

bool isUntypedUnderscore(const VarDecl& var) {
    return var.initKind() == VarInitKind::DeferredInit;
}

void collectPlacesInNode(AstNodeBase& node, Places& places) {
    // Тела вложенных функций/лямбд - отдельные CFG.
    if (node.is<FuncDecl>() || node.is<RecordDecl>() || node.is<ClassDecl>()) {
        return;
    }
    if (node.is<VarDecl>()) {
        const auto& var = *node.as<VarDecl>();
        addPlace(places, &node);
        if (isUntypedUnderscore(var)) {
            places.untyped.push_back(&var);
        }
        return; // тип/инициализатор не содержат объявлений мест
    }
    if (node.is<DestructureDecl>()) {
        const auto& destructure = *node.as<DestructureDecl>();
        if (!destructure.m_isAssign) {
            for (const DestructureDecl::Target& target : destructure.m_targets) {
                if (target.name && !isNoneMarker(target.name.get())) {
                    addPlace(places, target.name.get());
                }
            }
        }
    }
    if (node.is<CatchBlock>()) {
        const auto& catchBlock = *node.as<CatchBlock>();
        if (catchBlock.m_binding && catchBlock.m_binding->is<VarDecl>()) {
            addPlace(places, catchBlock.m_binding.get(), /*preInitialized=*/true);
        }
    }
    if (node.is<WithStmt>()) {
        for (const auto& [lock, binding] : node.as<WithStmt>()->m_locks) {
            (void)lock;
            if (binding && binding->is<VarDecl>()) {
                addPlace(places, binding.get());
            }
        }
    }
    if (node.is<MatchStmt>()) {
        addPlace(places, node.as<MatchStmt>()->m_tempDecl.get());
    }
    if (node.is<JumpStmt>()) {
        addPlace(places, node.as<JumpStmt>()->m_tempDecl.get());
    }

    std::vector<AstNodePtr*> children;
    node.collectChildren(children);
    for (AstNodePtr* child : children) {
        if (child && *child) {
            collectPlacesInNode(**child, places);
        }
    }
}

void collectExternalRefs(AstNodeBase& node, Places& places) {
    if (node.is<FuncDecl>()) {
        return;
    }
    if (node.kind() == ParserToken::Kind::Ident) {
        if (const auto* ident = node.as<IdentName>(); ident && ident->resolvedDecl && !places.table.find(ident->resolvedDecl).has_value()) {
            if (ident->resolvedDecl->is<VarDecl>() || ident->resolvedDecl->is<ArgNode>()) {
                addPlace(places, ident->resolvedDecl, /*preInitialized=*/true);
            }
        }
        return;
    }
    std::vector<AstNodePtr*> children;
    node.collectChildren(children);
    for (AstNodePtr* child : children) {
        if (child && *child) {
            collectExternalRefs(**child, places);
        }
    }
}

Places collectPlaces(const FuncDecl& func) {
    Places places;
    if (func.m_params) {
        for (const AstNodePtr& param : *func.m_params) {
            if (param && param->is<ArgNode>()) {
                addPlace(places, param.get(), /*preInitialized=*/true);
            }
        }
    }
    if (func.m_captures) {
        for (const AstNodePtr& capture : *func.m_captures) {
            if (capture && capture->is<ArgNode>()) {
                const auto& arg = *capture->as<ArgNode>();
                addPlace(places, arg.resolvedDecl, /*preInitialized=*/true);
            }
        }
    }
    if (func.hasBody()) {
        for (const AstNodePtr& stmt : *func.body()) {
            if (stmt) {
                collectPlacesInNode(*stmt, places);
            }
        }
        if (func.isLambda()) {
            for (const AstNodePtr& stmt : *func.body()) {
                if (stmt) {
                    collectExternalRefs(*stmt, places);
                }
            }
        }
    }
    return places;
}

/// Факты места на пути: инициализирована / стала const.
struct PlaceFact {
    std::uint8_t assigned = 0;
    std::uint8_t becameConst = 0;
};

struct DaState {
    std::vector<PlaceFact> facts;
};

struct ReadError {
    const AstNodeBase* site = nullptr;
    std::string message;
};

struct Reporter {
    std::vector<ReadError> errors;
    std::unordered_set<const AstNodeBase*> declsWithErrors;
};

class DaAnalysis {
  public:
    using State = DaState;
    static constexpr Direction direction = Direction::Forward;

    explicit DaAnalysis(const Places& places, const AttrPool& attrs)
    : m_places(places)
    , m_attrs(attrs) {}

    State boundary() const {
        State state;
        state.facts.assign(m_places.table.size(), PlaceFact{});
        for (const PlaceId id : m_places.preInitialized) {
            state.facts[id.value].assigned = 1;
        }
        return state;
    }

    State bottom() const {
        State state;
        state.facts.assign(m_places.table.size(), PlaceFact{1, 1}); // identity AND-join (top must-решётки)
        return state;
    }

    State unreachableState() const { return bottom(); }

    bool join(State& dst, const State& src) const {
        bool changed = false;
        for (std::size_t i = 0; i < dst.facts.size(); ++i) {
            if (dst.facts[i].assigned != 0 && src.facts[i].assigned == 0) {
                dst.facts[i].assigned = 0;
                changed = true;
            }
            if (dst.facts[i].becameConst != 0 && src.facts[i].becameConst == 0) {
                dst.facts[i].becameConst = 0;
                changed = true;
            }
        }
        return changed;
    }

    State transferElement(const MirInstruction& effect, State state) const {
        applyInstruction(effect, state, nullptr);
        return state;
    }

    /// Применяет эффект; reporter != nullptr - собирает диагностики (второй проход после fixpoint).
    void applyInstruction(const MirInstruction& effect, State& state, Reporter* reporter) const;

  private:
    const Places& m_places;
    const AttrPool& m_attrs;

    bool hasPlace(const AstNodeBase* decl) const { return decl && m_places.table.find(decl).has_value(); }

    bool hasReadOnly(const AstNodeBase* node) const {
        const AstNodeAttr* attrNode = node ? node->as_attr() : nullptr;
        return attrNode && attrNode->has_attr(m_attrs, attr::ReadOnly);
    }

    bool isDeclaredConst(const AstNodeBase* decl) const { return hasReadOnly(decl); }

    /// Статическую const-переменную (VarDecl/ArgNode) проверяет основной проход через
    /// QualType; DA не дублирует её диагностику. Не-const VarDecl/ArgNode — зона DA
    /// (became-const), цели деструктуризации — тоже DA.
    bool mainPassHandlesStaticConst(const AstNodeBase* decl) const { return decl && (decl->is<VarDecl>() || decl->is<ArgNode>()) && isDeclaredConst(decl); }

    void setAssigned(State& state, const AstNodeBase* decl, bool value) const {
        const auto id = m_places.table.find(decl);
        if (id) {
            state.facts[id->value].assigned = value ? 1 : 0;
        }
    }

    bool isAssigned(const State& state, const AstNodeBase* decl) const {
        const auto id = m_places.table.find(decl);
        return id && state.facts[id->value].assigned != 0;
    }

    void setConst(State& state, const AstNodeBase* decl) const {
        const auto id = m_places.table.find(decl);
        if (id) {
            state.facts[id->value].becameConst = 1;
        }
    }

    bool isConst(const State& state, const AstNodeBase* decl) const {
        const auto id = m_places.table.find(decl);
        return id && state.facts[id->value].becameConst != 0;
    }

    void applyDeclare(const MirInstruction& effect, State& state) const;
    void applyRead(const MirInstruction& effect, State& state, Reporter* reporter) const;
    void applyWrite(const MirInstruction& effect, State& state, Reporter* reporter) const;
    void reportRead(const AstNodeBase& site, const AstNodeBase* decl, State& state, Reporter* reporter) const;
};

void DaAnalysis::applyInstruction(const MirInstruction& effect, State& state, Reporter* reporter) const {
    switch (effect.kind) {
    case MirOp::Declare:
        applyDeclare(effect, state);
        break;
    case MirOp::Read:
        applyRead(effect, state, reporter);
        break;
    case MirOp::Write:
        applyWrite(effect, state, reporter);
        break;
    case MirOp::Move:
        // Перемещение/сброс (`swap`, `= _`) делает место неинициализированным для DA.
        applyWrite(effect, state, reporter);
        break;
    case MirOp::Call:
        break;
    case MirOp::ScopeExit:
        break;
    case MirOp::Assert:
    case MirOp::Invariant:
        // Контракт не меняет инициализированность мест; чтения его выражения уже
        // линеаризованы отдельными Read-элементами и обрабатываются выше.
        break;
    case MirOp::Terminator:
        break;
    }
}

void DaAnalysis::applyDeclare(const MirInstruction& effect, State& state) const {
    const AstNodeBase* place = effect.place;
    if (!hasPlace(place)) {
        return;
    }
    bool assigned = effect.assigned;
    const auto id = m_places.table.find(place);
    if (id && m_places.preInitialized.find(*id) != m_places.preInitialized.end()) {
        assigned = true; // catch-binding: всегда инициализирован на входе catch
    }
    setAssigned(state, place, assigned);
    if (!place->is<VarDecl>() && isDeclaredConst(place)) {
        setConst(state, place); // цель деструктуризации, объявленная const
    }
}

void DaAnalysis::applyRead(const MirInstruction& effect, State& state, Reporter* reporter) const {
    if (!effect.site) {
        return;
    }
    reportRead(*effect.site, effect.place, state, reporter);
}

void DaAnalysis::applyWrite(const MirInstruction& effect, State& state, Reporter* reporter) const {
    const AstNodeBase* place = effect.place;
    if (!hasPlace(place)) {
        return;
    }
    if (effect.finalWrite) {
        // Статически объявленную const-переменную (VarDecl/ArgNode) проверяет основной проход
        // (QualType); DA отслеживает became-const и const-цели деструктуризации.
        if (!mainPassHandlesStaticConst(place) && !isDeclaredConst(place)) {
            setConst(state, place);
        }
        setAssigned(state, place, effect.assigned);
        return;
    }
    if (!mainPassHandlesStaticConst(place) && isConst(state, place) && reporter) {
        std::string name(requiredText(*place));
        if (!name.empty() && name.front() == '$') {
            name.erase(0, 1);
        }
        const AstNodeBase* site = effect.site ? effect.site : place;
        reporter->errors.push_back(ReadError{.site = site, .message = std::format("cannot assign to constant variable '{}'", name)});
    }
    setAssigned(state, place, effect.assigned);
}

void DaAnalysis::reportRead(const AstNodeBase& site, const AstNodeBase* decl, State& state, Reporter* reporter) const {
    // decl вне places: global/static/field/synthetic-ссылка — вне intra-procedural DA (см. header).
    if (!decl || !hasPlace(decl) || isAssigned(state, decl)) {
        return;
    }
    if (!reporter) {
        return;
    }
    std::string name(requiredText(site));
    if (!name.empty() && name.front() == '$') {
        name.erase(0, 1); // DSL-сигил: показываем имя без служебного '$'
    }
    reporter->errors.push_back(ReadError{.site = &site,
                                         .message = std::format("variable '{}' is read before it is initialized (declared/reset without a value); "
                                                                "assign a value before reading it",
                                                                name)});
    reporter->declsWithErrors.insert(decl);
}

void analyzeFunction(Context& ctx, const FuncDecl& func, const ControlFlowGraph& cfg) {
    if (!func.hasBody()) {
        return;
    }
    Places places = collectPlaces(func);
    if (places.table.size() == 0) {
        return;
    }

    const DaAnalysis analysis(places, ctx.attrs());
    const DataflowResult<DaAnalysis> result = flow::runDataflow(cfg, analysis);

    Reporter reporter;
    for (const FlowBlock& block : cfg.blocks) {
        if (block.unreachable) {
            continue; // недостижимый код не диагностируем (состояние = top)
        }
        DaState state = result.in[block.id];
        for (const MirInstruction& element : block.elements) {
            analysis.applyInstruction(element, state, &reporter);
        }
    }

    std::stable_sort(reporter.errors.begin(), reporter.errors.end(), [](const ReadError& a, const ReadError& b) { return a.site->range() < b.site->range(); });
    for (const ReadError& error : reporter.errors) {
        ctx.diag().report(Severity::Error, error.site->range(), "{}", error.message);
    }

    for (const VarDecl* var : places.untyped) {
        if (!var || var->initKind() != VarInitKind::DeferredInit) {
            continue;
        }
        if (var->inferredType.getTypeId() != INVALID_TYPE_ID) {
            continue; // тип выведен из записей
        }
        if (reporter.declsWithErrors.find(var) != reporter.declsWithErrors.end()) {
            continue; // первичная причина (read-before-init) уже сообщена
        }
        std::string name(requiredText(*var));
        if (!name.empty() && name.front() == '$') {
            name.erase(0, 1);
        }
        ctx.diag().report(Severity::Error, var->nameRange(),
                          "cannot infer the type of untyped '{} := _' from any assignment; "
                          "annotate it ('{} :Any := _') or remove the declaration",
                          name, name);
    }
}

} // namespace

void DefiniteAssignmentPass::run(const flow::MirModule& mir) {
    for (const flow::MirFunction& function : mir.functions) {
        EXPECT(function.func != nullptr && "MirModule function must have a declaration");
        analyzeFunction(m_ctx, *function.func, function.cfg);
    }
}

} // namespace trust::semantic
