// test/unit/ir/dataflow_test.cpp
// Обобщённый dataflow: join (AND/OR), fixpoint на цикле, forward/backward,
// per-edge effect, недостижимые блоки.

#include "ir/dataflow.hpp"

#include <gtest/gtest.h>

#include <stdexcept>
#include <utility>
#include <vector>

namespace trust::flow {
namespace {

ControlFlowGraph makeCfg(std::vector<FlowBlock> blocks, std::size_t entry, std::size_t exit) {
    ControlFlowGraph cfg;
    cfg.entry = entry;
    cfg.exit = exit;
    cfg.blocks = std::move(blocks);
    for (std::size_t i = 0; i < cfg.blocks.size(); ++i) {
        cfg.blocks[i].id = i;
    }
    return cfg;
}

FlowEdge edge(std::size_t target, EdgeKind kind = EdgeKind::Normal) {
    return FlowEdge{.kind = kind, .target = target};
}

/// May-analysis: состояние "видели X" (OR).
struct MaySeen {
    using State = bool;
    static constexpr Direction direction = Direction::Forward;

    bool boundary() const { return false; }
    bool bottom() const { return false; }
    bool unreachableState() const { return true; }

    bool join(bool& dst, bool src) const {
        const bool old = dst;
        dst = dst || src;
        return old != dst;
    }
    bool transferBlock(const FlowBlock& block, bool state) const { return state || block.id == 2; }
};

/// Must-analysis: состояние "X точно есть" (AND).
struct MustSeen {
    using State = bool;
    static constexpr Direction direction = Direction::Forward;

    bool boundary() const { return true; }
    bool bottom() const { return true; }
    bool unreachableState() const { return true; }

    bool join(bool& dst, bool src) const {
        const bool old = dst;
        dst = dst && src;
        return old != dst;
    }
    bool transferBlock(const FlowBlock& block, bool state) const {
        if (block.id == 1) {
            return false;
        }
        return state;
    }
};

/// Backward may-analysis: состояние "X встретится после" (OR).
struct BackwardSeen {
    using State = bool;
    static constexpr Direction direction = Direction::Backward;

    bool boundary() const { return true; }
    bool bottom() const { return false; }
    bool unreachableState() const { return false; }

    bool join(bool& dst, bool src) const {
        const bool old = dst;
        dst = dst || src;
        return old != dst;
    }
    bool transferBlock(const FlowBlock& block, bool state) const { return state || block.id == 2; }
};

/// Edge-effect: состояние меняется только на Exception-ребре.
struct EdgeEffect {
    using State = bool;
    static constexpr Direction direction = Direction::Forward;

    bool boundary() const { return false; }
    bool bottom() const { return false; }
    bool unreachableState() const { return false; }

    bool join(bool& dst, bool src) const {
        const bool old = dst;
        dst = dst || src;
        return old != dst;
    }
    bool transferBlock(const FlowBlock&, bool) const { return false; }
    bool transferEdge(const FlowBlock&, EdgeKind kind, bool state) const { return state || kind == EdgeKind::Exception; }
};

TEST(Dataflow, ForwardMayJoinDiamond) {
    auto cfg = makeCfg(
        {
            FlowBlock{.successors = {edge(1), edge(2)}},
            FlowBlock{.successors = {edge(3)}},
            FlowBlock{.successors = {edge(3)}},
            FlowBlock{},
        },
        0, 3);

    const auto result = runDataflow(cfg, MaySeen{});
    EXPECT_EQ(result.in[3], true);
    EXPECT_TRUE(result.reachable[0]);
    EXPECT_TRUE(result.reachable[1]);
    EXPECT_TRUE(result.reachable[2]);
    EXPECT_TRUE(result.reachable[3]);
}

TEST(Dataflow, ForwardMustJoinDiamond) {
    auto cfg = makeCfg(
        {
            FlowBlock{.successors = {edge(1), edge(2)}},
            FlowBlock{.successors = {edge(3)}},
            FlowBlock{.successors = {edge(3)}},
            FlowBlock{},
        },
        0, 3);

    const auto result = runDataflow(cfg, MustSeen{});
    EXPECT_EQ(result.in[3], false); // путь через блок 1 не даёт X
}

TEST(Dataflow, LoopReachesFixpointAndPropagates) {
    auto cfg = makeCfg(
        {
            FlowBlock{.successors = {edge(1)}},
            FlowBlock{.successors = {edge(2, EdgeKind::True), edge(3, EdgeKind::False)}},
            FlowBlock{.successors = {edge(1)}},
            FlowBlock{},
        },
        0, 3);

    const auto result = runDataflow(cfg, MaySeen{});
    EXPECT_EQ(result.in[1], true);
    EXPECT_EQ(result.in[2], true);
    EXPECT_EQ(result.in[3], true);
}

TEST(Dataflow, BackwardPropagatesFromExit) {
    auto cfg = makeCfg(
        {
            FlowBlock{.successors = {edge(1)}},
            FlowBlock{.successors = {edge(2)}},
            FlowBlock{},
        },
        0, 2);

    const auto result = runDataflow(cfg, BackwardSeen{});
    EXPECT_EQ(result.in[0], true);
    EXPECT_TRUE(result.reachable[0]);
    EXPECT_TRUE(result.reachable[1]);
    EXPECT_TRUE(result.reachable[2]);
}

TEST(Dataflow, TransferEdgeEffect) {
    auto cfg = makeCfg(
        {
            FlowBlock{.successors = {edge(1, EdgeKind::Exception)}},
            FlowBlock{},
        },
        0, 1);

    const auto result = runDataflow(cfg, EdgeEffect{});
    EXPECT_EQ(result.in[1], true);
}

TEST(Dataflow, UnreachableBlockGetsUnreachableState) {
    auto cfg = makeCfg(
        {
            FlowBlock{.successors = {edge(1)}},
            FlowBlock{.successors = {edge(2)}},
            FlowBlock{},
            FlowBlock{},
        },
        0, 2);
    cfg.blocks[3].unreachable = true;

    const auto result = runDataflow(cfg, MaySeen{});
    EXPECT_FALSE(result.reachable[3]);
    EXPECT_EQ(result.in[3], true);
    EXPECT_EQ(result.out[3], true);
}

/// Generic element: граф не обязан содержать AST-узлы.
struct SumElements {
    using State = int;
    static constexpr Direction direction = Direction::Forward;

    int boundary() const { return 0; }
    int bottom() const { return 0; }
    int unreachableState() const { return 0; }

    bool join(int& dst, int src) const {
        const int old = dst;
        dst = old > src ? old : src;
        return old != dst;
    }
    int transferElement(const int& element, int state) const { return state + element; }
};

TEST(Dataflow, GenericElementGraph) {
    CfgT<int> cfg;
    cfg.entry = 0;
    cfg.exit = 1;
    cfg.blocks.push_back(BasicBlockT<int>{.id = 0, .elements = {1, 2}, .successors = {edge(1)}});
    cfg.blocks.push_back(BasicBlockT<int>{.id = 1});

    const auto result = runDataflow(cfg, SumElements{});
    EXPECT_EQ(result.out[0], 3);
    EXPECT_EQ(result.in[1], 3);
}

TEST(Dataflow, InvalidCfgRaisesError) {
    CfgT<int> empty;
    EXPECT_THROW(runDataflow(empty, SumElements{}), std::runtime_error);

    CfgT<int> badTarget;
    badTarget.entry = 0;
    badTarget.exit = 0;
    badTarget.blocks.push_back(BasicBlockT<int>{.id = 0, .successors = {edge(5)}});
    EXPECT_THROW(runDataflow(badTarget, SumElements{}), std::runtime_error);
}

} // namespace
} // namespace trust::flow
