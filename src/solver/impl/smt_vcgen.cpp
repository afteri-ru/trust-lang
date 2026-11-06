// Символьное исполнение тела функции по MIR (единая производная форма = CFG поверх HIR).
// Архитектура:
//   * состояние — значения МЕСТ (`flow::PlaceId`) + условие достижимости (path);
//   * блок исполняется один раз в RPO-порядке, входные состояния сливаются (ite по path);
//   * цикл — «регион» (блоки под доминированием заголовка); стратегии: индукция по инварианту
//     (элементы `MirOp::Invariant`, привязанные к циклу через `loopNode`) или bounded unrolling
//     (директива `z3_unroll(N)` / `-fsolver-loop-unroll`); иначе — verification envelope;
//   * выходы функции собираются как (path, ret) и превращаются в один VC
//     `pre ∧ ¬∧_i(path_i → post_i)`.

#include "solver/trust_to_smt.hpp"
#include "solver/vc/smt_term_builder.hpp"

#include "ir/cfg.hpp"
#include "ast/ast_nodes.hpp"
#include "ast/binary_op.hpp"
#include "ast/ast_nodes.hpp"
#include "ast/ident_name.hpp"
#include "ast/trust_prop.hpp"
#include "session/context.hpp"
#include "types/type_system.hpp"
#include "types/typekind.hpp"
#include "utils/error.hpp"

#include <algorithm>
#include <limits>
#include <map>
#include <memory>
#include <optional>
#include <set>
#include <string>
#include <unordered_set>
#include <utility>
#include <vector>

namespace trust {
namespace solver {

namespace sb = SmtTermBuilder;

namespace {

constexpr std::size_t kNoHeader = std::numeric_limits<std::size_t>::max();

[[nodiscard]] bool isFalseConst(const SmtTerm& term) {
    return term.kind == SmtTermKind::kConst && term.const_value == "false";
}

[[nodiscard]] bool isTrueConst(const SmtTerm& term) {
    return term.kind == SmtTermKind::kConst && term.const_value == "true";
}

[[nodiscard]] SmtTerm app2(std::string_view op, SmtTerm lhs, SmtTerm rhs, MapperRange range = {}) {
    return sb::makeApp(std::string(op), sb::boolSort(), {std::make_shared<SmtTerm>(std::move(lhs)), std::make_shared<SmtTerm>(std::move(rhs))}, range);
}

/// Дизъюнкция условий достижимости (с упрощением констант: false ∨ p = p).
[[nodiscard]] SmtTerm orPath(SmtTerm lhs, SmtTerm rhs) {
    if (isFalseConst(lhs)) {
        return rhs;
    }
    if (isFalseConst(rhs)) {
        return lhs;
    }
    return app2("or", std::move(lhs), std::move(rhs));
}

[[nodiscard]] SmtTerm andPath(SmtTerm lhs, SmtTerm rhs) {
    if (isTrueConst(lhs)) {
        return rhs;
    }
    if (isTrueConst(rhs)) {
        return lhs;
    }
    return app2("and", std::move(lhs), std::move(rhs));
}

[[nodiscard]] SmtTerm notPath(const SmtTerm& term) {
    return sb::mkNot(std::make_shared<SmtTerm>(term));
}

[[nodiscard]] SmtTerm iteValue(const SmtTerm& cond, SmtTerm thenValue, SmtTerm elseValue) {
    const SmtSort sort = thenValue.sort;
    return sb::makeApp("ite", sort,
                   {std::make_shared<SmtTerm>(cond), std::make_shared<SmtTerm>(std::move(thenValue)), std::make_shared<SmtTerm>(std::move(elseValue))});
}

/// Директива решателя `z3_unroll(N)` в инварианте цикла: N итераций разворачивания, 0 - не задана.
int invariantUnrollCount(const AstNodeBase* inv) {
    if (inv != nullptr && inv->kind() == ParserToken::Kind::TrustElem) {
        const auto& te = static_cast<const TrustElem&>(*inv);
        if (te.kind == Z3TermKind::Unroll && !te.m_args.empty() && te.m_args[0]) {
            try {
                return std::stoi(std::string(requiredText(*te.m_args[0])));
            } catch (...) {
                return 0; // не число — директива не задана (диагностика ниже по коду)
            }
        }
    }
    return 0;
}

/// Признак «сужение/присваивание» и базовая операция для составного присваивания.
[[nodiscard]] std::optional<std::string_view> compoundOpName(BinaryOp op) {
    switch (op) {
    case BinaryOp::AddAssign:
        return "+";
    case BinaryOp::SubAssign:
        return "-";
    case BinaryOp::MulAssign:
        return "*";
    case BinaryOp::DivAssign:
        return "/";
    case BinaryOp::IntDivAssign:
        return "//";
    case BinaryOp::ModAssign:
        return "%";
    case BinaryOp::AndAssign:
        return ".&.";
    case BinaryOp::OrAssign:
        return ".|.";
    case BinaryOp::XorAssign:
        return ".^.";
    case BinaryOp::ShlAssign:
        return ".<.";
    case BinaryOp::ShrAssign:
        return ".>>.";
    default:
        return std::nullopt;
    }
}

} // namespace

/// Символьный исполнитель тела одной функции. Живёт только на время кодирования функции.
struct TrustToSmt::BodyEncoder {
    TrustToSmt& self;
    const FuncDecl& func;
    const flow::MirFunction& mirFn;
    const flow::ControlFlowGraph& cfg;

    /// Состояние исполнения: значения мест + условие достижимости.
    struct State {
        VcValues values;
        SmtTerm path;
    };
    /// Выход из исполняемого региона: (куда, состояние) + терм возврата для `return`.
    struct ExitState {
        std::size_t target = 0;
        State state;
        std::optional<SmtTerm> ret;
    };
    /// Описание цикла: заголовок, регион (блоки под доминированием заголовка), инварианты.
    struct Loop {
        std::size_t header = kNoHeader;
        bool doWhile = false;
        const AstNodeBase* node = nullptr;
        std::vector<const AstNodeBase*> invariants;
        std::set<std::size_t> region;
        std::set<const AstNodeBase*> modifiedPlaces; ///< места, изменяемые в регионе (mod-set havoc)
        std::set<std::string> modifiedNames;         ///< имена изменяемых мест (`s := s+1` перепривязывает имя)
    };

    BodyEncoder(TrustToSmt& owner, const FuncDecl& f, const flow::MirFunction& fn, std::vector<SmtTerm> pre, std::vector<const AstNodeBase*> posts)
    : self(owner)
    , func(f)
    , mirFn(fn)
    , cfg(fn.cfg)
    , preTerms(std::move(pre))
    , postExprs(std::move(posts)) {}

    std::vector<SmtTerm> preTerms;
    std::vector<const AstNodeBase*> postExprs;
    std::vector<std::size_t> rpo;      ///< RPO-порядок блоков функции
    std::map<std::size_t, Loop> loops; ///< заголовок цикла → описание
    bool failed = false;
    /// Плотные идентификаторы мест (общий словарь с flow-анализами). Порядок регистрации строго
    /// детерминирован: параметры → блоки в порядке индекса → элементы внутри блока.
    flow::PlaceTable<const AstNodeBase*> places;
    std::vector<const AstNodeBase*> slotNodes; ///< PlaceId → узел (фолбэк по имени, havoc-имена)
    /// Предусловие функции: входит в КАЖДУЮ цель тела/цикла (как в VC функции), иначе
    /// отдельные обязательства проверялись бы без ограничений на вход.
    SmtTerm precondition;

    bool run();

  private:
    void computeRpo();
    void computeLoops();
    /// Исполнить регион: от `entry` до выхода из `region` (или до `stopHeader` — это back-edge).
    /// `backStates` заполняется состояниями на рёбрах в `stopHeader`.
    std::vector<ExitState> execute(std::size_t entry, const std::set<std::size_t>& region, std::size_t stopHeader, State seed,
                                   std::vector<ExitState>* backStates);
    /// Исполнить цикл стратегией (индукция/разворачивание) и вернуть выходы наружу.
    std::vector<ExitState> runLoop(std::size_t header, const State& pre);
    bool applyElements(std::size_t block, State& state);
    bool applyTerminator(std::size_t block, State& state, std::vector<ExitState>& edges);
    bool applyElement(const MirInstruction& element, State& state);
    bool applyWrite(const MirInstruction& element, State& state);
    bool applyCall(const MirInstruction& element, State& state);
    std::optional<SmtTerm> guardOf(const AstNodeBase* site, const VcValues& values) const;
    std::optional<SmtTerm> returnValue(const AstNodeBase* site, const VcValues& values);

    /// Слияние двух входных состояний: path = p1 ∨ p2; значение места — ite(p2, v2, v1);
    /// место, отсутствующее на одном из путей, из результата УДАЛЯЕТСЯ (не определено).
    State mergeStates(State first, State second);
    State havocLoopState(const State& pre, const Loop& loop);
    State mergeBackStates(const std::vector<ExitState>& backStates);
    void mergeIncoming(std::map<std::size_t, State>& incoming, std::size_t target, State state);
    std::optional<SmtTerm> encodeInvariants(const Loop& loop, const VcValues& values);
    void assertPreservation(const Loop& loop, const std::vector<ExitState>& backStates);
    /// PlaceId узла; при необходимости регистрирует его (формальные параметры вызываемой
    /// функции попадают в таблицу мест в точке вызова — порядок детерминирован).
    flow::PlaceId slotFor(const AstNodeBase* node);
    void declareHavoc(const std::string& name, const SmtSort& sort, MapperRange range);
    /// Условие цели: `pre ∧ path ∧ violation` (предусловие действует во всех обязательствах тела).
    void addGoal(SmtTerm path, SmtTerm violation, MapperRange range, VcObligationKind kind);
    SmtTerm coerceToPlaceSort(SmtTerm value, const AstNodeBase* place) const;
};

flow::PlaceId TrustToSmt::BodyEncoder::slotFor(const AstNodeBase* node) {
    EXPECT(node != nullptr && "slotFor: null place node");
    const flow::PlaceId id = places.add(node);
    if (slotNodes.size() <= id.value) {
        slotNodes.push_back(node);
    }
    return id;
}

void TrustToSmt::BodyEncoder::computeRpo() {
    const std::size_t count = cfg.blocks.size();
    std::vector<bool> visited(count, false);
    std::vector<std::pair<std::size_t, std::size_t>> stack; // (block, next successor index)
    std::vector<std::size_t> postorder;
    stack.emplace_back(cfg.entry, 0);
    visited[cfg.entry] = true;
    while (!stack.empty()) {
        const std::size_t block = stack.back().first;
        const std::size_t next = stack.back().second;
        if (next < cfg.blocks[block].successors.size()) {
            ++stack.back().second;
            const std::size_t target = cfg.blocks[block].successors[next].target;
            if (!visited[target]) {
                visited[target] = true;
                stack.emplace_back(target, 0);
            }
            continue;
        }
        postorder.push_back(block);
        stack.pop_back();
    }
    std::reverse(postorder.begin(), postorder.end());
    rpo = std::move(postorder);
}

void TrustToSmt::BodyEncoder::computeLoops() {
    const std::size_t count = cfg.blocks.size();
    // Доминаторы (по RPO): заголовок цикла доминирует все блоки своего тела.
    std::vector<std::set<std::size_t>> dom(count);
    std::set<std::size_t> allBlocks;
    for (std::size_t i = 0; i < count; ++i) {
        allBlocks.insert(i);
    }
    for (std::size_t i = 0; i < count; ++i) {
        // Недостижимые блоки не имеют доминаторов: иначе их рёбра дают ложные back-edge.
        dom[i] = i == cfg.entry ? std::set<std::size_t>{i} : (cfg.blocks[i].unreachable ? std::set<std::size_t>{} : allBlocks);
    }
    bool changed = true;
    while (changed) {
        changed = false;
        for (const std::size_t block : rpo) {
            if (block == cfg.entry) {
                continue;
            }
            std::optional<std::set<std::size_t>> merged;
            for (std::size_t pred = 0; pred < count; ++pred) {
                const bool isPred = std::any_of(cfg.blocks[pred].successors.begin(), cfg.blocks[pred].successors.end(),
                                                [block](const flow::FlowEdge& edge) { return edge.target == block; });
                if (!isPred) {
                    continue;
                }
                if (!merged) {
                    merged = dom[pred];
                    continue;
                }
                std::set<std::size_t> keep;
                for (const std::size_t item : *merged) {
                    if (dom[pred].count(item) != 0) {
                        keep.insert(item);
                    }
                }
                merged = std::move(keep);
            }
            if (!merged) {
                continue;
            }
            merged->insert(block);
            if (*merged != dom[block]) {
                dom[block] = std::move(*merged);
                changed = true;
            }
        }
    }
    // Заголовки: цель обратного ребра (u → v, где v доминирует u).
    std::set<std::size_t> headers;
    for (std::size_t from = 0; from < count; ++from) {
        if (cfg.blocks[from].unreachable) {
            continue;
        }
        for (const flow::FlowEdge& edge : cfg.blocks[from].successors) {
            if (edge.target != cfg.entry && dom[from].count(edge.target) != 0) {
                headers.insert(edge.target);
            }
        }
    }
    for (const std::size_t header : headers) {
        Loop loop;
        loop.header = header;
        std::vector<std::size_t> backSources;
        for (std::size_t from = 0; from < count; ++from) {
            if (dom[from].count(header) == 0) {
                continue;
            }
            const bool backEdge = std::any_of(cfg.blocks[from].successors.begin(), cfg.blocks[from].successors.end(),
                                              [header](const flow::FlowEdge& edge) { return edge.target == header; });
            if (backEdge) {
                backSources.push_back(from);
            }
        }
        // Регион = блоки под доминированием заголовка, достижимые до источников back-edge.
        std::set<std::size_t> dominated;
        for (std::size_t block = 0; block < count; ++block) {
            if (dom[block].count(header) != 0) {
                dominated.insert(block);
            }
        }
        std::set<std::size_t> canReachBack(backSources.begin(), backSources.end());
        bool grown = true;
        while (grown) {
            grown = false;
            for (std::size_t block = 0; block < count; ++block) {
                if (canReachBack.count(block) != 0 || dominated.count(block) == 0) {
                    continue;
                }
                const bool reaches = std::any_of(cfg.blocks[block].successors.begin(), cfg.blocks[block].successors.end(),
                                                 [&canReachBack](const flow::FlowEdge& edge) { return canReachBack.count(edge.target) != 0; });
                if (reaches) {
                    canReachBack.insert(block);
                    grown = true;
                }
            }
        }
        for (const std::size_t block : canReachBack) {
            if (dominated.count(block) != 0) {
                loop.region.insert(block);
            }
        }
        loop.region.insert(header);
        // Узел цикла — структурный факт MIR: terminator site заголовка (`while`) или источника
        // back-edge (`do-while`); адаптер ставит `terminate(Conditional, &stmt)` в обоих случаях.
        const auto isLoopNode = [](const AstNodeBase* candidate) {
            return candidate != nullptr && (candidate->is<WhileStmt>() || candidate->is<DoWhileStmt>());
        };
        loop.node = isLoopNode(cfg.blocks[header].terminatorNode.site) ? cfg.blocks[header].terminatorNode.site : nullptr;
        for (const std::size_t source : backSources) {
            if (loop.node != nullptr) {
                break;
            }
            if (isLoopNode(cfg.blocks[source].terminatorNode.site)) {
                loop.node = cfg.blocks[source].terminatorNode.site;
            }
        }
        if (loop.node == nullptr) {
            self.reportOutsideEnvelope(func, "loop form is not recognized at header block {}", header);
            failed = true;
            return;
        }
        loop.doWhile = loop.node->is<DoWhileStmt>();
        // mod-set: места, изменяемые в регионе (havoc при индукции по инварианту).
        for (const std::size_t block : loop.region) {
            for (const MirInstruction& element : cfg.blocks[block].elements) {
                if ((element.kind == MirOp::Write || element.kind == MirOp::Move || element.kind == MirOp::Declare) && element.place != nullptr) {
                    loop.modifiedPlaces.insert(element.place);
                    if (requiredText(*element.place).size() > 0) {
                        loop.modifiedNames.insert(std::string(requiredText(*element.place)));
                    }
                }
            }
        }
        loops.emplace(header, std::move(loop));
    }
    // Инварианты — из MIR-элементов `Invariant`; связь с циклом несёт `loopNode`.
    for (std::size_t block = 0; block < count; ++block) {
        for (const MirInstruction& element : cfg.blocks[block].elements) {
            if (element.kind != MirOp::Invariant) {
                continue;
            }
            if (element.site == nullptr || element.loopNode == nullptr) {
                self.report(func, "invariant element without contract/loop node (block {})", block);
                failed = true;
                return;
            }
            const auto owner = std::find_if(loops.begin(), loops.end(), [&](const auto& entry) { return entry.second.node == element.loopNode; });
            if (owner == loops.end()) {
                self.reportOutsideEnvelope(*element.site, "loop invariant is not attached to a recognized loop");
                failed = true;
                return;
            }
            owner->second.invariants.push_back(element.site);
        }
    }
}

bool TrustToSmt::BodyEncoder::run() {
    computeRpo();
    computeLoops();
    if (failed) {
        return false;
    }
    // Регистрация мест: параметры → элементы блоков. Порядок задаёт PlaceId и детерминизм формул.
    const auto registerSlot = [this](const AstNodeBase* node) {
        if (node != nullptr) {
            (void)slotFor(node);
        }
    };
    if (func.m_params) {
        for (const auto& param : *func.m_params) {
            if (param) {
                registerSlot(param.get());
            }
        }
    }
    for (const flow::FlowBlock& block : cfg.blocks) {
        for (const MirInstruction& element : block.elements) {
            registerSlot(element.kind == MirOp::Call ? element.site : element.place);
        }
    }
    self.m_curPlaces = &places;
    self.m_curSlotNodes = &slotNodes;
    // Входное состояние: параметры → их уникальные константы (func_param).
    State seed;
    seed.path = sb::makeConst("true", sb::boolSort());
    if (func.m_params) {
        for (const auto& param : *func.m_params) {
            if (!param || param->kind() != ParserToken::Kind::ArgNode) {
                continue;
            }
            const auto& arg = static_cast<const ArgNode&>(*param);
            const std::string name(arg.text());
            const auto smtName = self.m_paramSmtNames.find(name);
            if (smtName == self.m_paramSmtNames.end()) {
                continue;
            }
            const auto typeIt = self.m_paramTypes.find(name);
            const auto sort = typeIt != self.m_paramTypes.end() ? self.m_sorts.sortOf(typeIt->second) : std::nullopt;
            if (!sort) {
                self.reportOutsideEnvelope(*param, "parameter '{}' sort unknown", name);
                return false;
            }
            seed.values[self.slotOf(&arg)] = sb::makeNamedVar(smtName->second, *sort, param->range());
        }
    }
    self.m_entryState = seed.values;
    precondition = sb::mkAnd(preTerms, func.range());
    // Места, объявленные в функции: чтение места без значения — ICE, а не свободная константа.
    self.m_localPlaces.clear();
    std::set<std::size_t> allBlocks;
    for (std::size_t index = 0; index < cfg.blocks.size(); ++index) {
        allBlocks.insert(index);
        for (const MirInstruction& element : cfg.blocks[index].elements) {
            if (element.kind == MirOp::Declare && element.place != nullptr) {
                self.m_localPlaces.insert(element.place);
            }
        }
    }

    std::vector<ExitState> backStates;
    std::vector<ExitState> exits = execute(cfg.entry, allBlocks, kNoHeader, std::move(seed), &backStates);
    if (failed) {
        return false;
    }
    std::vector<ExitState> functionExits;
    for (ExitState& exit : exits) {
        if (exit.target != cfg.exit) {
            self.report(func, "unexpected control-flow edge to block {} (function exit expected)", exit.target);
            return false;
        }
        functionExits.push_back(std::move(exit));
    }
    if (functionExits.empty()) {
        self.reportOutsideEnvelope(func, "function has no modelable normal return path");
        return false;
    }

    // VC функции: pre ∧ ¬(∧_i (path_i → post_i)).
    std::vector<SmtTerm> obligations;
    for (const ExitState& exit : functionExits) {
        std::vector<SmtTerm> posts;
        for (const AstNodeBase* postExpr : postExprs) {
            const auto savedReturn = self.m_curReturn;
            const bool savedInPost = self.m_inPost;
            self.m_curReturn = exit.ret;
            self.m_inPost = true;
            auto post = self.toTerm(postExpr, std::nullopt, nullptr);
            self.m_inPost = savedInPost;
            self.m_curReturn = savedReturn;
            if (!post) {
                return false; // диагностика уже выдана (envelope/ICE)
            }
            posts.push_back(std::move(*post));
        }
        if (posts.empty()) {
            continue;
        }
        const SmtTerm post = sb::mkAnd(std::move(posts), func.range());
        obligations.push_back(app2("=>", exit.state.path, post, func.range()));
    }
    if (obligations.empty()) {
        return true; // проверять нечего (утверждения тела уже добавлены целями)
    }
    SmtTerm vc = sb::mkNot(std::make_shared<SmtTerm>(sb::mkAnd(std::move(obligations), func.range())), func.range());
    if (!preTerms.empty()) {
        vc = app2("and", sb::mkAnd(preTerms, func.range()), std::move(vc), func.range());
    }
    self.addGoal(std::move(vc), func.range(), self.m_curFuncName, VcObligationKind::kPost);
    return true;
}

std::vector<TrustToSmt::BodyEncoder::ExitState> TrustToSmt::BodyEncoder::execute(std::size_t entry, const std::set<std::size_t>& region, std::size_t stopHeader,
                                                                                 State seed, std::vector<ExitState>* backStates) {
    std::map<std::size_t, State> incoming;
    std::vector<ExitState> exits;
    incoming.emplace(entry, std::move(seed));
    const auto route = [&](ExitState edge) {
        if (edge.target == cfg.exit) {
            exits.push_back(std::move(edge)); // выход из функции всегда наружу
            return;
        }
        if (stopHeader != kNoHeader && edge.target == stopHeader) {
            if (backStates != nullptr) {
                backStates->push_back(std::move(edge));
            }
            return;
        }
        if (region.count(edge.target) != 0) {
            mergeIncoming(incoming, edge.target, std::move(edge.state));
            return;
        }
        exits.push_back(std::move(edge));
    };

    for (const std::size_t block : rpo) {
        if (region.count(block) == 0) {
            continue;
        }
        const auto it = incoming.find(block);
        if (it == incoming.end()) {
            continue; // недостижим в этом регионе
        }
        State state = std::move(it->second);
        incoming.erase(it);
        if (isFalseConst(state.path)) {
            continue;
        }
        if (block != stopHeader) {
            const auto loopIt = loops.find(block);
            if (loopIt != loops.end()) {
                // Вложенный цикл: заголовок исполняет стратегия (preheader — отдельный блок,
                // поэтому элементы заголовка принадлежат телу/условию и исполняются в волне).
                for (ExitState& exit : runLoop(block, state)) {
                    route(std::move(exit));
                }
                continue;
            }
        }
        if (!applyElements(block, state)) {
            return {};
        }
        std::vector<ExitState> edges;
        if (!applyTerminator(block, state, edges)) {
            return {};
        }
        for (ExitState& edge : edges) {
            route(std::move(edge));
        }
    }
    return exits;
}

std::vector<TrustToSmt::BodyEncoder::ExitState> TrustToSmt::BodyEncoder::runLoop(std::size_t header, const State& pre) {
    const auto loopIt = loops.find(header);
    if (loopIt == loops.end()) {
        FAULT("runLoop: block {} is not a loop header", header);
    }
    const Loop& loop = loopIt->second;
    int unroll = 0;
    for (const AstNodeBase* invariant : loop.invariants) {
        // Директива `z3_unroll(N)` — выражение инварианта-контракта (`invariant --> z3_unroll(N)`).
        const auto& contract = static_cast<const TrustContract&>(*invariant);
        unroll = std::max(unroll, invariantUnrollCount(contract.m_expr.get()));
    }
    if (unroll == 0 && loop.invariants.empty() && analysis::solverLoopUnrollEnabled(self.m_modes)) {
        unroll = self.m_unroll;
    }

    std::vector<ExitState> exits;
    if (unroll > 0) {
        // Bounded unrolling: N волн; каждое прохождение тела даёт свои обязательства.
        State seed = pre;
        for (int iteration = 0; iteration < unroll; ++iteration) {
            std::vector<ExitState> backs;
            std::vector<ExitState> wave = execute(header, loop.region, header, seed, &backs);
            if (failed) {
                return {};
            }
            exits.insert(exits.end(), std::make_move_iterator(wave.begin()), std::make_move_iterator(wave.end()));
            if (backs.empty()) {
                break;
            }
            seed = mergeBackStates(backs);
            if (isFalseConst(seed.path)) {
                break;
            }
        }
        return exits;
    }

    if (loop.invariants.empty()) {
        // Нет ни инварианта, ни разрешённого разворачивания: цикл не участвует в доказательстве.
        switch (self.m_modes.solverLoop) {
        case analysis::SolverLoopMode::kError:
            self.report(*loop.node, "loop without an invariant cannot be verified; add an invariant or enable unrolling");
            failed = true;
            return {};
        case analysis::SolverLoopMode::kIgnore:
            break;
        case analysis::SolverLoopMode::kWarning:
        default:
            self.m_services.diag().report(Severity::Warning, loop.node->range(),
                                     "loop without an invariant is not verified by the solver; add an invariant or enable unrolling");
            break;
        }
        self.reportOutsideEnvelope(*loop.node, "loop body without an invariant or unroll is not verified");
        failed = true;
        return {};
    }

    // Индукция по инварианту.
    // do-while: первая итерация безусловна — establishment не требуется; тело проверяем дважды:
    // от входного состояния и от произвольного состояния, удовлетворяющего инварианту.
    if (loop.doWhile) {
        std::vector<ExitState> backs;
        std::vector<ExitState> wave = execute(header, loop.region, header, pre, &backs);
        if (failed) {
            return {};
        }
        assertPreservation(loop, backs);
        exits.insert(exits.end(), std::make_move_iterator(wave.begin()), std::make_move_iterator(wave.end()));
    } else {
        const auto invariant = encodeInvariants(loop, pre.values);
        if (!invariant) {
            failed = true;
            return {};
        }
        addGoal(pre.path, notPath(*invariant), loop.node->range(), VcObligationKind::kInvariantEstablishment);
    }

    State havocked = havocLoopState(pre, loop);
    if (failed) {
        return {};
    }
    std::vector<ExitState> backs;
    std::vector<ExitState> wave = execute(header, loop.region, header, std::move(havocked), &backs);
    if (failed) {
        return {};
    }
    assertPreservation(loop, backs);
    exits.insert(exits.end(), std::make_move_iterator(wave.begin()), std::make_move_iterator(wave.end()));
    return exits;
}

TrustToSmt::BodyEncoder::State TrustToSmt::BodyEncoder::mergeStates(State first, State second) {
    // Копия условия второго входа: `second.path` перемещается в дизъюнкцию, а он ещё нужен
    // как селектор ite (иначе в формуле остаётся пустой терм).
    const SmtTerm selector = second.path;
    State merged;
    merged.path = orPath(std::move(first.path), std::move(second.path));
    for (auto& [place, value] : first.values) {
        const auto other = second.values.find(place);
        if (other == second.values.end()) {
            continue; // место не определено на одном из путей — из результата удаляется
        }
        merged.values[place] = iteValue(selector, other->second, value);
    }
    return merged;
}

void TrustToSmt::BodyEncoder::mergeIncoming(std::map<std::size_t, State>& incoming, std::size_t target, State state) {
    const auto it = incoming.find(target);
    if (it == incoming.end()) {
        incoming.emplace(target, std::move(state));
        return;
    }
    it->second = mergeStates(std::move(it->second), std::move(state));
}

TrustToSmt::BodyEncoder::State TrustToSmt::BodyEncoder::mergeBackStates(const std::vector<ExitState>& backStates) {
    if (backStates.empty()) {
        FAULT("mergeBackStates: no back-edge states");
    }
    State merged = backStates.front().state;
    for (std::size_t i = 1; i < backStates.size(); ++i) {
        merged = mergeStates(std::move(merged), backStates[i].state);
    }
    return merged;
}

void TrustToSmt::BodyEncoder::declareHavoc(const std::string& name, const SmtSort& sort, MapperRange range) {
    if (!self.m_declared.insert(name).second) {
        return;
    }
    SmtCommand command;
    command.kind = SmtCommandKind::kDeclareFun;
    command.fun_name = name;
    command.fun_result_sort = std::make_shared<SmtSort>(sort);
    command.srcRange = range;
    self.m_plan.declarations.push_back(std::move(command));
}

TrustToSmt::BodyEncoder::State TrustToSmt::BodyEncoder::havocLoopState(const State& pre, const Loop& loop) {
    State havocked;
    havocked.path = pre.path;
    for (const auto& [id, value] : pre.values) {
        const AstNodeBase* place = self.slotNode(id);
        if (loop.modifiedPlaces.count(place) == 0 && loop.modifiedNames.count(std::string(requiredText(*place))) == 0) {
            havocked.values[id] = value; // место в цикле не меняется — значение сохраняется
            continue;
        }
        const std::size_t offset = place->range().begin.isInvalid() ? 0 : place->range().begin.offset();
        const std::string name = "__havoc_" + std::string(requiredText(*place)) + "_" + std::to_string(offset);
        declareHavoc(name, value.sort, loop.node->range());
        havocked.values[id] = sb::makeNamedVar(name, value.sort, loop.node->range());
    }
    const auto invariant = encodeInvariants(loop, havocked.values);
    if (!invariant) {
        failed = true;
        havocked.path = sb::makeConst("false", sb::boolSort());
        return havocked;
    }
    havocked.path = andPath(std::move(havocked.path), std::move(*invariant));
    return havocked;
}

std::optional<SmtTerm> TrustToSmt::BodyEncoder::encodeInvariants(const Loop& loop, const VcValues& values) {
    std::vector<SmtTerm> terms;
    for (const AstNodeBase* invariant : loop.invariants) {
        const auto& contract = static_cast<const TrustContract&>(*invariant);
        if (!contract.m_expr) {
            continue;
        }
        auto term = self.toTerm(contract.m_expr.get(), std::nullopt, &values);
        if (!term) {
            return std::nullopt;
        }
        terms.push_back(std::move(*term));
    }
    if (terms.empty()) {
        self.reportOutsideEnvelope(*loop.node, "loop invariant is empty");
        return std::nullopt;
    }
    return sb::mkAnd(std::move(terms), loop.node->range());
}

void TrustToSmt::BodyEncoder::assertPreservation(const Loop& loop, const std::vector<ExitState>& backStates) {
    if (failed) {
        return;
    }
    for (const ExitState& back : backStates) {
        const auto invariant = encodeInvariants(loop, back.state.values);
        if (!invariant) {
            failed = true;
            return;
        }
        addGoal(back.state.path, notPath(*invariant), loop.node->range(), VcObligationKind::kInvariantPreservation);
    }
}

void TrustToSmt::BodyEncoder::addGoal(SmtTerm path, SmtTerm violation, MapperRange range, VcObligationKind kind) {
    SmtTerm condition = andPath(std::move(path), std::move(violation));
    if (!preTerms.empty()) {
        condition = andPath(precondition, std::move(condition));
    }
    self.addGoal(std::move(condition), range, self.m_curFuncName, kind);
}

SmtTerm TrustToSmt::BodyEncoder::coerceToPlaceSort(SmtTerm value, const AstNodeBase* place) const {
    // Ширина значения не сужается: литерал Int8 в состоянии Int32-места приводится к ширине места,
    // иначе ite-слияние ветвей даст рассогласование разрядности.
    if (place == nullptr || place->kind() != ParserToken::Kind::VarDecl) {
        return value;
    }
    const auto& decl = static_cast<const VarDecl&>(*place);
    // Тип — материализованный факт семантики (имя типа резолвит только она).
    const TypeId type = resolvedTypeOf(decl.m_type.get(), decl.inferredType.getTypeId());
    if (type == INVALID_TYPE_ID) {
        return value;
    }
    const auto sort = self.m_sorts.sortOf(type);
    if (!sort || sort->kind != SmtSortKind::kBitVec || value.sort.kind != SmtSortKind::kBitVec || sort->bv_width <= value.sort.bv_width) {
        return value;
    }
    return sb::coerceToWidth(std::move(value), *sort, self.m_sorts.isSignedType(type));
}

bool TrustToSmt::BodyEncoder::applyElements(std::size_t block, State& state) {
    const flow::FlowBlock& node = cfg.blocks[block];
    for (const MirInstruction& element : node.elements) {
        if (element.kind == MirOp::Terminator) {
            self.report(func, "terminator instruction appears in block elements (block {})", block);
            return false;
        }
        if (!applyElement(element, state)) {
            return false;
        }
    }
    return true;
}

bool TrustToSmt::BodyEncoder::applyTerminator(std::size_t block, State& state, std::vector<ExitState>& edges) {
    const flow::FlowBlock& node = cfg.blocks[block];
    switch (node.terminator) {
    case flow::TerminatorKind::Conditional: {
        const auto guard = guardOf(node.terminatorNode.site, state.values);
        if (!guard) {
            if (!failed) {
                self.reportOutsideEnvelope(*node.terminatorNode.site, "branch condition is not modelable");
                failed = true;
            }
            return false;
        }
        for (const flow::FlowEdge& edge : node.successors) {
            State next = state;
            const SmtTerm condition = edge.kind == flow::EdgeKind::True ? *guard : notPath(*guard);
            next.path = andPath(std::move(next.path), condition);
            edges.push_back(ExitState{edge.target, std::move(next), std::nullopt});
        }
        return true;
    }
    case flow::TerminatorKind::Return: {
        auto ret = returnValue(node.terminatorNode.site, state.values);
        if (failed) {
            return false;
        }
        for (const flow::FlowEdge& edge : node.successors) {
            edges.push_back(ExitState{edge.target, state, ret});
        }
        return true;
    }
    case flow::TerminatorKind::Throw:
        self.reportOutsideEnvelope(*node.terminatorNode.site, "exceptions (throw) are not modeled");
        failed = true;
        return false;
    case flow::TerminatorKind::Switch:
        self.reportOutsideEnvelope(*node.terminatorNode.site, "match statement is not modeled");
        failed = true;
        return false;
    case flow::TerminatorKind::Dispatch:
        self.reportOutsideEnvelope(*node.terminatorNode.site, "with/try control flow is not modeled");
        failed = true;
        return false;
    case flow::TerminatorKind::Goto:
    case flow::TerminatorKind::Break:
    case flow::TerminatorKind::Continue:
    case flow::TerminatorKind::Fallthrough:
    default:
        for (const flow::FlowEdge& edge : node.successors) {
            edges.push_back(ExitState{edge.target, state, std::nullopt});
        }
        return true;
    }
}

bool TrustToSmt::BodyEncoder::applyElement(const MirInstruction& element, State& state) {
    switch (element.kind) {
    case MirOp::Declare: {
        if (element.place != nullptr && element.place->kind() == ParserToken::Kind::VarDecl) {
            const auto& decl = static_cast<const VarDecl&>(*element.place);
            if (decl.initKind() == VarInitKind::Initialized) {
                auto value = self.toTerm(decl.m_initializer.get(), std::nullopt, &state.values);
                if (!value) {
                    return false;
                }
                state.values[self.slotOf(element.place)] = coerceToPlaceSort(std::move(*value), element.place);
            }
            // Переменный trust-контракт (`y trust_assert(...) := expr`): проверяется при y = init.
            for (const auto& trust : decl.m_trust) {
                auto* contract = dynamic_cast<TrustContract*>(trust.get());
                if (!contract || !contract->m_expr) {
                    continue;
                }
                if (contract->kind != PropertyKind::Assert && contract->kind != PropertyKind::kUnknown) {
                    continue;
                }
                auto term = self.toTerm(contract->m_expr.get(), std::nullopt, &state.values);
                if (!term) {
                    return false;
                }
                addGoal(state.path, notPath(*term), contract->range(), VcObligationKind::kAssert);
            }
        }
        return true;
    }
    case MirOp::Write:
        return applyWrite(element, state);
    case MirOp::Move:
        self.reportOutsideEnvelope(*element.site, "move/reset (swap, '= _') is not modeled");
        failed = true;
        return false;
    case MirOp::Call:
        return applyCall(element, state);
    case MirOp::Assert: {
        const AstNodeBase* site = element.site;
        if (site == nullptr || site->kind() != ParserToken::Kind::TrustContract) {
            self.report(func, "assert element without a contract node");
            return false;
        }
        const auto& contract = static_cast<const TrustContract&>(*site);
        if (!contract.m_expr) {
            return true;
        }
        auto term = self.toTerm(contract.m_expr.get(), std::nullopt, &state.values);
        if (!term) {
            return false;
        }
        addGoal(state.path, notPath(*term), contract.range(), VcObligationKind::kAssert);
        return true;
    }
    case MirOp::Read:
        return true; // чтение уже учтено кодировщиком выражений (место → состояние)
    case MirOp::Invariant:
        return true; // обрабатывается правилом цикла
    case MirOp::ScopeExit:
        return true; // время жизни места не влияет на SSA-значения (ключ — объявление)
    case MirOp::Terminator:
        self.report(func, "terminator instruction reached element processing");
        return false;
    }
    return true;
}

bool TrustToSmt::BodyEncoder::applyCall(const MirInstruction& element, State& state) {
    const AstNodeBase* site = element.site;
    if (site == nullptr || site->kind() != ParserToken::Kind::CallExpr) {
        self.report(func, "call element without a call expression");
        return false;
    }
    auto value = self.toTerm(site, std::nullopt, &state.values);
    if (!value) {
        return false;
    }
    state.values[self.slotOf(site)] = std::move(*value); // результат вызова вычислен ровно один раз

    // Контрактная модель вызова: предусловия callee проверяются в точке вызова.
    const AstNodeBase* calleeNode = element.place;
    if (calleeNode == nullptr || calleeNode->kind() != ParserToken::Kind::FuncDecl) {
        return true;
    }
    const auto& callee = static_cast<const FuncDecl&>(*calleeNode);
    const auto& call = static_cast<const CallExpr&>(*site);
    if (callee.m_trust.empty()) {
        return true; // без контракта callee вне области проверки (диагностика выдана в toTerm)
    }
    for (const auto& trust : callee.m_trust) {
        auto* contract = dynamic_cast<TrustContract*>(trust.get());
        if (!contract || !contract->m_expr || contract->kind != PropertyKind::Pre) {
            continue;
        }
        // Фактические аргументы связываются с формальными параметрами callee (по ArgNode).
        VcValues argumentValues = state.values;
        const std::size_t argumentCount = call.m_args ? call.m_args->size() : 0;
        for (std::size_t i = 0; i < argumentCount && callee.m_params && i < callee.m_params->size(); ++i) {
            const AstNodePtr& argExpr = (*call.m_args)[i];
            const AstNodePtr& param = (*callee.m_params)[i];
            if (!argExpr || !param || param->kind() != ParserToken::Kind::ArgNode) {
                continue;
            }
            auto argument = self.toTerm(argExpr.get(), std::nullopt, &state.values);
            if (!argument) {
                return false;
            }
            argumentValues[slotFor(param.get())] = std::move(*argument);
        }
        auto precondition = self.toTerm(contract->m_expr.get(), std::nullopt, &argumentValues);
        if (!precondition) {
            return false;
        }
        addGoal(state.path, notPath(*precondition), contract->range(), VcObligationKind::kCallPre);
    }
    return true;
}

bool TrustToSmt::BodyEncoder::applyWrite(const MirInstruction& element, State& state) {
    if (element.place == nullptr) {
        return true; // запись в не-место не меняет состояние
    }
    if (element.projection != nullptr) {
        self.reportOutsideEnvelope(*element.site, "write through a field/index projection is not modeled");
        failed = true;
        return false;
    }
    const AstNodeBase* site = element.site;
    if (site == nullptr) {
        self.report(func, "write element without a source node");
        return false;
    }
    if (site->kind() == ParserToken::Kind::NameDecl) {
        // Объявление с инициализацией (`y := x`): значение — инициализатор, место — узел имени.
        // Если имя уже есть в состоянии (идиома `s := s + 1`), запись идёт в СУЩЕСТВУЮЩЕЕ место:
        // `:=` не создаёт новое место на каждой итерации, иначе инвариант/постусловие теряют связь.
        const auto& binary = static_cast<const Binary&>(*site);
        if (!binary.m_right || isNoneMarker(binary.m_right.get())) {
            state.values.erase(self.slotOf(element.place));
            return true;
        }
        auto value = self.toTerm(binary.m_right.get(), std::nullopt, &state.values);
        if (!value) {
            return false;
        }
        flow::PlaceId key = self.slotOf(element.place);
        for (const auto& [id, term] : state.values) {
            const AstNodeBase* place = self.slotNode(id);
            if (place != nullptr && place != element.place && requiredText(*place) == requiredText(*element.place)) {
                key = id;
                break;
            }
        }
        const AstNodeBase* keyNode = self.slotNode(key);
        state.values[key] = coerceToPlaceSort(std::move(*value), keyNode);
        return true;
    }
    if (site->kind() == ParserToken::Kind::AssignOp) {
        const auto& binary = static_cast<const Binary&>(*site);
        if (isSwapOp(binary.m_op)) {
            self.reportOutsideEnvelope(binary, "swap is not modeled");
            failed = true;
            return false;
        }
        const bool reset = !binary.m_right || isNoneMarker(binary.m_right.get());
        if (reset) {
            state.values.erase(self.slotOf(element.place));
            return true;
        }
        auto value = self.toTerm(binary.m_right.get(), std::nullopt, &state.values);
        if (!value) {
            return false;
        }
        if (isCompoundAssignOp(binary.m_op)) {
            // Составное присваивание: `x op= rhs` — новое значение = x op rhs.
            const auto current = state.values.find(self.slotOf(element.place));
            if (current == state.values.end()) {
                self.report(binary, "compound assignment to a place without a value (uninitialized)");
                return false;
            }
            const auto baseOp = compoundOpName(binary.m_op);
            if (!baseOp) {
                self.reportOutsideEnvelope(binary, "compound assignment operator is not modelable");
                failed = true;
                return false;
            }
            const bool bitVec = current->second.sort.kind == SmtSortKind::kBitVec;
            // Знак — из целочисленного факта типа операнда/результата (не-целые пропускаются).
            const bool signedOp =
                self.m_sorts.integerSignOf(binary.lhsType.getTypeId()).value_or(self.m_sorts.integerSignOf(binary.resultType.getTypeId()).value_or(true));
            const std::string_view smtOp = sb::mapArithOp(*baseOp, bitVec, signedOp);
            if (smtOp.empty()) {
                self.reportOutsideEnvelope(binary, "compound assignment operator is not modelable");
                failed = true;
                return false;
            }
            value = sb::makeApp(std::string(smtOp), current->second.sort,
                            {std::make_shared<SmtTerm>(current->second), std::make_shared<SmtTerm>(std::move(*value))}, binary.range());
        }
        state.values[self.slotOf(element.place)] = coerceToPlaceSort(std::move(*value), element.place);
        return true;
    }
    if (site->kind() == ParserToken::Kind::IncDecOp) {
        const auto& binary = static_cast<const Binary&>(*site);
        const auto current = state.values.find(self.slotOf(element.place));
        if (current == state.values.end()) {
            self.report(binary, "increment/decrement of a place without a value (uninitialized)");
            return false;
        }
        const bool bitVec = current->second.sort.kind == SmtSortKind::kBitVec;
        const bool signedOp =
            self.m_sorts.integerSignOf(binary.lhsType.getTypeId()).value_or(self.m_sorts.integerSignOf(binary.resultType.getTypeId()).value_or(true));
        const auto smtOp = sb::mapArithOp(isIncrementOp(binary.m_op) ? "+" : "-", bitVec, signedOp);
        if (smtOp.empty()) {
            self.reportOutsideEnvelope(binary, "increment/decrement is not modelable");
            failed = true;
            return false;
        }
        const SmtTerm one = sb::makeConst("1", current->second.sort, binary.range());
        state.values[self.slotOf(element.place)] =
            sb::makeApp(std::string(smtOp), current->second.sort, {std::make_shared<SmtTerm>(current->second), std::make_shared<SmtTerm>(one)}, binary.range());
        return true;
    }
    if (site->kind() == ParserToken::Kind::AppendStmt) {
        self.reportOutsideEnvelope(*site, "array append is not modeled");
        failed = true;
        return false;
    }
    self.reportOutsideEnvelope(*site, "write of this statement kind is not modeled");
    failed = true;
    return false;
}

std::optional<SmtTerm> TrustToSmt::BodyEncoder::guardOf(const AstNodeBase* site, const VcValues& values) const {
    if (site == nullptr) {
        return std::nullopt;
    }
    if (site->is<IfStmt>()) {
        const auto& stmt = static_cast<const IfStmt&>(*site);
        return stmt.m_cond ? self.toTerm(stmt.m_cond.get(), std::nullopt, &values) : std::nullopt;
    }
    if (site->is<WhileStmt>()) {
        const auto& stmt = static_cast<const WhileStmt&>(*site);
        return stmt.m_cond ? self.toTerm(stmt.m_cond.get(), std::nullopt, &values) : std::nullopt;
    }
    if (site->is<DoWhileStmt>()) {
        const auto& stmt = static_cast<const DoWhileStmt&>(*site);
        return stmt.m_cond ? self.toTerm(stmt.m_cond.get(), std::nullopt, &values) : std::nullopt;
    }
    // Разрыв короткого замыкания (`&&`/`||` не имеют своего kind-класса: это LogicalOp-Binary).
    if (site->kind() == ParserToken::Kind::LogicalOp) {
        const auto& binary = static_cast<const Binary&>(*site);
        return binary.m_left ? self.toTerm(binary.m_left.get(), std::nullopt, &values) : std::nullopt;
    }
    return std::nullopt;
}

std::optional<SmtTerm> TrustToSmt::BodyEncoder::returnValue(const AstNodeBase* site, const VcValues& values) {
    if (site == nullptr || site->kind() != ParserToken::Kind::ReturnStmt) {
        return std::nullopt; // void-возврат (значения нет)
    }
    const auto& jump = static_cast<const JumpStmt&>(*site);
    // Hoist-временная возврата создана семантикой: исходное выражение — в её инициализаторе.
    const AstNodeBase* valueNode = jump.m_value.get();
    if (jump.m_tempDecl && jump.m_tempDecl->kind() == ParserToken::Kind::VarDecl) {
        const auto* decl = static_cast<const VarDecl*>(jump.m_tempDecl.get());
        if (decl->m_initializer) {
            valueNode = decl->m_initializer.get();
        }
    }
    if (valueNode == nullptr) {
        return std::nullopt; // void
    }
    std::optional<SmtSort> expected;
    if (self.m_curResultSort && (self.m_curResultSort->kind == SmtSortKind::kBitVec || self.m_curResultSort->kind == SmtSortKind::kReal)) {
        expected = self.m_curResultSort;
    }
    auto value = self.toTerm(valueNode, expected, &values);
    if (!value) {
        failed = true;
        return std::nullopt;
    }
    if (self.m_curResultSort && self.m_curResultSort->kind == SmtSortKind::kBitVec && value->sort.kind == SmtSortKind::kBitVec) {
        *value = sb::coerceToWidth(std::move(*value), *self.m_curResultSort, self.m_curResultSigned);
    }
    return value;
}

bool TrustToSmt::encodeBody(const FuncDecl& f, const flow::MirFunction& fn, std::vector<SmtTerm> preTerms, std::vector<const AstNodeBase*> postExprs) {
    BodyEncoder encoder(*this, f, fn, std::move(preTerms), std::move(postExprs));
    const bool ok = encoder.run();
    // Таблица мест жила только на время исполнения тела (см. m_curPlaces).
    m_curPlaces = nullptr;
    m_curSlotNodes = nullptr;
    return ok;
}

} // namespace solver
} // namespace trust
