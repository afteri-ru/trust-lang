// test/unit/ast/slots_test.cpp
// Характеризация топологии AST: состав и ПОРЯДОК детей (children()/collectChildren)
// и владения (клон всех владеемых слотов + ремап невладеющих ссылок).
// Фиксирует наблюдаемый контракт класс-таблицы слотов (ast/slots/node_slots_x.hpp);
// тест не зависит от способа вычисления — он проверяет результат.
#include "ast/ast_nodes.hpp"
#include "ast/clone.hpp"
#include "syntax/term/term.h"

#include <gtest/gtest.h>

#include <string>
#include <utility>
#include <vector>

namespace trust {
namespace {

using K = ParserToken::Kind;

AstNodePtr lit(std::string text = "1") {
    return std::make_shared<Literal>(K::IntLiteral, std::move(text));
}

AstNodePtr ident(std::string name) {
    return std::make_shared<IdentName>(std::move(name));
}

AstNodePtr typeName(std::string name) {
    return std::make_shared<IdentType>(std::move(name));
}

/// Последовательность kind'ов детей в порядке обхода (END — пустой слот, не должно случаться).
std::string childKinds(const AstNodeBase& node) {
    std::string out;
    for (const AstNodePtr& child : node.children()) {
        if (!out.empty()) {
            out += ",";
        }
        out += std::string(child ? ParserToken::name(child->kind()) : "END");
    }
    return out;
}

// -- Обход (children/collectChildren): состав и порядок ------------------------------------

TEST(AstSlots, SequenceTraversesBody) {
    auto node = std::make_shared<Sequence>(K::sequence);
    node->m_body = {lit(), ident("x")};
    EXPECT_EQ(childKinds(*node), "IntLiteral,Ident");
}

TEST(AstSlots, TypeSetIsLeafDespiteSequenceClass) {
    auto node = std::make_shared<Sequence>(K::TypeSet);
    node->m_body = {lit(), lit()};
    EXPECT_EQ(childKinds(*node), "");
}

TEST(AstSlots, BinaryTraversesLeftRightAndIndices) {
    auto left = ident("a");
    auto right = ident("b");
    auto extra = lit("2");
    auto node = std::make_shared<Binary>(K::ArrayAccess, left, right);
    node->m_indices = {right, extra};
    // m_right дублируется первым индексом — второй раз НЕ обходится (дедупликация по узлу).
    EXPECT_EQ(childKinds(*node), "Ident,Ident,IntLiteral");
}

TEST(AstSlots, BinaryTypeDeclSkipsLhsName) {
    auto node = std::make_shared<Binary>(K::TypeDecl, ident("T"), typeName("Int32"));
    EXPECT_EQ(childKinds(*node), "TypeName");
}

TEST(AstSlots, CallExprTraversesCalleeAndArgs) {
    auto node = std::make_shared<CallExpr>(K::CallExpr, ident("f"));
    node->m_args = std::vector<AstNodePtr>{lit(), lit("2")};
    EXPECT_EQ(childKinds(*node), "Ident,IntLiteral,IntLiteral");
}

TEST(AstSlots, ArgNodeTraversesTypeAndValue) {
    auto node = std::make_shared<ArgNode>("a", typeName("Int32"), lit());
    EXPECT_EQ(childKinds(*node), "TypeName,IntLiteral");
}

TEST(AstSlots, VarDeclTraversesTypeAndInitializer) {
    auto node = std::make_shared<VarDecl>("x", typeName("Int32"), lit());
    EXPECT_EQ(childKinds(*node), "TypeName,IntLiteral");
}

TEST(AstSlots, FuncDeclTraversesTypeAliasParamsBody) {
    auto node = std::make_shared<FuncDecl>("f");
    node->m_type = typeName("Int32");
    node->setAlias(AliasKind::Name, ident("g"), MapperRange{});
    node->m_params = std::vector<AstNodePtr>{std::make_shared<ArgNode>("a", typeName("Int32"), nullptr)};
    node->m_definition = Definition::fromBody({lit()});
    // Порядок контракта: тип -> цель алиаса -> параметры -> тело.
    EXPECT_EQ(childKinds(*node), "TypeName,Ident,ArgNode,IntLiteral");
}

TEST(AstSlots, FuncDeclTemplateParamsAndCapturesAreNotTraversed) {
    auto node = std::make_shared<FuncDecl>("f");
    node->m_templateParams = std::vector<AstNodePtr>{std::make_shared<ArgNode>("T")};
    node->m_captures = std::vector<AstNodePtr>{ident("x")};
    EXPECT_EQ(childKinds(*node), "");
}

TEST(AstSlots, IdentTypeTraversesDimsThenParams) {
    auto term = Term::Create(TermID::NAME, "T", {}, parser::token_type::NAME);
    std::optional<std::vector<AstNodePtr>> dims{std::vector<AstNodePtr>{lit("2"), lit("3")}};
    std::optional<std::vector<AstNodePtr>> params{std::vector<AstNodePtr>{typeName("U")}};
    auto node = std::make_shared<IdentType>(term, dims, params);
    EXPECT_EQ(childKinds(*node), "IntLiteral,IntLiteral,TypeName");
}

TEST(AstSlots, IdentTypeTemplateArgsAreNotTraversed) {
    auto node = std::make_shared<IdentType>("vector");
    node->setTemplateArgs({TemplateArg{typeName("Int32"), false}});
    EXPECT_EQ(childKinds(*node), "");
}

TEST(AstSlots, LiteralTypeAnnotationIsNotTraversed) {
    auto node = std::make_shared<Literal>(K::IntLiteral, std::string("1"));
    node->typeAnnotation = typeName("Int16");
    EXPECT_EQ(childKinds(*node), "");
}

TEST(AstSlots, DictLiteralTraversesBodyOnly) {
    auto node = std::make_shared<DictLiteralNode>(K::DictLiteral);
    node->m_body = {lit(), lit("2")};
    node->m_type = typeName("Int32");
    node->arrayElementAnnotation = typeName("Int32");
    EXPECT_EQ(childKinds(*node), "IntLiteral,IntLiteral");
}

TEST(AstSlots, RangeExprTraversesOperandsOnly) {
    auto node = std::make_shared<RangeExpr>(K::RangeExpr, lit("0"), lit("10"));
    node->operandTypes = {typeName("Int32"), typeName("Int32")};
    EXPECT_EQ(childKinds(*node), "IntLiteral,IntLiteral");
}

TEST(AstSlots, JumpStmtTraversesTempThenValue) {
    auto node = std::make_shared<JumpStmt>(K::ReturnStmt);
    node->m_tempDecl = std::make_shared<VarDecl>("__trust_res_0", typeName("Int32"), lit());
    node->m_value = ident("__trust_res_0");
    node->m_label = ident("done");
    EXPECT_EQ(childKinds(*node), "VarDecl,Ident");
}

// -- Управляющие конструкции --------------------------------------------------------------

TEST(AstSlots, IfStmtTraversesCondBodyElseThenElseifs) {
    auto node = std::make_shared<IfStmt>();
    node->setKind(K::IfStmt);
    node->m_cond = ident("c");
    node->m_body = lit("1");
    node->m_else = lit("2");
    node->m_elseifs.push_back({ident("c2"), lit("3")});
    EXPECT_EQ(childKinds(*node), "Ident,IntLiteral,IntLiteral,Ident,IntLiteral");
}

TEST(AstSlots, WhileStmtTraversesCondBodyElseThenElseFlag) {
    auto node = std::make_shared<WhileStmt>();
    node->setKind(K::WhileStmt);
    node->m_cond = ident("c");
    node->m_body = lit("1");
    node->m_else = lit("2");
    node->m_elseFlag = std::make_shared<VarDecl>("_w0", typeName("Bool"), lit("0"));
    EXPECT_EQ(childKinds(*node), "Ident,IntLiteral,IntLiteral,VarDecl");
}

TEST(AstSlots, MatchStmtTraversesTempValueCasesDefault) {
    auto node = std::make_shared<MatchStmt>();
    node->setKind(K::MatchingStmt);
    node->m_tempDecl = std::make_shared<VarDecl>("_match0", typeName("Int32"), lit());
    node->m_value = ident("_match0");
    MatchStmt::MatchCase match_case;
    match_case.patterns = {lit("1"), lit("2")};
    match_case.body = lit("3");
    node->m_cases.push_back(std::move(match_case));
    node->m_default = lit("4");
    EXPECT_EQ(childKinds(*node), "VarDecl,Ident,IntLiteral,IntLiteral,IntLiteral,IntLiteral");
}

TEST(AstSlots, WithStmtTraversesLockPairsBodyElse) {
    auto node = std::make_shared<WithStmt>();
    node->setKind(K::WithStmt);
    node->m_locks.push_back({ident("lock"), std::make_shared<VarDecl>("x", nullptr, lit())});
    node->m_body = lit("1");
    node->m_else = lit("2");
    node->m_failFlag = std::make_shared<VarDecl>("_w0", typeName("Bool"), lit("0"));
    EXPECT_EQ(childKinds(*node), "Ident,VarDecl,IntLiteral,IntLiteral");
}

TEST(AstSlots, TryCatchTraversesBodyCatchesElse) {
    auto node = std::make_shared<TryCatchStmt>();
    node->setKind(K::TryCatchStmt);
    node->m_body = {lit()};
    node->m_catches = {std::make_shared<CatchBlock>(K::CatchBlock)};
    node->m_else = lit("2");
    EXPECT_EQ(childKinds(*node), "IntLiteral,CatchBlock,IntLiteral");
}

TEST(AstSlots, CatchBlockTraversesBodyOnly) {
    auto node = std::make_shared<CatchBlock>(K::CatchBlock);
    node->m_body = {lit()};
    node->m_binding = std::make_shared<VarDecl>("e", typeName("IntAny"), nullptr);
    EXPECT_EQ(childKinds(*node), "IntLiteral");
}

// -- Синтетические узлы и контракты -------------------------------------------------------

TEST(AstSlots, SemicolonStmtTraversesExpr) {
    auto node = std::make_shared<SemicolonStmt>(lit());
    EXPECT_EQ(childKinds(*node), "IntLiteral");
}

TEST(AstSlots, LastResultCaptureTraversesTempSourceSink) {
    auto node = std::make_shared<LastResultCapture>(lit("1"), lit("2"), lit("3"));
    EXPECT_EQ(childKinds(*node), "IntLiteral,IntLiteral,IntLiteral");
}

TEST(AstSlots, TrustContractExprIsNotTraversed) {
    auto node = std::make_shared<TrustContract>(K::TrustContract, lit());
    EXPECT_EQ(childKinds(*node), "");
}

TEST(AstSlots, TrustElemArgsAreNotTraversed) {
    auto node = std::make_shared<TrustElem>(K::TrustElem, Z3TermKind::Old, std::vector<AstNodePtr>{ident("x")});
    EXPECT_EQ(childKinds(*node), "");
}

TEST(AstSlots, ErrorExprOriginalIsNotTraversed) {
    auto node = std::make_shared<ErrorExpr>(lit());
    EXPECT_EQ(childKinds(*node), "");
}

TEST(AstSlots, TrustContractsAttachedToAnyNodeAreNotTraversed) {
    auto node = std::make_shared<VarDecl>("x", typeName("Int32"), lit());
    node->m_trust.push_back(std::make_shared<TrustContract>(K::TrustContract, ident("x")));
    EXPECT_EQ(childKinds(*node), "TypeName,IntLiteral");
}

TEST(AstSlots, ModuleNodePartsBodyIsNotTraversed) {
    auto node = std::make_shared<ModuleNode>();
    node->setKind(K::ModuleDecl);
    ModuleNode::ModulePart part;
    part.body = std::make_shared<std::vector<AstNodePtr>>(std::vector<AstNodePtr>{lit()});
    node->parts().push_back(std::move(part));
    // Тело части модуля — разделяемое (владелец — реестр), в обход узла/клон не входит.
    EXPECT_EQ(childKinds(*node), "");
}

// -- Владение: клон копирует ВСЕ владеемые слоты (в т.ч. скрытые от обхода) ----------------

TEST(AstSlots, CloneCopiesSlotsHiddenFromTraversal) {
    auto func = std::make_shared<FuncDecl>("f");
    func->m_templateParams = std::vector<AstNodePtr>{std::make_shared<ArgNode>("T")};
    func->m_captures = std::vector<AstNodePtr>{ident("x")};

    auto typed = std::make_shared<IdentType>("vector");
    typed->setTemplateArgs({TemplateArg{typeName("Int32"), false}});

    auto dict = std::make_shared<DictLiteralNode>(K::DictLiteral);
    dict->m_type = typeName("Int32");
    dict->arrayElementAnnotation = typeName("Int64");

    auto range = std::make_shared<RangeExpr>(K::RangeExpr, lit("0"), lit("1"));
    range->operandTypes = {typeName("Int32"), typeName("Int32")};

    auto literal = std::make_shared<Literal>(K::IntLiteral, std::string("7"));
    literal->typeAnnotation = typeName("Int16");

    auto elem = std::make_shared<TrustElem>(K::TrustElem, Z3TermKind::Old, std::vector<AstNodePtr>{ident("v")});
    auto error = std::make_shared<ErrorExpr>(lit());
    auto contract = std::make_shared<TrustContract>(K::TrustContract, ident("v"));

    auto body = std::make_shared<Sequence>(K::sequence);
    body->m_body = {typed, dict, range, literal, elem, error, contract};
    func->m_definition = Definition::fromBody({body});

    const std::vector<AstNodePtr> clones = cloneTrees({func});
    ASSERT_EQ(clones.size(), 1u);
    const auto& func_clone = static_cast<const FuncDecl&>(*clones[0]);
    ASSERT_TRUE(func_clone.m_templateParams.has_value());
    EXPECT_NE((*func_clone.m_templateParams)[0].get(), (*func->m_templateParams)[0].get());
    ASSERT_TRUE(func_clone.m_captures.has_value());
    EXPECT_NE((*func_clone.m_captures)[0].get(), (*func->m_captures)[0].get());

    ASSERT_TRUE(func_clone.hasBody());
    const auto& seq_clone = static_cast<const Sequence&>(*(*func_clone.body())[0]);
    ASSERT_EQ(seq_clone.m_body.size(), 7u);

    const auto& typed_clone = static_cast<const IdentType&>(*seq_clone.m_body[0]);
    ASSERT_TRUE(typed_clone.templateArgs().has_value());
    EXPECT_NE((*typed_clone.templateArgs())[0].node.get(), (*typed->templateArgs())[0].node.get());

    const auto& dict_clone = static_cast<const DictLiteralNode&>(*seq_clone.m_body[1]);
    EXPECT_NE(dict_clone.m_type.get(), dict->m_type.get());
    EXPECT_NE(dict_clone.arrayElementAnnotation.get(), dict->arrayElementAnnotation.get());

    const auto& range_clone = static_cast<const RangeExpr&>(*seq_clone.m_body[2]);
    ASSERT_EQ(range_clone.operandTypes.size(), 2u);
    EXPECT_NE(range_clone.operandTypes[0].get(), range->operandTypes[0].get());

    const auto& literal_clone = static_cast<const Literal&>(*seq_clone.m_body[3]);
    EXPECT_NE(literal_clone.typeAnnotation.get(), literal->typeAnnotation.get());

    const auto& elem_clone = static_cast<const TrustElem&>(*seq_clone.m_body[4]);
    ASSERT_EQ(elem_clone.m_args.size(), 1u);
    EXPECT_NE(elem_clone.m_args[0].get(), elem->m_args[0].get());

    const auto& error_clone = static_cast<const ErrorExpr&>(*seq_clone.m_body[5]);
    EXPECT_NE(error_clone.m_original.get(), error->m_original.get());

    const auto& contract_clone = static_cast<const TrustContract&>(*seq_clone.m_body[6]);
    EXPECT_NE(contract_clone.m_expr.get(), contract->m_expr.get());
}

TEST(AstSlots, CloneCopiesTrustContractsOfEveryNode) {
    auto var = std::make_shared<VarDecl>("x", typeName("Int32"), lit());
    var->m_trust.push_back(std::make_shared<TrustContract>(K::TrustContract, ident("x")));

    const std::vector<AstNodePtr> clones = cloneTrees({var});
    ASSERT_EQ(clones.size(), 1u);
    const auto& var_clone = static_cast<const VarDecl&>(*clones[0]);
    ASSERT_EQ(var_clone.m_trust.size(), 1u);
    EXPECT_NE(var_clone.m_trust[0].get(), var->m_trust[0].get());
}

TEST(AstSlots, CloneSharesModulePartBody) {
    auto module = std::make_shared<ModuleNode>();
    module->setKind(K::ModuleDecl);
    ModuleNode::ModulePart part;
    part.body = std::make_shared<std::vector<AstNodePtr>>(std::vector<AstNodePtr>{lit()});
    module->parts().push_back(std::move(part));

    const std::vector<AstNodePtr> clones = cloneTrees({module});
    ASSERT_EQ(clones.size(), 1u);
    const auto& module_clone = static_cast<const ModuleNode&>(*clones[0]);
    ASSERT_EQ(module_clone.parts().size(), 1u);
    // Тело части модуля принадлежит реестру: клон разделяет его, а не копирует.
    EXPECT_EQ(module_clone.parts()[0].body.get(), module->parts()[0].body.get());
}

// -- Невладеющие ссылки: ремап на клоны ----------------------------------------------------

TEST(AstSlots, CloneRemapsLoopInvariantsWhenTargetIsCloned) {
    auto contract = std::make_shared<TrustContract>(K::TrustContract, ident("i"));
    auto body = std::make_shared<Sequence>(K::sequence);
    body->m_body = {contract};

    auto loop = std::make_shared<WhileStmt>();
    loop->setKind(K::WhileStmt);
    loop->m_cond = ident("c");
    loop->m_body = body;
    loop->m_invariants.push_back(contract.get());

    const std::vector<AstNodePtr> clones = cloneTrees({loop});
    ASSERT_EQ(clones.size(), 1u);
    const auto& loop_clone = static_cast<const WhileStmt&>(*clones[0]);
    ASSERT_EQ(loop_clone.m_invariants.size(), 1u);
    ASSERT_TRUE(loop_clone.m_body != nullptr);
    const auto& body_clone = static_cast<const Sequence&>(*loop_clone.m_body);
    ASSERT_EQ(body_clone.m_body.size(), 1u);
    // Инвариант — невладеющая ссылка на узел, входящий в клон: ремап на клон узла.
    EXPECT_EQ(loop_clone.m_invariants[0], body_clone.m_body[0].get());
}

// -- Эквивалентность способов обхода: children() и единая класс-таблица слотов --------------
// Набор узлов покрывает все ФОРМЫ слотов (Ptr/List/OptList/PairList/CaseList/дедуп/скрытые),
// поэтому расхождение таблицы с фактическим составом узла ловится здесь.

namespace {

class CollectingVisitor final : public AstSlotVisitor {
  public:
    std::vector<AstNodePtr*> traversed;
    void owned(AstNodePtr& slot, SlotWalk walk) override {
        if (walk == SlotWalk::Traversed && slot) {
            traversed.push_back(&slot);
        }
    }
    void nonOwning(const AstNodeBase*&) override {}
    void nonOwningFunc(const FuncDecl*&) override {}
};

std::string visitorKinds(const AstNodeBase& node) {
    CollectingVisitor visitor;
    const_cast<AstNodeBase&>(node).forEachSlot(visitor);
    std::string out;
    for (const AstNodePtr* slot : visitor.traversed) {
        if (!out.empty()) {
            out += ",";
        }
        out += std::string(ParserToken::name((*slot)->kind()));
    }
    return out;
}

std::vector<AstNodePtr> nodeSamples() {
    std::vector<AstNodePtr> out;

    auto seq = std::make_shared<Sequence>(K::sequence);
    seq->m_body = {lit(), ident("x")};
    out.push_back(seq);

    auto type_set = std::make_shared<Sequence>(K::TypeSet);
    type_set->m_body = {lit(), lit()};
    out.push_back(type_set);

    auto array_access = std::make_shared<Binary>(K::ArrayAccess, ident("a"), ident("b"));
    array_access->m_indices = {array_access->m_right, lit("2")};
    out.push_back(array_access);

    auto type_decl = std::make_shared<Binary>(K::TypeDecl, ident("T"), typeName("Int32"));
    out.push_back(type_decl);

    auto call = std::make_shared<CallExpr>(K::CallExpr, ident("f"));
    call->m_args = std::vector<AstNodePtr>{lit()};
    out.push_back(call);

    auto func = std::make_shared<FuncDecl>("f");
    func->m_type = typeName("Int32");
    func->setAlias(AliasKind::Name, ident("g"), MapperRange{});
    func->m_params = std::vector<AstNodePtr>{std::make_shared<ArgNode>("a", typeName("Int32"), nullptr)};
    func->m_definition = Definition::fromBody({seq});
    out.push_back(func);

    auto dict = std::make_shared<DictLiteralNode>(K::DictLiteral);
    dict->m_body = {lit()};
    dict->m_type = typeName("Int32");
    out.push_back(dict);

    auto range = std::make_shared<RangeExpr>(K::RangeExpr, lit("0"), lit("1"));
    range->operandTypes = {typeName("Int32"), typeName("Int32")};
    out.push_back(range);

    auto jump = std::make_shared<JumpStmt>(K::ReturnStmt);
    jump->m_tempDecl = std::make_shared<VarDecl>("__trust_res_0", typeName("Int32"), lit());
    jump->m_value = ident("__trust_res_0");
    jump->m_label = ident("done");
    out.push_back(jump);

    return out;
}

} // namespace

TEST(AstSlots, VisitorMatchesChildrenForEverySample) {
    for (const AstNodePtr& sample : nodeSamples()) {
        ASSERT_NE(sample, nullptr);
        EXPECT_EQ(visitorKinds(*sample), childKinds(*sample)) << "kind=" << ParserToken::name(sample->kind());
    }
}

TEST(AstSlots, VisitorMatchesChildrenRecursively) {
    const std::vector<AstNodePtr> samples = nodeSamples(); // владелец: сырые указатели ниже живут всё время теста
    std::vector<const AstNodeBase*> stack;
    for (const AstNodePtr& sample : samples) {
        stack.push_back(sample.get());
    }
    std::size_t checked = 0;
    while (!stack.empty()) {
        const AstNodeBase* node = stack.back();
        stack.pop_back();
        EXPECT_EQ(visitorKinds(*node), childKinds(*node)) << "kind=" << ParserToken::name(node->kind());
        for (const AstNodePtr& child : node->children()) {
            if (child) {
                stack.push_back(child.get());
            }
        }
        ++checked;
    }
    EXPECT_GT(checked, 15u);

} // namespace
} // namespace

} // namespace trust
