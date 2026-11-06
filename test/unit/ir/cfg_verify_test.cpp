// test/unit/ir/cfg_verify_test.cpp
// Структурный верификатор MIR: валидный модуль принимается, нарушения формы/полей — нет.
#include "ir/cfg.hpp"
#include "ast/ast_nodes.hpp"
#include "ast/ast_nodes.hpp"
#include "ast/trust_prop.hpp"
#include "attrs/attr_pool.hpp"

#include <gtest/gtest.h>

#include <memory>
#include <string>
#include <vector>

namespace trust::flow {
namespace {

std::shared_ptr<FuncDecl> func(std::string name, std::vector<AstNodePtr> body = {}) {
    auto node = std::make_shared<FuncDecl>(std::move(name));
    node->m_definition = Definition::fromBody(std::move(body));
    return node;
}

AstNodePtr literal(std::string text = "1") {
    return std::make_shared<Literal>(ParserToken::Kind::IntLiteral, std::move(text));
}

AstNodePtr var(std::string name, AstNodePtr init = nullptr) {
    return std::make_shared<VarDecl>(std::move(name), nullptr, std::move(init));
}

/// Модуль одной функции с телом `x := 1; y := 2;` — валидная форма.
MirModule sampleModule(AttrPool& attrs) {
    std::vector<AstNodePtr> body;
    body.push_back(var("x", literal("1")));
    body.push_back(var("y", literal("2")));
    std::vector<AstNodePtr> roots{func("f", std::move(body))};
    return buildMir(roots, attrs);
}

TEST(FlowVerify, AcceptsBuiltModule) {
    AttrPool attrs;
    MirModule mir = sampleModule(attrs);
    ASSERT_FALSE(mir.functions.empty());
    EXPECT_FALSE(verifyFlowModule(mir).has_value());
}

TEST(FlowVerify, RejectsOutOfRangeEdge) {
    AttrPool attrs;
    MirModule mir = sampleModule(attrs);
    ControlFlowGraph& cfg = mir.functions[0].cfg;
    ASSERT_FALSE(cfg.blocks[cfg.entry].successors.empty());
    cfg.blocks[cfg.entry].successors[0].target = cfg.blocks.size() + 5;
    const auto violation = verifyFlowModule(mir);
    ASSERT_TRUE(violation.has_value());
    EXPECT_NE(violation->find("out of range"), std::string::npos);
}

TEST(FlowVerify, RejectsTerminatorInstructionInsideElements) {
    AttrPool attrs;
    MirModule mir = sampleModule(attrs);
    ControlFlowGraph& cfg = mir.functions[0].cfg;
    cfg.blocks[cfg.entry].elements.push_back(MirInstruction{.kind = MirOp::Terminator, .site = nullptr});
    const auto violation = verifyFlowModule(mir);
    ASSERT_TRUE(violation.has_value());
    EXPECT_NE(violation->find("Terminator must not appear"), std::string::npos);
}

TEST(FlowVerify, RejectsContractElementNotBackedByTrustContract) {
    AttrPool attrs;
    MirModule mir = sampleModule(attrs);
    ControlFlowGraph& cfg = mir.functions[0].cfg;
    auto site = literal("1");
    cfg.blocks[cfg.entry].elements.push_back(MirInstruction{.kind = MirOp::Assert, .site = site.get()});
    const auto violation = verifyFlowModule(mir);
    ASSERT_TRUE(violation.has_value());
    EXPECT_NE(violation->find("not backed by a TrustContract"), std::string::npos);
}

TEST(FlowVerify, RejectsInvariantWithoutAttachedLoopNode) {
    AttrPool attrs;
    MirModule mir = sampleModule(attrs);
    ControlFlowGraph& cfg = mir.functions[0].cfg;
    auto contract = std::make_shared<TrustContract>();
    contract->kind = PropertyKind::Invariant;
    cfg.blocks[cfg.entry].elements.push_back(MirInstruction{.kind = MirOp::Invariant, .site = contract.get()});
    const auto violation = verifyFlowModule(mir);
    ASSERT_TRUE(violation.has_value());
    EXPECT_NE(violation->find("Invariant without an attached loop node"), std::string::npos);
}

TEST(FlowVerify, RejectsAssertCarryingLoopNode) {
    AttrPool attrs;
    MirModule mir = sampleModule(attrs);
    ControlFlowGraph& cfg = mir.functions[0].cfg;
    auto contract = std::make_shared<TrustContract>();
    contract->kind = PropertyKind::Assert;
    auto loop = std::make_shared<WhileStmt>();
    cfg.blocks[cfg.entry].elements.push_back(MirInstruction{.kind = MirOp::Assert, .site = contract.get(), .loopNode = loop.get()});
    const auto violation = verifyFlowModule(mir);
    ASSERT_TRUE(violation.has_value());
    EXPECT_NE(violation->find("Assert must not carry a loop node"), std::string::npos);
}

TEST(FlowVerify, RejectsRecoverableInconsistencyOfUnreachableFlag) {
    AttrPool attrs;
    MirModule mir = sampleModule(attrs);
    ControlFlowGraph& cfg = mir.functions[0].cfg;
    cfg.blocks[cfg.entry].unreachable = true;
    const auto violation = verifyFlowModule(mir);
    ASSERT_TRUE(violation.has_value());
    EXPECT_NE(violation->find("unreachable flag"), std::string::npos);
}

} // namespace
} // namespace trust::flow
