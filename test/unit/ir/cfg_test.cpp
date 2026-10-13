// test/unit/ir/cfg_test.cpp
// Форма CFG поверх HIR: ветвление, циклы, break/continue, goto-метки (named break/continue),
// match, with, try/catch, недостижимый код, ScopeExit на путях выхода.
//
// Вход CFG — понижённая форма: именованные переходы представлены GotoStmt/LabelStmt,
// выражения-операторы могут быть обёрнуты SemicolonStmt. Само lowering проверяется
// отдельно (semantic/hir тесты).

#include "ir/cfg.hpp"
#include "ast/ast_nodes.hpp"
#include "ast/ident_name.hpp"
#include "ast/token_type.hpp"
#include "attrs/attr_pool.hpp"

#include <gtest/gtest.h>

#include <memory>
#include <optional>
#include <stdexcept>
#include <string>
#include <utility>
#include <vector>

namespace trust::flow {
namespace {

using trust::AstNodePtr;
using trust::FuncDecl;
using trust::IdentName;
using trust::IdentType;
using trust::JumpStmt;
using trust::Literal;
using trust::ParserToken;
using trust::ScopeBlock;
using trust::VarDecl;

AstNodePtr lit(std::string text = "1") {
    return std::make_shared<Literal>(ParserToken::Kind::IntLiteral, std::move(text));
}

AstNodePtr var(std::string name, AstNodePtr init = nullptr) {
    return std::make_shared<VarDecl>(std::move(name), nullptr, std::move(init));
}

AstNodePtr block(std::vector<AstNodePtr> body, std::string label = {}) {
    auto node = std::make_shared<ScopeBlock>(std::move(label));
    node->m_body = std::move(body);
    return node;
}

AstNodePtr ident(std::string name) {
    return std::make_shared<IdentName>(std::move(name));
}

AstNodePtr identOf(std::string name, const AstNodePtr& decl) {
    auto id = std::make_shared<IdentName>(std::move(name));
    id->resolvedDecl = decl.get();
    return id;
}

std::optional<std::size_t> blockOfRead(const ControlFlowGraph& cfg, const AstNodeBase* place) {
    for (const FlowBlock& b : cfg.blocks) {
        for (const MirInstruction& element : b.elements) {
            if (element.kind == MirOp::Read && element.place == place) {
                return b.id;
            }
        }
    }
    return std::nullopt;
}

template <class T>
std::shared_ptr<T> kinded(ParserToken::Kind kind) {
    auto node = std::make_shared<T>();
    node->setKind(kind);
    return node;
}

std::shared_ptr<FuncDecl> func(std::vector<AstNodePtr> body) {
    auto f = std::make_shared<FuncDecl>("f");
    f->m_body = std::move(body);
    return f;
}

std::shared_ptr<JumpStmt> jump(ParserToken::Kind kind, std::string label = {}) {
    auto j = std::make_shared<JumpStmt>(kind);
    if (!label.empty()) {
        j->m_label = ident(std::move(label));
    }
    return j;
}

/// HIR: выражение в позиции оператора (явная точка с запятой).
AstNodePtr stmtOf(AstNodePtr expr) { return std::make_shared<SemicolonStmt>(std::move(expr)); }

/// HIR: goto-метка (named break/continue уже понижены lowering'ом).
AstNodePtr gotoStmt(std::string name) { return std::make_shared<LabelRef>(ParserToken::Kind::GotoStmt, std::move(name)); }

/// HIR: метка-цель перехода.
AstNodePtr labelStmt(std::string name) { return std::make_shared<LabelRef>(ParserToken::Kind::LabelStmt, std::move(name)); }

std::optional<std::size_t> targetOf(const ControlFlowGraph& cfg, std::size_t block, EdgeKind kind) {
    for (const FlowEdge& e : cfg.blocks[block].successors) {
        if (e.kind == kind) {
            return e.target;
        }
    }
    return std::nullopt;
}

ControlFlowGraph buildCfgOf(const FuncDecl& func) {
    static const trust::AttrPool pool;
    return buildCfg(func, pool);
}

std::optional<std::size_t> blockOfPlace(const ControlFlowGraph& cfg, const AstNodeBase* place) {
    for (const FlowBlock& b : cfg.blocks) {
        for (const MirInstruction& element : b.elements) {
            if ((element.kind == MirOp::Declare || element.kind == MirOp::Write) && element.place == place) {
                return b.id;
            }
        }
    }
    return std::nullopt;
}

std::optional<std::size_t> blockOfTerminator(const ControlFlowGraph& cfg, const AstNodeBase* node) {
    for (const FlowBlock& b : cfg.blocks) {
        if (b.terminatorNode.site == node) {
            return b.id;
        }
    }
    return std::nullopt;
}

TEST(Cfg, IfElseBranchesJoin) {
    auto cond = ident("c");
    auto thenNode = var("a", lit());
    auto elseNode = var("b", lit());
    auto ifs = kinded<IfStmt>(ParserToken::Kind::IfStmt);
    ifs->m_cond = cond;
    ifs->m_body = block({thenNode});
    ifs->m_else = block({elseNode});

    const ControlFlowGraph cfg = buildCfgOf(*func({ifs}));
    const std::size_t condBlock = *blockOfTerminator(cfg, ifs.get());
    EXPECT_EQ(cfg.blocks[condBlock].terminator, TerminatorKind::Conditional);

    const auto trueTarget = targetOf(cfg, condBlock, EdgeKind::True);
    const auto falseTarget = targetOf(cfg, condBlock, EdgeKind::False);
    ASSERT_TRUE(trueTarget);
    ASSERT_TRUE(falseTarget);
    EXPECT_EQ(*trueTarget, *blockOfPlace(cfg, thenNode.get()));
    EXPECT_EQ(*falseTarget, *blockOfPlace(cfg, elseNode.get()));

    const auto thenJoin = targetOf(cfg, *trueTarget, EdgeKind::Normal);
    const auto elseJoin = targetOf(cfg, *falseTarget, EdgeKind::Normal);
    ASSERT_TRUE(thenJoin);
    ASSERT_TRUE(elseJoin);
    EXPECT_EQ(*thenJoin, *elseJoin);
    EXPECT_FALSE(cfg.blocks[*thenJoin].unreachable);
}

TEST(Cfg, StatementAfterReturnIsUnreachable) {
    auto ret = jump(ParserToken::Kind::ReturnStmt);
    auto dead = var("dead", lit());

    const ControlFlowGraph cfg = buildCfgOf(*func({ret, dead}));
    const std::size_t retBlock = *blockOfTerminator(cfg, ret.get());
    EXPECT_EQ(cfg.blocks[retBlock].terminator, TerminatorKind::Return);
    EXPECT_EQ(targetOf(cfg, retBlock, EdgeKind::Return), cfg.exit);

    const std::size_t deadBlock = *blockOfPlace(cfg, dead.get());
    EXPECT_TRUE(cfg.blocks[deadBlock].unreachable);
}

TEST(Cfg, WhileLoopBackEdgeAndExit) {
    auto cond = ident("c");
    auto bodyNode = var("x", lit());
    auto ws = kinded<WhileStmt>(ParserToken::Kind::WhileStmt);
    ws->m_cond = cond;
    ws->m_body = block({bodyNode});

    const ControlFlowGraph cfg = buildCfgOf(*func({ws}));
    const std::size_t condBlock = *blockOfTerminator(cfg, ws.get());
    const std::size_t bodyBlock = *blockOfPlace(cfg, bodyNode.get());

    EXPECT_EQ(targetOf(cfg, condBlock, EdgeKind::True), bodyBlock);
    const auto loopExit = targetOf(cfg, condBlock, EdgeKind::False);
    ASSERT_TRUE(loopExit);
    EXPECT_EQ(targetOf(cfg, *loopExit, EdgeKind::Normal), cfg.exit);
    EXPECT_EQ(targetOf(cfg, bodyBlock, EdgeKind::Normal), condBlock);
}

TEST(Cfg, DoWhileExecutesBodyFirst) {
    auto cond = ident("c");
    auto bodyNode = var("x", lit());
    auto ds = kinded<DoWhileStmt>(ParserToken::Kind::DoWhileStmt);
    ds->m_cond = cond;
    ds->m_body = block({bodyNode});

    const ControlFlowGraph cfg = buildCfgOf(*func({ds}));
    const std::size_t condBlock = *blockOfTerminator(cfg, ds.get());
    const std::size_t bodyBlock = *blockOfPlace(cfg, bodyNode.get());

    EXPECT_EQ(targetOf(cfg, condBlock, EdgeKind::True), bodyBlock);
    const auto loopExit = targetOf(cfg, condBlock, EdgeKind::False);
    ASSERT_TRUE(loopExit);
    EXPECT_EQ(targetOf(cfg, *loopExit, EdgeKind::Normal), cfg.exit);
    EXPECT_EQ(targetOf(cfg, bodyBlock, EdgeKind::Normal), condBlock);
}

TEST(Cfg, BreakAndContinueTargetsLoop) {
    auto cond = ident("c");
    auto br = jump(ParserToken::Kind::BreakStmt);
    auto cont = jump(ParserToken::Kind::ContinueStmt);
    auto ws = kinded<WhileStmt>(ParserToken::Kind::WhileStmt);
    ws->m_cond = cond;
    ws->m_body = block({br, cont});

    const ControlFlowGraph cfg = buildCfgOf(*func({ws}));
    const std::size_t condBlock = *blockOfTerminator(cfg, ws.get());
    const std::size_t exitBlock = *targetOf(cfg, condBlock, EdgeKind::False);

    const std::size_t breakBlock = *blockOfTerminator(cfg, br.get());
    EXPECT_EQ(cfg.blocks[breakBlock].terminator, TerminatorKind::Break);
    EXPECT_EQ(targetOf(cfg, breakBlock, EdgeKind::Break), exitBlock);

    const std::size_t continueBlock = *blockOfTerminator(cfg, cont.get());
    EXPECT_EQ(cfg.blocks[continueBlock].terminator, TerminatorKind::Continue);
    EXPECT_EQ(targetOf(cfg, continueBlock, EdgeKind::Continue), condBlock);
}

TEST(Cfg, NamedBlockBreakGotoTargetsBlockExit) {
    auto cond = ident("c");
    auto br = gotoStmt("outer_break");
    auto ws = kinded<WhileStmt>(ParserToken::Kind::WhileStmt);
    ws->m_cond = cond;
    ws->m_body = block({br});

    // HIR именованного блока: тело + метка `_break` в конце.
    auto outer = block({ws, labelStmt("outer_break")}, "outer");
    auto after = var("after", lit());

    const ControlFlowGraph cfg = buildCfgOf(*func({outer, after}));
    const std::size_t gotoBlock = *blockOfTerminator(cfg, br.get());
    EXPECT_EQ(cfg.blocks[gotoBlock].terminator, TerminatorKind::Goto);

    const auto target = targetOf(cfg, gotoBlock, EdgeKind::Break);
    ASSERT_TRUE(target);
    // Метка стоит ВНУТРИ тела блока: ScopeExit самого блока выполняется на этом пути.
    bool hasBlockExit = false;
    for (const MirInstruction& e : cfg.blocks[*target].elements) {
        hasBlockExit = hasBlockExit || (e.kind == MirOp::ScopeExit && e.scope == outer.get());
    }
    EXPECT_TRUE(hasBlockExit);
    // После метки выполнение продолжается в том же блоке: следующая инструкция — в нём же.
    EXPECT_EQ(*target, *blockOfPlace(cfg, after.get()));
}

TEST(Cfg, NamedBlockContinueGotoTargetsFirstLoop) {
    auto cond = ident("c");
    auto cont = gotoStmt("outer_continue");
    auto ws = kinded<WhileStmt>(ParserToken::Kind::WhileStmt);
    ws->m_cond = cond;
    ws->m_body = block({cont});

    // HIR: continue-метка ставится ПЕРЕД первым циклом тела именованного блока.
    auto outer = block({labelStmt("outer_continue"), ws}, "outer");

    const ControlFlowGraph cfg = buildCfgOf(*func({outer}));
    const std::size_t condBlock = *blockOfTerminator(cfg, ws.get());
    const std::size_t gotoBlock = *blockOfTerminator(cfg, cont.get());

    // goto ведёт на метку (блок входа цикла), оттуда — обычный переход к условию.
    const auto labelBlock = targetOf(cfg, gotoBlock, EdgeKind::Continue);
    ASSERT_TRUE(labelBlock);
    EXPECT_EQ(targetOf(cfg, *labelBlock, EdgeKind::Normal), condBlock);
}

TEST(Cfg, GotoEmitsScopeExitForExitedScopesOnly) {
    auto br = gotoStmt("outer_break");
    auto inner = block({br}, "inner");
    auto outer = block({inner, labelStmt("outer_break")}, "outer");

    const ControlFlowGraph cfg = buildCfgOf(*func({outer}));
    const std::size_t gotoBlock = *blockOfTerminator(cfg, br.get());

    std::vector<const AstNodeBase*> exited;
    for (const MirInstruction& e : cfg.blocks[gotoBlock].elements) {
        if (e.kind == MirOp::ScopeExit) {
            exited.push_back(e.scope);
        }
    }
    // Переход выходит только из вложенного блока; сам outer остаётся (метка внутри него).
    ASSERT_EQ(exited.size(), 1u);
    EXPECT_EQ(exited[0], inner.get());
}

TEST(Cfg, SemicolonStmtIsTransparent) {
    auto x = var("x", lit());
    auto read = identOf("x", x);

    const ControlFlowGraph cfg = buildCfgOf(*func({x, stmtOf(read)}));
    ASSERT_TRUE(blockOfRead(cfg, x.get()));
}

TEST(Cfg, MatchHasSwitchTerminatorAndJoin) {
    auto value = ident("v");
    auto caseBodyA = var("a", lit());
    auto caseBodyB = var("b", lit());
    auto defaultBody = var("d", lit());

    auto ms = kinded<MatchStmt>(ParserToken::Kind::MatchingStmt);
    ms->m_value = value;
    MatchStmt::MatchCase c1;
    c1.patterns.push_back(lit("1"));
    c1.body = block({caseBodyA});
    MatchStmt::MatchCase c2;
    c2.patterns.push_back(lit("2"));
    c2.body = block({caseBodyB});
    ms->m_cases.push_back(std::move(c1));
    ms->m_cases.push_back(std::move(c2));
    ms->m_default = block({defaultBody});

    const ControlFlowGraph cfg = buildCfgOf(*func({ms}));
    const std::size_t switchBlock = *blockOfTerminator(cfg, ms.get());
    EXPECT_EQ(cfg.blocks[switchBlock].terminator, TerminatorKind::Switch);

    std::vector<std::size_t> joins;
    for (const FlowEdge& e : cfg.blocks[switchBlock].successors) {
        EXPECT_EQ(e.kind, EdgeKind::Normal);
    }
    for (const AstNodeBase* body : {caseBodyA.get(), caseBodyB.get(), defaultBody.get()}) {
        const std::size_t b = *blockOfPlace(cfg, body);
        EXPECT_FALSE(cfg.blocks[b].unreachable);
        const auto join = targetOf(cfg, b, EdgeKind::Normal);
        ASSERT_TRUE(join);
        joins.push_back(*join);
    }
    ASSERT_EQ(joins.size(), 3u);
    EXPECT_EQ(joins[0], joins[1]);
    EXPECT_EQ(joins[1], joins[2]);
}

TEST(Cfg, TryCatchHasExceptionPath) {
    auto thrown = jump(ParserToken::Kind::ThrowStmt);
    thrown->m_value = lit("1");

    auto cb = kinded<CatchBlock>(ParserToken::Kind::CatchBlock);
    cb->m_binding = std::make_shared<VarDecl>("e", std::make_shared<IdentType>("Int32"), nullptr);
    auto catchBody = var("a", lit());
    cb->m_body.push_back(catchBody);

    auto ts = kinded<TryCatchStmt>(ParserToken::Kind::TryCatchStmt);
    ts->m_body.push_back(thrown);
    ts->m_catches.push_back(cb);

    const ControlFlowGraph cfg = buildCfgOf(*func({ts}));
    const std::size_t throwBlock = *blockOfTerminator(cfg, thrown.get());
    EXPECT_EQ(cfg.blocks[throwBlock].terminator, TerminatorKind::Throw);

    const auto catchDispatch = targetOf(cfg, throwBlock, EdgeKind::Throw);
    ASSERT_TRUE(catchDispatch);
    EXPECT_EQ(cfg.blocks[*catchDispatch].terminator, TerminatorKind::Dispatch);

    const std::size_t catchEntry = *blockOfPlace(cfg, cb->m_binding.get());
    EXPECT_EQ(targetOf(cfg, *catchDispatch, EdgeKind::Exception), catchEntry);
    EXPECT_FALSE(cfg.blocks[catchEntry].unreachable);
    EXPECT_FALSE(cfg.blocks[*blockOfPlace(cfg, catchBody.get())].unreachable);
}

TEST(Cfg, WithElseHasExceptionPath) {
    auto binding = var("a", lit());
    auto bodyNode = var("b", lit());
    auto elseNode = var("c", lit());

    auto ws = kinded<WithStmt>(ParserToken::Kind::WithStmt);
    ws->m_locks.push_back({nullptr, binding});
    ws->m_body = block({bodyNode});
    ws->m_else = block({elseNode});

    const ControlFlowGraph cfg = buildCfgOf(*func({ws}));
    const std::size_t dispatch = *blockOfTerminator(cfg, ws.get());
    EXPECT_EQ(cfg.blocks[dispatch].terminator, TerminatorKind::Dispatch);

    const auto normal = targetOf(cfg, dispatch, EdgeKind::Normal);
    const auto exception = targetOf(cfg, dispatch, EdgeKind::Exception);
    ASSERT_TRUE(normal);
    ASSERT_TRUE(exception);
    EXPECT_EQ(*normal, *blockOfPlace(cfg, bodyNode.get()));
    EXPECT_EQ(*exception, *blockOfPlace(cfg, elseNode.get()));
}

TEST(Cfg, GotoWithoutTargetLabelIsRejected) {
    auto dangling = gotoStmt("x");
    EXPECT_THROW(buildCfgOf(*func({dangling})), std::runtime_error);
}

TEST(Cfg, DuplicateGotoLabelIsRejected) {
    // HIR обязан иметь ровно одну метку на имя (lowering гарантирует уникальность в функции).
    auto first = labelStmt("dup_break");
    auto second = labelStmt("dup_break");
    EXPECT_THROW(buildCfgOf(*func({first, second})), std::runtime_error);
}

TEST(Cfg, LabeledJumpIsRejectedInHir) {
    // В HIR именованные break/continue уже понижены в goto: JumpStmt с меткой — нарушение формы.
    auto labeledBreak = jump(ParserToken::Kind::BreakStmt, "outer");
    auto ws = kinded<WhileStmt>(ParserToken::Kind::WhileStmt);
    ws->m_cond = ident("c");
    ws->m_body = block({labeledBreak});
    EXPECT_THROW(buildCfgOf(*func({ws})), std::runtime_error);
}

TEST(Cfg, DumpContainsBlocksAndEdges) {
    auto ws = kinded<WhileStmt>(ParserToken::Kind::WhileStmt);
    ws->m_cond = ident("c");
    ws->m_body = block({});

    const std::string dump = dumpCfg(buildCfgOf(*func({ws})));
    EXPECT_NE(dump.find("block 0:"), std::string::npos);
    EXPECT_NE(dump.find("term "), std::string::npos);
    EXPECT_NE(dump.find("-> "), std::string::npos);
}

/// Вложенные именованные блоки с циклами (HIR-форма):
/// outer { <outer_continue>; while(a) { inner { <inner_continue>; while(b) { goto <target>_continue; } } } }
struct NestedNamedLoops {
    AstNodePtr cont;
    std::shared_ptr<WhileStmt> outerWhile;
    std::shared_ptr<WhileStmt> innerWhile;
    std::shared_ptr<FuncDecl> fn;
};

NestedNamedLoops nestedNamedLoops(std::string target) {
    auto cont = gotoStmt(std::move(target) + "_continue");
    auto innerWhile = kinded<WhileStmt>(ParserToken::Kind::WhileStmt);
    innerWhile->m_cond = ident("b");
    innerWhile->m_body = block({cont});

    auto inner = block({labelStmt("inner_continue"), innerWhile}, "inner");

    auto outerWhile = kinded<WhileStmt>(ParserToken::Kind::WhileStmt);
    outerWhile->m_cond = ident("a");
    outerWhile->m_body = block({inner});

    auto outer = block({labelStmt("outer_continue"), outerWhile}, "outer");
    return NestedNamedLoops{.cont = cont, .outerWhile = outerWhile, .innerWhile = innerWhile, .fn = func({outer})};
}

TEST(Cfg, NamedBlockOuterContinueJumpsToOuterLoop) {
    NestedNamedLoops nodes = nestedNamedLoops("outer");
    const ControlFlowGraph cfg = buildCfgOf(*nodes.fn);
    const std::size_t contBlock = *blockOfTerminator(cfg, nodes.cont.get());
    const std::size_t outerCond = *blockOfTerminator(cfg, nodes.outerWhile.get());
    // goto → метка `outer_continue` → обычный переход к условию внешнего цикла.
    const auto labelBlock = targetOf(cfg, contBlock, EdgeKind::Continue);
    ASSERT_TRUE(labelBlock);
    EXPECT_EQ(targetOf(cfg, *labelBlock, EdgeKind::Normal), outerCond);
}

TEST(Cfg, NamedBlockInnerContinueJumpsToInnerLoop) {
    NestedNamedLoops nodes = nestedNamedLoops("inner");
    const ControlFlowGraph cfg = buildCfgOf(*nodes.fn);
    const std::size_t contBlock = *blockOfTerminator(cfg, nodes.cont.get());
    const std::size_t innerCond = *blockOfTerminator(cfg, nodes.innerWhile.get());
    const auto labelBlock = targetOf(cfg, contBlock, EdgeKind::Continue);
    ASSERT_TRUE(labelBlock);
    EXPECT_EQ(targetOf(cfg, *labelBlock, EdgeKind::Normal), innerCond);
}

TEST(Cfg, InstructionsFollowSemanticOrder) {
    auto a = var("a", lit());
    auto b = var("b", lit());
    auto c = var("c", lit());

    auto innerAssign = kinded<Binary>(ParserToken::Kind::AssignOp);
    innerAssign->m_op = trust::BinaryOp::PlainAssign;
    innerAssign->m_left = identOf("c", c);
    innerAssign->m_right = lit("1");

    auto sum = kinded<Binary>(ParserToken::Kind::MathOp);
    sum->m_op = trust::BinaryOp::Add;
    sum->m_left = identOf("b", b);
    sum->m_right = innerAssign;

    auto outer = kinded<Binary>(ParserToken::Kind::AssignOp);
    outer->m_op = trust::BinaryOp::PlainAssign;
    outer->m_left = identOf("a", a);
    outer->m_right = sum;

    const ControlFlowGraph cfg = buildCfgOf(*func({outer}));
    std::vector<MirInstruction> instructions;
    for (const FlowBlock& blk : cfg.blocks) {
        if (blk.unreachable) {
            continue;
        }
        for (const MirInstruction& e : blk.elements) {
            if (e.kind != MirOp::ScopeExit) {
                instructions.push_back(e);
            }
        }
    }
    ASSERT_EQ(instructions.size(), 3u);
    EXPECT_EQ(instructions[0].kind, MirOp::Read);
    EXPECT_EQ(instructions[0].place, b.get());
    EXPECT_EQ(instructions[1].kind, MirOp::Write);
    EXPECT_EQ(instructions[1].place, c.get());
    EXPECT_EQ(instructions[2].kind, MirOp::Write);
    EXPECT_EQ(instructions[2].place, a.get());
}

TEST(Cfg, IndexedAssignmentReadsBaseAndIndexBeforeRhs) {
    auto x = var("x", lit());
    auto i = var("i", lit());
    auto y = var("y", lit());

    auto inc = kinded<Binary>(ParserToken::Kind::IncDecOp);
    inc->m_op = trust::BinaryOp::PostIncrement;
    inc->m_left = identOf("i", i);

    auto index = kinded<Binary>(ParserToken::Kind::ArrayAccess);
    index->m_left = identOf("x", x);
    index->m_right = inc;

    auto assign = kinded<Binary>(ParserToken::Kind::AssignOp);
    assign->m_op = trust::BinaryOp::PlainAssign;
    assign->m_left = index;
    assign->m_right = identOf("y", y);

    const ControlFlowGraph cfg = buildCfgOf(*func({assign}));
    std::vector<MirInstruction> instructions;
    for (const FlowBlock& blk : cfg.blocks) {
        if (blk.unreachable) {
            continue;
        }
        for (const MirInstruction& e : blk.elements) {
            if (e.kind == MirOp::Read || e.kind == MirOp::Write) {
                instructions.push_back(e);
            }
        }
    }
    ASSERT_EQ(instructions.size(), 5u);
    EXPECT_EQ(instructions[0].kind, MirOp::Read);
    EXPECT_EQ(instructions[0].place, x.get());
    EXPECT_EQ(instructions[1].kind, MirOp::Read);
    EXPECT_EQ(instructions[1].place, i.get());
    EXPECT_EQ(instructions[2].kind, MirOp::Write);
    EXPECT_EQ(instructions[2].place, i.get());
    EXPECT_EQ(instructions[3].kind, MirOp::Read);
    EXPECT_EQ(instructions[3].place, y.get());
    // Запись в элемент адресуется КОРНЮ (x), projection - сам lvalue-узел.
    EXPECT_EQ(instructions[4].kind, MirOp::Write);
    EXPECT_EQ(instructions[4].place, x.get());
    EXPECT_EQ(instructions[4].projection, index.get());
}

TEST(Cfg, CallArgumentsAreReadInOrder) {
    auto x = var("x", lit());
    auto y = var("y", lit());

    auto leftCall = kinded<CallExpr>(ParserToken::Kind::CallExpr);
    leftCall->m_callee = ident("f");
    leftCall->m_args = std::vector<AstNodePtr>{identOf("x", x)};
    auto rightCall = kinded<CallExpr>(ParserToken::Kind::CallExpr);
    rightCall->m_callee = ident("g");
    rightCall->m_args = std::vector<AstNodePtr>{identOf("y", y)};

    auto sum = kinded<Binary>(ParserToken::Kind::MathOp);
    sum->m_op = trust::BinaryOp::Add;
    sum->m_left = leftCall;
    sum->m_right = rightCall;

    const ControlFlowGraph cfg = buildCfgOf(*func({sum}));
    const std::size_t xBlock = *blockOfRead(cfg, x.get());
    const std::size_t yBlock = *blockOfRead(cfg, y.get());
    EXPECT_EQ(xBlock, yBlock);
    const auto& elements = cfg.blocks[xBlock].elements;
    ASSERT_EQ(elements.size(), 4u);
    EXPECT_EQ(elements[0].kind, MirOp::Read);
    EXPECT_EQ(elements[0].place, x.get());
    EXPECT_EQ(elements[1].kind, MirOp::Call);
    EXPECT_EQ(elements[2].kind, MirOp::Read);
    EXPECT_EQ(elements[2].place, y.get());
    EXPECT_EQ(elements[3].kind, MirOp::Call);
}

TEST(Cfg, ShortCircuitSplitsBlocks) {
    auto condDecl = var("cond", lit());
    auto valueDecl = var("value", lit());

    auto andNode = kinded<Binary>(ParserToken::Kind::LogicalOp);
    andNode->m_op = trust::BinaryOp::LogicalAnd;
    andNode->m_left = identOf("cond", condDecl);
    andNode->m_right = identOf("value", valueDecl);

    const ControlFlowGraph cfg = buildCfgOf(*func({andNode}));
    const std::size_t condBlock = *blockOfRead(cfg, condDecl.get());
    const std::size_t valueBlock = *blockOfRead(cfg, valueDecl.get());
    EXPECT_NE(condBlock, valueBlock);
    EXPECT_EQ(targetOf(cfg, condBlock, EdgeKind::True), valueBlock);

    const auto join = targetOf(cfg, condBlock, EdgeKind::False);
    ASSERT_TRUE(join);
    EXPECT_EQ(targetOf(cfg, valueBlock, EdgeKind::Normal), *join);
}

TEST(Cfg, ScopeExitIsRecorded) {
    auto outer = block({var("a", lit())}, "outer");
    const ControlFlowGraph cfg = buildCfgOf(*func({outer}));
    bool found = false;
    for (const FlowBlock& b : cfg.blocks) {
        for (const MirInstruction& e : b.elements) {
            found = found || e.kind == MirOp::ScopeExit;
        }
    }
    EXPECT_TRUE(found);
}

TEST(Cfg, GenericElementGraphIsAstIndependent) {
    CfgT<int> cfg;
    cfg.entry = 0;
    cfg.exit = 1;
    cfg.blocks.push_back(BasicBlockT<int>{
        .id = 0, .elements = {7, 8}, .successors = {FlowEdge{.kind = EdgeKind::True, .target = 1}, FlowEdge{.kind = EdgeKind::False, .target = 1}}});
    cfg.blocks.push_back(BasicBlockT<int>{.id = 1});

    ASSERT_EQ(cfg.blocks.size(), 2u);
    EXPECT_EQ(cfg.block(0).elements.size(), 2u);
    EXPECT_EQ(cfg.block(0).elements.front(), 7);
    EXPECT_EQ(cfg.block(0).successors.size(), 2u);
    EXPECT_EQ(cfg.block(0).successors.front().target, 1u);
}

TEST(Cfg, ScopeExitOnReturnEmitsNestedScopesLifo) {
    auto ret = jump(ParserToken::Kind::ReturnStmt);
    auto innerNode = block({ret}, "inner");
    auto outerNode = block({innerNode}, "outer");

    const ControlFlowGraph cfg = buildCfgOf(*func({outerNode}));
    const std::size_t retBlock = *blockOfTerminator(cfg, ret.get());
    std::vector<const AstNodeBase*> exited;
    for (const MirInstruction& e : cfg.blocks[retBlock].elements) {
        if (e.kind == MirOp::ScopeExit) {
            exited.push_back(e.scope);
        }
    }
    ASSERT_EQ(exited.size(), 2u);
    EXPECT_EQ(exited[0], innerNode.get());
    EXPECT_EQ(exited[1], outerNode.get());
}

TEST(Cfg, ScopeExitOnBreakAndContinueExitsLoopBody) {
    auto br = jump(ParserToken::Kind::BreakStmt);
    auto bodyNode = block({br});
    auto ws = kinded<WhileStmt>(ParserToken::Kind::WhileStmt);
    ws->m_cond = ident("c");
    ws->m_body = bodyNode;

    const ControlFlowGraph cfg = buildCfgOf(*func({ws}));
    const std::size_t breakBlock = *blockOfTerminator(cfg, br.get());
    bool found = false;
    for (const MirInstruction& e : cfg.blocks[breakBlock].elements) {
        if (e.kind == MirOp::ScopeExit && e.scope == bodyNode.get()) {
            found = true;
        }
    }
    EXPECT_TRUE(found);

    auto cont = jump(ParserToken::Kind::ContinueStmt);
    auto bodyNode2 = block({cont});
    auto ws2 = kinded<WhileStmt>(ParserToken::Kind::WhileStmt);
    ws2->m_cond = ident("c");
    ws2->m_body = bodyNode2;

    const ControlFlowGraph cfg2 = buildCfgOf(*func({ws2}));
    const std::size_t contBlock = *blockOfTerminator(cfg2, cont.get());
    bool found2 = false;
    for (const MirInstruction& e : cfg2.blocks[contBlock].elements) {
        if (e.kind == MirOp::ScopeExit && e.scope == bodyNode2.get()) {
            found2 = true;
        }
    }
    EXPECT_TRUE(found2);
}

TEST(Cfg, ResetEmitsMoveInsteadOfWrite) {
    auto x = var("x", lit());
    auto reset = kinded<Binary>(ParserToken::Kind::AssignOp);
    reset->m_op = trust::BinaryOp::PlainAssign;
    reset->m_left = identOf("x", x);

    const ControlFlowGraph cfg = buildCfgOf(*func({reset}));
    bool moved = false;
    for (const FlowBlock& blk : cfg.blocks) {
        if (blk.unreachable) {
            continue;
        }
        for (const MirInstruction& e : blk.elements) {
            if (e.kind == MirOp::Move && e.place == x.get()) {
                moved = true;
            }
            EXPECT_FALSE(e.kind == MirOp::Write && e.place == x.get());
        }
    }
    EXPECT_TRUE(moved);
}

TEST(Cfg, ProjectedWriteTargetsRootPlace) {
    auto o = var("o", lit());
    auto member = kinded<Binary>(ParserToken::Kind::MemberAccess);
    member->m_left = identOf("o", o);
    member->m_right = ident("f");

    auto assign = kinded<Binary>(ParserToken::Kind::AssignOp);
    assign->m_op = trust::BinaryOp::PlainAssign;
    assign->m_left = member;
    assign->m_right = lit();

    const ControlFlowGraph cfg = buildCfgOf(*func({assign}));
    bool found = false;
    for (const FlowBlock& blk : cfg.blocks) {
        if (blk.unreachable) {
            continue;
        }
        for (const MirInstruction& e : blk.elements) {
            if (e.kind == MirOp::Write && e.place == o.get() && e.projection == member.get()) {
                found = true;
            }
        }
    }
    EXPECT_TRUE(found);
}

TEST(Cfg, CallEmitsCallInstructionWithResolvedCallee) {
    auto calleeDecl = var("f");
    auto call = kinded<CallExpr>(ParserToken::Kind::CallExpr);
    call->m_callee = identOf("f", calleeDecl);
    call->m_args = std::vector<AstNodePtr>{};

    const ControlFlowGraph cfg = buildCfgOf(*func({call}));
    bool found = false;
    for (const FlowBlock& blk : cfg.blocks) {
        if (blk.unreachable) {
            continue;
        }
        for (const MirInstruction& e : blk.elements) {
            if (e.kind == MirOp::Call && e.place == calleeDecl.get() && e.site == call.get()) {
                found = true;
            }
        }
    }
    EXPECT_TRUE(found);
}

} // namespace
} // namespace trust::flow
