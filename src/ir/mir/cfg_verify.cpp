// Структурный верификатор MIR (см. include/ir/cfg.hpp): проверяет инварианты формы
// графа и обязательные поля элементов. Возвращает ПЕРВОЕ нарушение (nullopt — нарушений нет).

#include "ir/cfg.hpp"

#include "ast/ast_nodes.hpp"
#include "ast/ast_nodes.hpp"
#include "ast/trust_prop.hpp"

#include <format>
#include <optional>
#include <string>
#include <vector>

namespace trust::flow {
namespace {

[[nodiscard]] std::string blockNote(const FuncDecl& func, std::size_t block) {
    return std::format(" (function '{}', block {})", func.text(), block);
}

[[nodiscard]] std::optional<std::string> checkElements(const MirFunction& function) {
    const FuncDecl& func = *function.func;
    for (const FlowBlock& block : function.cfg.blocks) {
        for (const MirInstruction& element : block.elements) {
            const std::string where = blockNote(func, block.id);
            switch (element.kind) {
            case MirOp::Declare:
                if (element.place == nullptr) {
                    return "flow verify: Declare without a place" + where;
                }
                break;
            case MirOp::Read:
            case MirOp::Write:
            case MirOp::Move:
                if (element.place == nullptr || element.site == nullptr) {
                    return std::format("flow verify: {} without place/site{}", mirOpName(element.kind), where);
                }
                if (element.projection == element.place) {
                    return "flow verify: projection must not equal the place root" + where;
                }
                break;
            case MirOp::Call:
                if (element.site == nullptr) {
                    return "flow verify: Call without a site" + where;
                }
                break;
            case MirOp::ScopeExit:
                if (element.scope == nullptr || element.place == nullptr || element.site == nullptr) {
                    return "flow verify: ScopeExit without scope/place/site" + where;
                }
                break;
            case MirOp::Assert:
            case MirOp::Invariant: {
                if (element.site == nullptr || !element.site->is<TrustContract>()) {
                    return std::format("flow verify: {} element is not backed by a TrustContract{}", mirOpName(element.kind), where);
                }
                const auto* contract = element.site->as<TrustContract>();
                const bool invariant = contract->kind == PropertyKind::Invariant;
                if (invariant != (element.kind == MirOp::Invariant)) {
                    return "flow verify: contract element kind does not match PropertyKind" + where;
                }
                // Связь инварианта с циклом — часть контракта MIR: верификатор не выводит её сам.
                if (element.kind == MirOp::Invariant) {
                    const bool attached = element.loopNode != nullptr && (element.loopNode->is<WhileStmt>() || element.loopNode->is<DoWhileStmt>());
                    if (!attached) {
                        return "flow verify: Invariant without an attached loop node" + where;
                    }
                } else if (element.loopNode != nullptr) {
                    return "flow verify: Assert must not carry a loop node" + where;
                }
                break;
            }
            case MirOp::Terminator:
                return "flow verify: Terminator must not appear in block elements" + where;
            }
        }
    }
    return std::nullopt;
}

[[nodiscard]] std::optional<std::string> checkEdges(const MirFunction& function) {
    const FuncDecl& func = *function.func;
    const ControlFlowGraph& cfg = function.cfg;
    // Сначала проверяем цели рёбер: достижимость вычисляется только по валидным индексам.
    for (std::size_t index = 0; index < cfg.blocks.size(); ++index) {
        for (const FlowEdge& edge : cfg.blocks[index].successors) {
            if (edge.target >= cfg.blocks.size()) {
                return std::format("flow verify: edge target {} is out of range{}", edge.target, blockNote(func, index));
            }
        }
    }
    std::vector<bool> reachable(cfg.blocks.size(), false);
    std::vector<std::size_t> work{cfg.entry};
    reachable[cfg.entry] = true;
    while (!work.empty()) {
        const std::size_t index = work.back();
        work.pop_back();
        for (const FlowEdge& edge : cfg.blocks[index].successors) {
            if (!reachable[edge.target]) {
                reachable[edge.target] = true;
                work.push_back(edge.target);
            }
        }
    }
    for (std::size_t index = 0; index < cfg.blocks.size(); ++index) {
        const FlowBlock& block = cfg.blocks[index];
        const std::string where = blockNote(func, index);
        if (block.id != index) {
            return "flow verify: block id does not match its position" + where;
        }
        if (block.unreachable == reachable[index]) {
            return "flow verify: unreachable flag contradicts reachability from entry" + where;
        }
        if (index == cfg.exit && !block.successors.empty()) {
            return "flow verify: exit block must not have successors" + where;
        }
        if (block.terminator != TerminatorKind::Fallthrough) {
            if (block.terminatorNode.kind != MirOp::Terminator || block.terminatorNode.site == nullptr) {
                return "flow verify: terminator has no source node" + where;
            }
        }
        switch (block.terminator) {
        case TerminatorKind::Fallthrough:
            if (block.successors.size() > 1) {
                return "flow verify: fallthrough block has more than one successor" + where;
            }
            break;
        case TerminatorKind::Conditional: {
            if (block.successors.size() != 2) {
                return "flow verify: conditional block must have exactly two successors" + where;
            }
            const bool hasTrue = block.successors[0].kind == EdgeKind::True || block.successors[1].kind == EdgeKind::True;
            const bool hasFalse = block.successors[0].kind == EdgeKind::False || block.successors[1].kind == EdgeKind::False;
            if (!hasTrue || !hasFalse) {
                return "flow verify: conditional block must branch on True/False edges" + where;
            }
            break;
        }
        case TerminatorKind::Switch:
        case TerminatorKind::Dispatch:
            if (block.successors.empty()) {
                return "flow verify: dispatch/switch block without successors" + where;
            }
            break;
        case TerminatorKind::Return:
        case TerminatorKind::Throw:
        case TerminatorKind::Break:
        case TerminatorKind::Continue:
        case TerminatorKind::Goto:
            if (block.successors.size() != 1) {
                return std::format("flow verify: terminator {} must have exactly one successor{}", static_cast<int>(block.terminator), where);
            }
            break;
        }
    }
    if (cfg.blocks[cfg.entry].successors.empty()) {
        return std::format("flow verify: entry block has no successors (function '{}')", func.text());
    }
    return std::nullopt;
}

} // namespace

std::optional<std::string> verifyFlowModule(const MirModule& mir) {
    for (std::size_t index = 0; index < mir.functions.size(); ++index) {
        const MirFunction& function = mir.functions[index];
        if (function.func == nullptr) {
            return std::format("flow verify: function #{} has no FuncDecl", index);
        }
        if (function.cfg.blocks.empty()) {
            return std::format("flow verify: function '{}' has no blocks", function.func->text());
        }
        if (function.cfg.entry >= function.cfg.blocks.size() || function.cfg.exit >= function.cfg.blocks.size()) {
            return std::format("flow verify: function '{}' has out-of-range entry/exit", function.func->text());
        }
        if (function.cfg.entry == function.cfg.exit) {
            return std::format("flow verify: function '{}' has entry == exit", function.func->text());
        }
        if (auto violation = checkElements(function)) {
            return violation;
        }
        if (auto violation = checkEdges(function)) {
            return violation;
        }
    }
    return std::nullopt;
}

} // namespace trust::flow
