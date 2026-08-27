#include "syntax/parser_test_fixture.hpp"
TEST_F(ParserTest, CodeSimple) {
    ASSERT_TRUE(Parse("{%code+code%};"));
    ASSERT_EQ("code+code", ast->getText());
}

TEST_F(ParserTest, CodeSimple2) {
    ASSERT_TRUE(Parse("{% code+code %};"));
    ASSERT_EQ(" code+code ", ast->getText());
}

TEST_F(ParserTest, Brakets1) {
    ASSERT_TRUE(Parse("(1+2)"));
    ASSERT_EQ("1 + 2", ast->toString());
}

TEST_F(ParserTest, Brakets2) {
    ASSERT_TRUE(Parse("(1==2)"));
    ASSERT_EQ("1 == 2", ast->toString());
}

TEST_F(ParserTest, Brakets3) {
    ASSERT_TRUE(Parse("(call())"));
    ASSERT_EQ("call()", ast->toString());
}

TEST_F(ParserTest, Brakets4) {
    ASSERT_TRUE(Parse("(:call())"));
    ASSERT_EQ(":call()", ast->toString());
}

TEST_F(ParserTest, Brakets5) {
    ASSERT_TRUE(Parse("(:call()==0)"));
    ASSERT_EQ(":call() == 0", ast->toString());
}

TEST_F(ParserTest, AssignSimple) {
    ASSERT_TRUE(Parse("term := term2;"));
    ASSERT_EQ("term := term2;", ast->toString());
}

TEST_F(ParserTest, AssignSimple2) {
    ASSERT_TRUE(Parse("\t term   :=   term2()  ;  \n"));
    ASSERT_EQ(TermID::CREATE_NAME, ast->getTermID()) << trust::toString(ast->getTermID());
    ASSERT_TRUE(ast->m_left);
    ASSERT_TRUE(ast->m_right);
    ASSERT_EQ("term", ast->m_left->getText());
    ASSERT_EQ(0, ast->m_left->size());
    ASSERT_EQ("term2", ast->m_right->getText());
    ASSERT_EQ(0, ast->m_right->size());
    ASSERT_EQ("term := term2();", ast->toString());
}

TEST_F(ParserTest, AssignFullName) {
    ASSERT_TRUE(Parse("term::name() := {term2;};"));
    ASSERT_EQ("term::name() := {term2;};", ast->toString());
}

TEST_F(ParserTest, AssignClass0) {
    ASSERT_TRUE(Parse("term := :Class();"));
    ASSERT_EQ("term := :Class();", ast->toString());
}

TEST_F(ParserTest, AssignClass1) {
    ASSERT_TRUE(Parse(":class  :=    :Class() {}  ;"));
    ASSERT_EQ(":class := :Class(){};", ast->toString());
}

TEST_F(ParserTest, AssignClass2) {
    ASSERT_TRUE(Parse(":class  :=  ::ns::func(arg1, arg2=\"\") {};"));
    ASSERT_EQ(":class := ::ns::func(arg1, arg2=\"\"){};", ast->toString());
}

TEST_F(ParserTest, Namespace) {
    ASSERT_TRUE(Parse("name{ func() := {}  };"));
    ASSERT_TRUE(Parse("name::space{ func() := {}  };"));
    ASSERT_TRUE(Parse("::name::space{ func() := {}  };"));
    ASSERT_TRUE(Parse("::{ func() := {}  };"));
}

// Метка блока (`ns { }`) должна попадать в ScopeBlock.name нового AST.
// (критерий приёмки задачи: ScopeBlock.name не пуст для `ns { }`)
TEST_F(ParserTest, NamespaceToScopeBlockName) {
    ASSERT_TRUE(Parse("ns { x := 1; };"));

    ASSERT_TRUE(ast);
    ASSERT_TRUE(ast->isBlock());
    ASSERT_EQ("ns", ast->getText());

    std::vector<AstNodePtr> nodes = TermToAstConverter::termToAst(ast, m_ctx);
    ASSERT_EQ(1, nodes.size());
    auto* sb = dynamic_cast<ScopeBlock*>(nodes[0].get());
    ASSERT_TRUE(sb);
    ASSERT_EQ("ns", sb->name());
    ASSERT_FALSE(sb->is_anonymous());
}

// Иммутабельность '^' не применима к меткам блоков/областям имён:
// termToAst должен выдать ошибку и НЕ проставить attr::ReadOnly.
TEST_F(ParserTest, NamespaceImmutableError) {
    ASSERT_TRUE(Parse("ns^ { x := 1; };"));

    m_ctx.diag().clear();
    std::vector<AstNodePtr> nodes = TermToAstConverter::termToAst(ast, m_ctx);
    ASSERT_GT(m_ctx.diag().errorCount(), 0) << "'^' in block label must produce an error";

    ASSERT_EQ(1, nodes.size());
    auto* sb = dynamic_cast<ScopeBlock*>(nodes[0].get());
    ASSERT_TRUE(sb);
    ASSERT_EQ("ns", sb->name());
    ASSERT_FALSE(sb->has_attr(m_ctx.attrs(), attr::ReadOnly));
}

// take(*^) при конвертации в AstNode: крышечка срезается (текст "*"),
// признак иммутабельности уходит в attr::ReadOnly.
TEST_F(ParserTest, TakeConstToAst) {
    ASSERT_TRUE(Parse("term(*^arg^);"));

    std::vector<AstNodePtr> nodes = TermToAstConverter::termToAst(ast, m_ctx);
    ASSERT_EQ(1, nodes.size());

    // term(*^arg^) - вызов: корневой узел CallExpr (callee = имя term, args = [операнд]).
    auto* termNode = dynamic_cast<CallExpr*>(nodes[0].get());
    ASSERT_TRUE(termNode);
    ASSERT_EQ("term", termNode->m_callee->text());
    ASSERT_TRUE(termNode->m_args && !termNode->m_args->empty());

    auto* take = dynamic_cast<Sequence*>(termNode->m_args->at(0).get());
    ASSERT_TRUE(take);
    ASSERT_EQ(ParserToken::Kind::RefTakeExpr, take->kind());
    ASSERT_EQ("*", take->text()) << "'^' must be stripped from take text";
    ASSERT_TRUE(take->has_attr(m_ctx.attrs(), attr::ReadOnly));

    // arg^ - имя с иммутабельностью: срезается '^', проставляется attr::ReadOnly.
    auto* arg = dynamic_cast<AstNodeAttr*>(take->m_body[0].get());
    ASSERT_TRUE(arg);
    ASSERT_EQ("arg", arg->text());
    ASSERT_TRUE(arg->has_attr(m_ctx.attrs(), attr::ReadOnly));
}

// ptr(&) при конвертации в AstNode: узел-оператор становится RefMakeExpr,
// операнд - его телом; иммутабельность &^ → attr::ReadOnly.
TEST_F(ParserTest, RefMakeToAst) {
    ASSERT_TRUE(Parse("term(&arg);"));

    std::vector<AstNodePtr> nodes = TermToAstConverter::termToAst(ast, m_ctx);
    ASSERT_EQ(1, nodes.size());

    auto* termNode = dynamic_cast<CallExpr*>(nodes[0].get());
    ASSERT_TRUE(termNode);
    ASSERT_EQ("term", termNode->m_callee->text());
    ASSERT_TRUE(termNode->m_args && !termNode->m_args->empty());

    auto* refMake = dynamic_cast<Sequence*>(termNode->m_args->at(0).get());
    ASSERT_TRUE(refMake);
    ASSERT_EQ(ParserToken::Kind::RefMakeExpr, refMake->kind());
    ASSERT_EQ("&", refMake->text());
    ASSERT_FALSE(refMake->has_attr(m_ctx.attrs(), attr::ReadOnly));

    auto* arg = dynamic_cast<AstNodeAttr*>(refMake->m_body[0].get());
    ASSERT_TRUE(arg);
    ASSERT_EQ("arg", arg->text());
    ASSERT_FALSE(arg->has_attr(m_ctx.attrs(), attr::ReadOnly));

    // &^ - иммутабельность оператора → attr::ReadOnly на RefMakeExpr.
    ASSERT_TRUE(Parse("term(&^arg);"));
    nodes = TermToAstConverter::termToAst(ast, m_ctx);
    ASSERT_EQ(1, nodes.size());
    termNode = dynamic_cast<CallExpr*>(nodes[0].get());
    ASSERT_TRUE(termNode);
    refMake = dynamic_cast<Sequence*>(termNode->m_args->at(0).get());
    ASSERT_TRUE(refMake);
    ASSERT_EQ(ParserToken::Kind::RefMakeExpr, refMake->kind());
    ASSERT_EQ("&", refMake->text());
    ASSERT_TRUE(refMake->has_attr(m_ctx.attrs(), attr::ReadOnly));

    // &arg^ - иммутабельность операнда → attr::ReadOnly на arg, не на операторе.
    ASSERT_TRUE(Parse("term(&arg^);"));
    nodes = TermToAstConverter::termToAst(ast, m_ctx);
    ASSERT_EQ(1, nodes.size());
    termNode = dynamic_cast<CallExpr*>(nodes[0].get());
    ASSERT_TRUE(termNode);
    refMake = dynamic_cast<Sequence*>(termNode->m_args->at(0).get());
    ASSERT_TRUE(refMake);
    ASSERT_EQ(ParserToken::Kind::RefMakeExpr, refMake->kind());
    ASSERT_EQ("&", refMake->text());
    ASSERT_FALSE(refMake->has_attr(m_ctx.attrs(), attr::ReadOnly));
    arg = dynamic_cast<AstNodeAttr*>(refMake->m_body[0].get());
    ASSERT_TRUE(arg);
    ASSERT_EQ("arg", arg->text());
    ASSERT_TRUE(arg->has_attr(m_ctx.attrs(), attr::ReadOnly));
}
// ELLIPSIS ("...") - синтаксис, распознанный лексером/грамматикой в аргументах - теперь
// конвертируется в AST-узел (kind=Ellipsis, Sequence). Ранее (без Kind) конвертация FAULT.
TEST_F(ParserTest, EllipsisToAst) {
    m_ctx.diag().clear();
    auto term = Term::Create(TermID::ELLIPSIS, "...");
    std::vector<AstNodePtr> nodes = TermToAstConverter::termToAst(term, m_ctx);
    ASSERT_EQ(0, m_ctx.diag().errorCount()) << "ELLIPSIS must convert without error (not FAULT)";
    ASSERT_EQ(1, nodes.size());
    auto* ell = dynamic_cast<Sequence*>(nodes[0].get());
    ASSERT_TRUE(ell);
    ASSERT_EQ(ParserToken::Kind::Ellipsis, ell->kind());
    ASSERT_EQ("...", ell->text());
}

// TypeName-терм-конструктор - единственный владелец раскладки TYPE-терма:
//   m_dims   из term->m_type (ARGS-терм размерностей `[...]`)
//   m_params из term->m_args (call-аргументы `(...)`)
TEST_F(ParserTest, TypeNameDimsParamsToAst) {
    ASSERT_TRUE(Parse("m:Matrix[2,3](Float) := mat;"));
    std::vector<AstNodePtr> nodes = TermToAstConverter::termToAst(ast, m_ctx);
    ASSERT_EQ(1, nodes.size());
    auto* vd = dynamic_cast<VarDecl*>(nodes[0].get());
    ASSERT_TRUE(vd);
    auto* t = dynamic_cast<IdentType*>(vd->m_type.get());
    ASSERT_TRUE(t);
    ASSERT_EQ("Matrix", t->text());
    ASSERT_TRUE(t->dims() && t->dims()->size() == 2) << "dims из [...]: 2 измерения";
    ASSERT_TRUE(t->params() && t->params()->size() == 1) << "params из (...): 1 generic-параметр";
}

TEST_F(ParserTest, TypeNameParamsOnlyToAst) {
    // Параметризованная аннотация с ТИПИЗИРОВАННЫМИ аргументами `Pair(:Int, :String)` → IdentType
    // с параметрами. (Голые имена `Pair(Int, String)` - это value-форма/вызов → DictLiteralNode,
    // см. TypeCastExprToAst; аннотация с параметрами пишется с типами-аргументами.)
    ASSERT_TRUE(Parse("p:Pair(:Int, :String) := 0;"));
    std::vector<AstNodePtr> nodes = TermToAstConverter::termToAst(ast, m_ctx);
    ASSERT_EQ(1, nodes.size());
    auto* vd = dynamic_cast<VarDecl*>(nodes[0].get());
    ASSERT_TRUE(vd);
    auto* t = dynamic_cast<IdentType*>(vd->m_type.get());
    ASSERT_TRUE(t);
    ASSERT_EQ("Pair", t->text());
    ASSERT_FALSE(t->dims()) << "нет dims";
    ASSERT_TRUE(t->params() && t->params()->size() == 2) << "params из (...): 2 аргумента";
}

TEST_F(ParserTest, TypeCastExprToAst) {
    // `:Type(expr)` в позиции выражения → ЕДИНЫЙ узел DictLiteralNode с аннотацией типа
    // m_type=TypeName и элементами (имя=значение). Класс узла (кортеж/каст/конструктор)
    // определяет анализатор по типу из реестра (kind CastExpr упразднён).
    ASSERT_TRUE(Parse("b:Int8 := :Int8(a);"));
    std::vector<AstNodePtr> nodes = TermToAstConverter::termToAst(ast, m_ctx);
    ASSERT_EQ(1, nodes.size());
    auto* vd = dynamic_cast<VarDecl*>(nodes[0].get());
    ASSERT_TRUE(vd);
    ASSERT_TRUE(vd->m_initializer);
    auto* dl = dynamic_cast<DictLiteralNode*>(vd->m_initializer.get());
    ASSERT_TRUE(dl);
    EXPECT_EQ(dl->kind(), ParserToken::Kind::DictLiteral);
    ASSERT_TRUE(dl->m_type);
    EXPECT_EQ(dl->m_type->kind(), ParserToken::Kind::TypeName);
    EXPECT_EQ(dl->m_type->text(), "Int8");
    ASSERT_EQ(dl->m_body.size(), 1);
    ASSERT_TRUE(dl->m_body[0]);
    EXPECT_EQ(dl->m_body[0]->kind(), ParserToken::Kind::ArgNode);
    const auto& b = static_cast<const ArgNode&>(*dl->m_body[0]);
    EXPECT_TRUE(b.text().empty()) << "безымянный элемент";
    ASSERT_TRUE(b.m_value);
    EXPECT_EQ(b.m_value->kind(), ParserToken::Kind::Ident);
    EXPECT_EQ(b.m_value->text(), "a");
}

// -- Диапазон `start..stop[..step]` (TermID::RANGE) → RangeExpr --
// Терм `range` хранит операнды в m_args с именами start/stop/step; конвертер строит
// m_body=[start, stop, (step)] и переносит аннотации типа операндов в operandTypes.
TEST_F(ParserTest, RangeExprToAst) {
    auto r = Term::Create(TermID::RANGE, "..");
    r->push_back(Term::Create(TermID::INTEGER, "1"), "start");
    r->push_back(Term::Create(TermID::INTEGER, "10"), "stop");
    std::vector<AstNodePtr> nodes = TermToAstConverter::termToAst(r, m_ctx);
    ASSERT_EQ(1, nodes.size());
    auto* re = dynamic_cast<RangeExpr*>(nodes[0].get());
    ASSERT_TRUE(re);
    EXPECT_EQ(re->kind(), ParserToken::Kind::RangeExpr);
    ASSERT_EQ(re->m_body.size(), 2);
    EXPECT_EQ(re->start()->kind(), ParserToken::Kind::IntLiteral);
    EXPECT_EQ(re->stop()->kind(), ParserToken::Kind::IntLiteral);
    EXPECT_FALSE(re->hasStep());
}

TEST_F(ParserTest, RangeExprWithStepToAst) {
    auto r = Term::Create(TermID::RANGE, "..");
    r->push_back(Term::Create(TermID::INTEGER, "0"), "start");
    r->push_back(Term::Create(TermID::INTEGER, "10"), "stop");
    r->push_back(Term::Create(TermID::NUMBER, "0.01"), "step");
    std::vector<AstNodePtr> nodes = TermToAstConverter::termToAst(r, m_ctx);
    ASSERT_EQ(1, nodes.size());
    auto* re = dynamic_cast<RangeExpr*>(nodes[0].get());
    ASSERT_TRUE(re);
    ASSERT_EQ(re->m_body.size(), 3);
    EXPECT_TRUE(re->hasStep());
    EXPECT_EQ(re->step()->kind(), ParserToken::Kind::FloatLiteral);
}

TEST_F(ParserTest, RangeExprTypedStopToAst) {
    // `0..100:Rational` - stop-операнд с аннотацией типа (грамматика `digits_literal type_item`
    // кладёт её в m_type) → конвертер переносит её в RangeExpr::operandTypes[1] (TypeName Rational).
    auto r = Term::Create(TermID::RANGE, "..");
    r->push_back(Term::Create(TermID::INTEGER, "0"), "start");
    auto stop = Term::Create(TermID::INTEGER, "100");
    stop->m_type = Term::Create(TermID::TYPE, ":Rational");
    r->push_back(stop, "stop");
    std::vector<AstNodePtr> nodes = TermToAstConverter::termToAst(r, m_ctx);
    ASSERT_EQ(1, nodes.size());
    auto* re = dynamic_cast<RangeExpr*>(nodes[0].get());
    ASSERT_TRUE(re);
    ASSERT_EQ(re->m_body.size(), 2);
    ASSERT_EQ(re->operandTypes.size(), 2);
    EXPECT_EQ(re->operandTypes[0], nullptr);
    ASSERT_TRUE(re->operandTypes[1]);
    EXPECT_EQ(re->operandTypes[1]->kind(), ParserToken::Kind::TypeName);
    EXPECT_EQ(re->operandTypes[1]->text(), "Rational");
}

TEST_F(ParserTest, TypeNameDimsOnlyToAst) {
    ASSERT_TRUE(Parse("l:List[Int] := 0;"));
    std::vector<AstNodePtr> nodes = TermToAstConverter::termToAst(ast, m_ctx);
    ASSERT_EQ(1, nodes.size());
    auto* vd = dynamic_cast<VarDecl*>(nodes[0].get());
    ASSERT_TRUE(vd);
    auto* t = dynamic_cast<IdentType*>(vd->m_type.get());
    ASSERT_TRUE(t);
    ASSERT_EQ("List", t->text());
    ASSERT_TRUE(t->dims() && t->dims()->size() == 1) << "dims из [...]: 1 размерность";
    ASSERT_FALSE(t->params()) << "нет params";
}

// -- Класс-селекция Ident→CallExpr vs IdentName --
// Голое имя (без детей) → IdentName; вызов (есть дети) → CallExpr.
TEST_F(ParserTest, IdentBareToIdentName) {
    auto t = Term::Create(TermID::NAME, "x");
    std::vector<AstNodePtr> nodes = TermToAstConverter::termToAst(t, m_ctx);
    ASSERT_EQ(1, nodes.size());
    auto* id = dynamic_cast<IdentName*>(nodes[0].get());
    ASSERT_TRUE(id);
    ASSERT_EQ("x", id->text());
}

TEST_F(ParserTest, IdentCallToCallExpr) {
    auto t = Term::Create(TermID::NAME, "f");
    t->m_args.emplace();
    t->push_back(Term::Create(TermID::NAME, "a"));
    std::vector<AstNodePtr> nodes = TermToAstConverter::termToAst(t, m_ctx);
    ASSERT_EQ(1, nodes.size());
    auto* call = dynamic_cast<CallExpr*>(nodes[0].get());
    ASSERT_TRUE(call);
    ASSERT_TRUE(call->m_callee);
    ASSERT_EQ("f", call->m_callee->text());
    ASSERT_TRUE(call->m_args && call->m_args->size() == 1);
}

// f() (пустые args) → CallExpr: наличие m_args (даже пустого) = вызов по структурному
// предикату `m_args || m_sequence || m_left || m_right`. IdentName остаётся только для
// терма без m_args (голое имя, см. IdentBareToIdentName).
TEST_F(ParserTest, IdentEmptyCallToCallExpr) {
    auto t = Term::Create(TermID::NAME, "f");
    t->m_args.emplace(); // f()
    std::vector<AstNodePtr> nodes = TermToAstConverter::termToAst(t, m_ctx);
    ASSERT_EQ(1, nodes.size());
    auto* call = dynamic_cast<CallExpr*>(nodes[0].get());
    ASSERT_TRUE(call) << "f() (пустые args) → CallExpr (вызов без аргументов)";
    ASSERT_TRUE(call->m_callee);
    ASSERT_EQ("f", call->m_callee->text());
}

// Именованный аргумент (name=value) → Binary(AssignOp) внутри args (visit_ARGUMENT class-select).
TEST_F(ParserTest, CallNamedArgToArgNode) {
    auto t = Term::Create(TermID::NAME, "f");
    t->m_args.emplace();
    auto arg = Term::Create(TermID::ARGUMENT, "");
    arg->m_left = Term::Create(TermID::NAME, "x");
    arg->m_right = Term::Create(TermID::INTEGER, "5");
    t->push_back(arg, "x");
    std::vector<AstNodePtr> nodes = TermToAstConverter::termToAst(t, m_ctx);
    ASSERT_EQ(1, nodes.size());
    auto* call = dynamic_cast<CallExpr*>(nodes[0].get());
    ASSERT_TRUE(call);
    ASSERT_TRUE(call->m_args && call->m_args->size() == 1);
    auto* a = dynamic_cast<ArgNode*>(call->m_args->at(0).get());
    ASSERT_TRUE(a);
    ASSERT_EQ("x", a->text());
    ASSERT_TRUE(a->m_value);
    ASSERT_EQ("5", a->m_value->text());
}

// -- VarDecl (visit_CREATE_NAME → VarDecl-конструктор) --
TEST_F(ParserTest, VarDeclSimpleToAst) {
    ASSERT_TRUE(Parse("x := 5;"));
    std::vector<AstNodePtr> nodes = TermToAstConverter::termToAst(ast, m_ctx);
    ASSERT_EQ(1, nodes.size());
    auto* vd = dynamic_cast<VarDecl*>(nodes[0].get());
    ASSERT_TRUE(vd);
    ASSERT_EQ("x", vd->text());
    ASSERT_FALSE(vd->m_type);
    ASSERT_TRUE(vd->m_initializer);
    ASSERT_EQ(ParserToken::Kind::IntLiteral, vd->m_initializer->kind());
}

TEST_F(ParserTest, VarDeclTypedToAst) {
    ASSERT_TRUE(Parse("x:Int := 5;"));
    std::vector<AstNodePtr> nodes = TermToAstConverter::termToAst(ast, m_ctx);
    ASSERT_EQ(1, nodes.size());
    auto* vd = dynamic_cast<VarDecl*>(nodes[0].get());
    ASSERT_TRUE(vd);
    ASSERT_EQ("x", vd->text());
    ASSERT_TRUE(vd->m_type);
    ASSERT_EQ(ParserToken::Kind::TypeName, vd->m_type->kind());
}

// -- Функции через CREATE_NAME (`:=`) → FuncDecl-конструктор --
// CREATE_NAME - единый узел функции И переменной; класс-селекция по m_left->isCall().
TEST_F(ParserTest, FuncDeclViaAssignToAst) {
    ASSERT_TRUE(Parse("add(a:Int, b:Int):Int := { ++ a ++; };"));
    std::vector<AstNodePtr> nodes = TermToAstConverter::termToAst(ast, m_ctx);
    ASSERT_EQ(1, nodes.size());
    auto* fd = dynamic_cast<FuncDecl*>(nodes[0].get());
    ASSERT_TRUE(fd);
    ASSERT_EQ("add", fd->text());
    ASSERT_TRUE(fd->m_type);
    ASSERT_EQ(ParserToken::Kind::TypeName, fd->m_type->kind());
    ASSERT_TRUE(fd->m_params && fd->m_params->size() == 2);
    for (const auto& p : *fd->m_params) {
        auto* pd = dynamic_cast<ArgNode*>(p.get());
        ASSERT_TRUE(pd);
        ASSERT_TRUE(pd->m_type) << "параметр должен нести тип (из m_right)";
    }
    ASSERT_TRUE(fd->m_body && !fd->m_body->empty()) << "функция должна иметь тело";
}

// Native-функция - тоже обычная функция через `:=` (m_left - native-идентификатор с сигнатурой).
// NATIVE отдельно не выделяется.
TEST_F(ParserTest, NativeFuncDeclToAst) {
    ASSERT_TRUE(Parse("%add(a:Int, b:Int):Int := { ++ a ++; };"));
    std::vector<AstNodePtr> nodes = TermToAstConverter::termToAst(ast, m_ctx);
    ASSERT_EQ(1, nodes.size());
    auto* fd = dynamic_cast<FuncDecl*>(nodes[0].get());
    ASSERT_TRUE(fd);
    ASSERT_EQ("%add", fd->text());
    ASSERT_TRUE(fd->m_type);
    ASSERT_TRUE(fd->m_params && fd->m_params->size() == 2);
    ASSERT_TRUE(fd->m_body && !fd->m_body->empty()) << "native-функция должна иметь тело";
}

// CREATE_TYPE (`::=`) - синоним типа, а НЕ функция (даже с формой вызова в m_left).
TEST_F(ParserTest, CreateTypeIsTypeSynonymToAst) {
    ASSERT_TRUE(Parse("MyInt ::= Int;"));
    std::vector<AstNodePtr> nodes = TermToAstConverter::termToAst(ast, m_ctx);
    ASSERT_EQ(1, nodes.size());
    auto* bin = dynamic_cast<Binary*>(nodes[0].get());
    ASSERT_TRUE(bin) << "::= - это синоним типа (Binary TypeDecl), не функция";
    ASSERT_EQ(ParserToken::Kind::TypeDecl, bin->kind());
}

// Forward-объявление переменной `x:Int32 := ...;` - чистое многоточие вместо инициализатора.
TEST_F(ParserTest, ForwardVarDeclToAst) {
    ASSERT_TRUE(Parse("x:Int32 := ...;"));
    std::vector<AstNodePtr> nodes = TermToAstConverter::termToAst(ast, m_ctx);
    ASSERT_EQ(1, nodes.size());
    auto* vd = dynamic_cast<VarDecl*>(nodes[0].get());
    ASSERT_TRUE(vd);
    ASSERT_EQ("x", vd->text());
    ASSERT_TRUE(vd->m_type);
    ASSERT_EQ(ParserToken::Kind::TypeName, vd->m_type->kind());
    ASSERT_EQ(nullptr, vd->m_initializer) << "forward-объявление не должно иметь инициализатора";
}

// Forward-объявление переменной без типа `y := ...;` - инициализатора нет, тип опционален.
TEST_F(ParserTest, ForwardVarDeclNoTypeToAst) {
    ASSERT_TRUE(Parse("y := ...;"));
    std::vector<AstNodePtr> nodes = TermToAstConverter::termToAst(ast, m_ctx);
    ASSERT_EQ(1, nodes.size());
    auto* vd = dynamic_cast<VarDecl*>(nodes[0].get());
    ASSERT_TRUE(vd);
    ASSERT_EQ("y", vd->text());
    ASSERT_EQ(nullptr, vd->m_type);
    ASSERT_EQ(nullptr, vd->m_initializer) << "forward-объявление не должно иметь инициализатора";
}

// Forward-объявление функции `%add(a:Int32, b:Int32):Int32 := ...;` - чистое многоточие
// вместо тела → m_body = nullopt (forward declaration).
TEST_F(ParserTest, ForwardFuncDeclToAst) {
    ASSERT_TRUE(Parse("%add(a:Int32, b:Int32):Int32 := ...;"));
    std::vector<AstNodePtr> nodes = TermToAstConverter::termToAst(ast, m_ctx);
    ASSERT_EQ(1, nodes.size());
    auto* fd = dynamic_cast<FuncDecl*>(nodes[0].get());
    ASSERT_TRUE(fd);
    ASSERT_EQ("%add", fd->text());
    ASSERT_TRUE(fd->m_type);
    ASSERT_TRUE(fd->m_params && fd->m_params->size() == 2);
    for (const auto& p : *fd->m_params) {
        auto* pd = dynamic_cast<ArgNode*>(p.get());
        ASSERT_TRUE(pd);
        ASSERT_TRUE(pd->m_type) << "параметр forward-функции должен нести тип";
    }
    ASSERT_FALSE(fd->m_body.has_value()) << "forward-объявление не должно иметь тела";
}

// Нереализованная конструкция (TermID без Kind, напр. await "[*]") при конвертации должна
// дать диагностику Severity::Error с позицией, а НЕ внутренний FAULT.
TEST_F(ParserTest, UnimplementedConstructReportsError) {
    m_ctx.diag().clear();
    auto term = Term::Create(TermID::AWAIT, "[*]");
    std::vector<AstNodePtr> nodes = TermToAstConverter::termToAst(term, m_ctx);
    ASSERT_GT(m_ctx.diag().errorCount(), 0) << "unimplemented construct must report an error";
    ASSERT_EQ(0, nodes.size()) << "unimplemented node must be dropped (convert returns nullptr)";
}

TEST_F(ParserTest, AssignFullName2) {
    ASSERT_TRUE(Parse("term::name::name2() := term2;"));
    ASSERT_EQ("term::name::name2() := term2;", ast->toString());
}

TEST_F(ParserTest, AssignFullName3) {
    ASSERT_TRUE(Parse("::term::name::name3() := term2;"));
    ASSERT_EQ("::term::name::name3() := term2;", ast->toString());
}

// TEST_F(ParserTest, FiledAssign) {
//     ASSERT_TRUE(Parse("$1.val :=  123;"));
//     ASSERT_EQ(TermID::CREATE_NAME, ast->getTermID()) << trust::toString(ast->getTermID());
//     ASSERT_TRUE(ast->m_left);
//     ASSERT_TRUE(ast->m_right);
//
//     ASSERT_EQ(TermID::ARGUMENT, ast->m_left->getTermID());
//     ASSERT_EQ("$1", ast->m_left->getText());
//
//     ASSERT_TRUE(ast->m_left->m_right);
//     ASSERT_EQ("val", ast->m_left->m_right->getText());
//
//     ASSERT_EQ(TermID::INTEGER, ast->m_right->getTermID());
//     ASSERT_EQ("123", ast->m_right->getText());
// }

// TEST_F(ParserTest, FiledAssign2) {
//     ASSERT_TRUE(Parse("term.field1.field2 :=  123;"));
//     ASSERT_EQ(TermID::CREATE_NAME, ast->getTermID()) << trust::toString(ast->getTermID());
//     ASSERT_TRUE(ast->m_left);
//     ASSERT_TRUE(ast->m_right);
//
//     ASSERT_EQ(TermID::NAME, ast->m_left->getTermID());
//     ASSERT_EQ("term", ast->m_left->getText());
//
//     ASSERT_TRUE(ast->m_left->m_right);
//     ASSERT_EQ("field1", ast->m_left->m_right->getText());
//     ASSERT_TRUE(ast->m_left->m_right->m_right);
//     ASSERT_EQ("field2", ast->m_left->m_right->m_right->getText());
//     ASSERT_FALSE(ast->m_left->m_right->m_right->m_right);
//
//     ASSERT_EQ(TermID::INTEGER, ast->m_right->getTermID());
//     ASSERT_EQ("123", ast->m_right->getText());
//
//     ASSERT_EQ("term.field1.field2 := 123;", ast->toString());
// }

TEST_F(ParserTest, ArrayAssign) {
    ASSERT_TRUE(Parse("$0[0] =  123;"));
    ASSERT_EQ(TermID::ASSIGN, ast->getTermID()) << trust::toString(ast->getTermID());
    ASSERT_TRUE(ast->m_left);
    ASSERT_TRUE(ast->m_right);

    ASSERT_EQ(TermID::INDEX, ast->m_left->getTermID()) << trust::toString(ast->getTermID());
    ASSERT_TRUE(ast->m_left->m_left);
    ASSERT_EQ(TermID::ARGUMENT, ast->m_left->m_left->getTermID());
    ASSERT_EQ("$0", ast->m_left->m_left->getText());

    ASSERT_EQ(1, ast->m_left->size());
    ASSERT_EQ("0", ast->m_left->at(0).second->getText());
}

TEST_F(ParserTest, DISABLED_ArrayAssign2) {
    ASSERT_TRUE(Parse("term[1][1..3] =  123;"));
    ASSERT_EQ(TermID::ASSIGN, ast->getTermID()) << trust::toString(ast->getTermID());
    ASSERT_TRUE(ast->m_left);
    ASSERT_TRUE(ast->m_right);

    ASSERT_EQ(TermID::NAME, ast->m_left->getTermID());
    ASSERT_EQ("term", ast->m_left->getText());

    ASSERT_TRUE(ast->m_left->m_right);
    ASSERT_EQ("[", ast->m_left->m_right->getText());
    ASSERT_TRUE(ast->m_left->m_right->m_right);
    ASSERT_EQ("[", ast->m_left->m_right->m_right->getText());
    ASSERT_FALSE(ast->m_left->m_right->m_right->m_right);

    ASSERT_EQ(TermID::INTEGER, ast->m_right->getTermID());
    ASSERT_EQ("123", ast->m_right->getText());

    ASSERT_EQ("term[1][1, 2, 3]=123;", ast->toString());
}

TEST_F(ParserTest, DISABLED_FieldArray) {
    ASSERT_TRUE(Parse("term.val[1].field :=  value[-1..@count()..5].field;"));
    ASSERT_EQ(TermID::CREATE_NAME, ast->getTermID()) << trust::toString(ast->getTermID());
    ASSERT_EQ("term.val[1].field := value[-1..@count()..5].field;", ast->toString());
}

TEST_F(ParserTest, AssignSimple3) {
    ASSERT_TRUE(Parse("\t term   :=   term2(   )  ;  \n"));
    ASSERT_EQ(TermID::CREATE_NAME, ast->getTermID()) << trust::toString(ast->getTermID()) << ast->toString();
    ASSERT_TRUE(ast->m_left);
    ASSERT_TRUE(ast->m_right);
    ASSERT_EQ("term := term2();", ast->toString());
}

TEST_F(ParserTest, AssignSimpleArg) {
    ASSERT_TRUE(Parse("\t term    ::=    term2( arg  ) ;   \n"));
    ASSERT_EQ(TermID::CREATE_TYPE, ast->getTermID()) << trust::toString(ast->getTermID());
    ASSERT_TRUE(ast->m_left);
    ASSERT_TRUE(ast->m_right);
    ASSERT_EQ("term ::= term2(arg);", ast->toString());
}

TEST_F(ParserTest, AssignSimpleNamedArg) {
    ASSERT_TRUE(Parse("\t term  :=  $term2( arg = arg2  )\n;\n\n"));
    ASSERT_EQ("term := $term2(arg=arg2);", ast->toString());
}

TEST_F(ParserTest, AssignNamedArgs0) {
    ASSERT_TRUE(Parse("\t term   :=   \\term2( arg, arg1 = arg2  )  ;  \n"));
    ASSERT_EQ("term := \\term2(arg, arg1=arg2);", ast->toString());
}

TEST_F(ParserTest, AssignNamedArgs1) {
    ASSERT_TRUE(Parse("\t term   :=   @term2( arg, arg1 = arg2  )  ;  \n"));
    ASSERT_EQ("term := @term2(arg, arg1=arg2);", ast->toString());
}

TEST_F(ParserTest, AssignNamedArgs2) {
    ASSERT_TRUE(Parse("\t term   :=   $term2( arg, arg1 = arg2(arg3))  ;  \n"));
    ASSERT_EQ("term := $term2(arg, arg1=arg2(arg3));", ast->toString());
}

TEST_F(ParserTest, AssignNamedArgs3) {
    ASSERT_TRUE(Parse("\t $term   :=   term2( \\arg, arg1 = 123  )  ;  \n"));
    ASSERT_EQ("$term := term2(\\arg, arg1=123);", ast->toString());
}

TEST_F(ParserTest, AssignNamedArgs4) {
    ASSERT_TRUE(Parse("\t %term   :=   term2( arg, arg1 = \\arg2($arg3))  ;  \n"));
    ASSERT_EQ("%term := term2(arg, arg1=\\arg2($arg3));", ast->toString());
}

TEST_F(ParserTest, AssignNamedArgs5) {
    ASSERT_TRUE(Parse("\t %term   :=   term2( arg, arg1 = \\\\arg2($arg3))  ;  \n"));
    ASSERT_EQ("%term := term2(arg, arg1=\\\\arg2($arg3));", ast->toString());
}

TEST_F(ParserTest, AssignNamedArgs6) {
    ASSERT_TRUE(Parse("\t %term   :=   term2( arg, arg1 = @arg2($arg3))  ;  \n"));
    ASSERT_EQ("%term := term2(arg, arg1=@arg2($arg3));", ast->toString());
}

TEST_F(ParserTest, AssignString) {
    ASSERT_TRUE(Parse("term := \"строка\";"));
    ASSERT_EQ("term := \"строка\";", ast->toString());
}

TEST_F(ParserTest, AssignString2) {
    ASSERT_TRUE(Parse("$2  :=  \"строка\" ; \n"));
    ASSERT_EQ("$2 := \"строка\";", ast->toString());
}

TEST_F(ParserTest, AssignStringControlChar) {
    ASSERT_TRUE(Parse("$2 :=  \"стр\\\"\t\r\xffока\\s\" ; \n"));
    /* Esc-последовательности больше не декодируются лексером, сохраняются как есть */
    ASSERT_EQ("$2 := \"стр\\\"\t\r\xffока\\s\";", ast->toString());
}

TEST_F(ParserTest, AssignStringMultiline) {
    ASSERT_TRUE(Parse("term  :=  'стр\\\n\t  ока\\\n   \\s' ; \n"));
    /* Esc-последовательности больше не декодируются лексером, сохраняются как есть */
    ASSERT_EQ("term := 'стр\\\n\t  ока\\\n   \\s';", ast->toString());
}

TEST_F(ParserTest, AssignDictEmpty) {
    ASSERT_TRUE(Parse("term := (   ,    );"));
    ASSERT_EQ("term := (,);", ast->toString());
}

TEST_F(ParserTest, AssignDict) {
    ASSERT_TRUE(Parse("term := (name,)"));
    ASSERT_TRUE(ast);
    ASSERT_TRUE(ast->m_left);
    ASSERT_TRUE(ast->m_right);
    ASSERT_TRUE(ast->m_right->m_id == TermID::DICT);
    ASSERT_EQ("term := (name,);", ast->toString());

    ASSERT_TRUE(Parse("term := (  123 , )"));
    ASSERT_EQ("term := (123,);", ast->toString());

    ASSERT_TRUE(Parse("term := (  name  = 123 ,  )"));
    ASSERT_EQ("term := (name=123,);", ast->toString());
}

TEST_F(ParserTest, AssignArray) {
    ASSERT_TRUE(Parse("term := [  123  , ]"));
    ASSERT_EQ("term := [123,];", ast->toString());
}

TEST_F(ParserTest, ArgsArray1) {
    ASSERT_TRUE(Parse("term([1,]);"));
    ASSERT_EQ(TermID::NAME, ast->getTermID()) << trust::toString(ast->getTermID());
    ASSERT_EQ("term", ast->getText());
    ASSERT_EQ(1, ast->size());
    ASSERT_EQ("[1,]", ast->at(0).second->toString());
}

TEST_F(ParserTest, LogicEq) {
    ASSERT_TRUE(Parse("var := 1==2;"));
    ASSERT_EQ("var := 1 == 2;", ast->toString());
}

TEST_F(ParserTest, LogicNe) {
    ASSERT_TRUE(Parse("var := 1!=2;"));
    ASSERT_EQ("var := 1 != 2;", ast->toString());
}

TEST_F(ParserTest, InstanceName) {
    ASSERT_TRUE(Parse("var ~ Class"));
    ASSERT_TRUE(Parse("var ~ :Class"));
    ASSERT_TRUE(Parse("var ~ 'name'"));
    ASSERT_TRUE(Parse("var ~ $var"));
    ASSERT_TRUE(Parse("1  ~  $var"));
    ASSERT_TRUE(Parse("'строка'  ~  'тип'"));
    ASSERT_TRUE(Parse("1..20 ~ var_name"));

    ASSERT_TRUE(Parse("var ~~ Class"));
    ASSERT_TRUE(Parse("var ~~ :Class"));
    ASSERT_TRUE(Parse("var ~~ 'name'"));
    ASSERT_TRUE(Parse("var ~~ $var"));
    ASSERT_TRUE(Parse("1  ~~  $var"));
    ASSERT_TRUE(Parse("'строка'  ~~  'тип'"));
    ASSERT_TRUE(Parse("1..20 ~~ var_name"));
}

TEST_F(ParserTest, FunctionSimple) {
    ASSERT_TRUE(Parse("func() := {{%%}};"));
    ASSERT_EQ(TermID::CREATE_NAME, ast->getTermID()) << trust::toString(ast->getTermID());
    ASSERT_EQ("func() := {{%%};};", ast->toString());
}

TEST_F(ParserTest, FunctionSimpleTwo) {
    ASSERT_TRUE(Parse("func() := {{% %};{% %}};"));
    ASSERT_EQ(TermID::CREATE_NAME, ast->getTermID()) << trust::toString(ast->getTermID());
    ASSERT_EQ("func() := {{% %}; {% %};};", ast->toString());
}

TEST_F(ParserTest, FunctionSimple2) {
    ASSERT_TRUE(Parse("func(arg)  :=  {$0:=0;};"));
    ASSERT_EQ(TermID::CREATE_NAME, ast->getTermID()) << trust::toString(ast->getTermID());
    ASSERT_EQ("func(arg) := {$0 := 0;};", ast->toString());
}

TEST_F(ParserTest, FunctionSimple3) {
    ASSERT_TRUE(Parse("func(arg)  :=  {{%  %};{% %};{%  %}; $99:=0;};"));
    ASSERT_EQ(TermID::CREATE_NAME, ast->getTermID()) << trust::toString(ast->getTermID());
    ASSERT_EQ("func(arg) := {{%  %}; {% %}; {%  %}; $99 := 0;};", ast->toString());
}

TEST_F(ParserTest, FunctionSimple4) {
    ASSERT_TRUE(Parse("func(arg) := {$33:=0;};"));
    ASSERT_EQ(TermID::CREATE_NAME, ast->getTermID()) << trust::toString(ast->getTermID());
    ASSERT_EQ("func(arg) := {$33 := 0;};", ast->toString());
}

TEST_F(ParserTest, FunctionSimple5) {
    ASSERT_TRUE(Parse("print(str=\"\") :={% printf(\"%s\", static_cast<char *>($str)); %};"));
    ASSERT_EQ(TermID::CREATE_NAME, ast->getTermID()) << trust::toString(ast->getTermID());
    ASSERT_EQ("print(str=\"\") := {% printf(\"%s\", static_cast<char *>($str)); %};", ast->toString());
}

TEST_F(ParserTest, FunctionRussian1) {
    ASSERT_TRUE(Parse("мин(arg) := {$00:=0;};"));
    ASSERT_EQ(TermID::CREATE_NAME, ast->getTermID()) << trust::toString(ast->getTermID());
    ASSERT_EQ("мин(arg) := {$00 := 0;};", ast->toString());
}

TEST_F(ParserTest, FunctionRussian2) {
    ASSERT_TRUE(Parse("мин(арг) := {$1:=0;};"));
    ASSERT_EQ(TermID::CREATE_NAME, ast->getTermID()) << trust::toString(ast->getTermID());
    ASSERT_EQ("мин(арг) := {$1 := 0;};", ast->toString());
}

TEST_F(ParserTest, FunctionRussian3) {
    ASSERT_TRUE(Parse("русс(10,20);"));
    ASSERT_EQ(TermID::NAME, ast->getTermID()) << trust::toString(ast->getTermID());
    ASSERT_EQ("русс(10, 20)", ast->toString());
}

TEST_F(ParserTest, FunctionRussian4) {
    ASSERT_TRUE(Parse("мин(10,20);"));
    ASSERT_EQ(TermID::NAME, ast->getTermID()) << trust::toString(ast->getTermID());
    ASSERT_EQ("мин(10, 20)", ast->toString());
}

TEST_F(ParserTest, FunctionArgs) {
    ASSERT_TRUE(Parse("мин(...) := {$1:=0;};"));
    ASSERT_EQ(TermID::CREATE_NAME, ast->getTermID()) << trust::toString(ast->getTermID());
    ASSERT_EQ("мин(...) := {$1 := 0;};", ast->toString());
}

TEST_F(ParserTest, FunctionArgs2) {
    ASSERT_TRUE(Parse("мин(arg, ...) := {$1:=0;};"));
    ASSERT_EQ(TermID::CREATE_NAME, ast->getTermID()) << trust::toString(ast->getTermID());
    ASSERT_EQ("мин(arg, ...) := {$1 := 0;};", ast->toString());
}

TEST_F(ParserTest, FunctionArgs3) {
    ASSERT_TRUE(Parse("мин(arg1, arg2, ...) := {$0:=0;};"));
    ASSERT_EQ(TermID::CREATE_NAME, ast->getTermID()) << trust::toString(ast->getTermID());
    ASSERT_EQ("мин(arg1, arg2, ...) := {$0 := 0;};", ast->toString());
}

TEST_F(ParserTest, FunctionKwArgs1) {
    ASSERT_TRUE(Parse("мин(...) := {$0:=0;func();var;};"));
    ASSERT_EQ(TermID::CREATE_NAME, ast->getTermID()) << trust::toString(ast->getTermID());
    ASSERT_EQ("мин(...) := {$0 := 0; func(); var;};", ast->toString());
}

TEST_F(ParserTest, FunctionKwArgs2) {
    ASSERT_TRUE(Parse("мин(arg=123 ,  ...) := {$0:=0;};"));
    ASSERT_EQ(TermID::CREATE_NAME, ast->getTermID()) << trust::toString(ast->getTermID());
    ASSERT_EQ("мин(arg=123, ...) := {$0 := 0;};", ast->toString());
}

TEST_F(ParserTest, FunctionKwArgs3) {
    ASSERT_TRUE(Parse("мин(arg1=1, arg2=2 ,...) := {$0:=0;};"));
    ASSERT_EQ(TermID::CREATE_NAME, ast->getTermID()) << trust::toString(ast->getTermID());
    ASSERT_EQ("мин(arg1=1, arg2=2, ...) := {$0 := 0;};", ast->toString());
}

TEST_F(ParserTest, FunctionArgsAll) {
    ASSERT_TRUE(Parse("мин(arg1=1, arg2=2 , ...) := {$0:=0;};"));
    ASSERT_EQ(TermID::CREATE_NAME, ast->getTermID()) << trust::toString(ast->getTermID());
    ASSERT_EQ("мин(arg1=1, arg2=2, ...) := {$0 := 0;};", ast->toString());
}

TEST_F(ParserTest, FunctionArgsAll2) {
    ASSERT_TRUE(Parse("мин(arg, arg1=1, arg2=2, ...) := {$0:=0;};"));
    ASSERT_EQ(TermID::CREATE_NAME, ast->getTermID()) << trust::toString(ast->getTermID());
    ASSERT_EQ("мин(arg, arg1=1, arg2=2, ...) := {$0 := 0;};", ast->toString());
}

TEST_F(ParserTest, FunctionEmpty) {
    ASSERT_TRUE(Parse("мин(arg, arg1=1, arg2=2, ...) := {};"));
    ASSERT_EQ("мин(arg, arg1=1, arg2=2, ...) := {};", ast->toString());
}

TEST_F(ParserTest, FunctionArgsFail) {
    m_ctx.diag().clear();
    EXPECT_NO_THROW(Parse("мин(... ...) := {$0:=0;};"));
    EXPECT_GT(m_ctx.diag().errorCount(), 0);
    m_ctx.diag().clear();
    EXPECT_NO_THROW(Parse("мин(arg ...) := {$0:=0;};"));
    EXPECT_GT(m_ctx.diag().errorCount(), 0);
    m_ctx.diag().clear();
    EXPECT_NO_THROW(Parse("мин(arg=1 ..., arg) := {$0:=0;};"));
    EXPECT_GT(m_ctx.diag().errorCount(), 0);
    m_ctx.diag().clear();
    EXPECT_NO_THROW(Parse("мин(arg=1, arg ...) := {$0:=0;};"));
    EXPECT_GT(m_ctx.diag().errorCount(), 0);
    m_ctx.diag().clear();
    EXPECT_NO_THROW(Parse("мин(arg=1 ...) := {$0:=0;};"));
    EXPECT_GT(m_ctx.diag().errorCount(), 0);
}

TEST_F(ParserTest, ArrayAdd7) {
    ASSERT_TRUE(Parse("name()  :=  term2;")); // $[].name:=term2;
    ASSERT_EQ(TermID::CREATE_NAME, ast->getTermID()) << trust::toString(ast->getTermID());
    ASSERT_EQ("name() := term2;", ast->toString());
}

TEST_F(ParserTest, Ellipsis0) {
    ASSERT_TRUE(Parse("... = _;")); //
    ASSERT_EQ("...=_;", ast->toString());

    ASSERT_TRUE(Parse("... = name;")); //
    ASSERT_EQ("...=name;", ast->toString());

    ASSERT_TRUE(Parse("... = name, name2::, ::na::name3;")); //
    ASSERT_EQ("...=name,name2::,::na::name3;", ast->toString());
}

TEST_F(ParserTest, Ellipsis1) {
    ASSERT_TRUE(Parse("name  :=  term2(arg1  ,  ...    ...    dict);")); //
    ASSERT_EQ(TermID::CREATE_NAME, ast->getTermID()) << trust::toString(ast->getTermID());
    ASSERT_EQ("name := term2(arg1, ... ...dict);", ast->toString());
}

// TEST_F(ParserTest, DISABLED_Complex1) {
//     ASSERT_TRUE(Parse("10+20j"));
//     ASSERT_EQ(TermID::COMPLEX, ast->getTermID()) << trust::toString(ast->getTermID());
//     ASSERT_EQ("10+20j;", ast->toString());
// }
//
// TEST_F(ParserTest, DISABLED_Complex2) {
//     ASSERT_TRUE(Parse("0j"));
//     ASSERT_EQ(TermID::COMPLEX, ast->getTermID()) << trust::toString(ast->getTermID());
//     ASSERT_EQ("0j;", ast->toString());
// }
//
// TEST_F(ParserTest, DISABLED_Complex3) {
//     ASSERT_TRUE(Parse("0.1-0.20j"));
//     ASSERT_EQ(TermID::COMPLEX, ast->getTermID()) << trust::toString(ast->getTermID());
//     ASSERT_EQ("0.1-0.20j;", ast->toString());
// }

TEST_F(ParserTest, Rational) {
    ASSERT_TRUE(Parse("1\\1"));
    ASSERT_EQ(TermID::RATIONAL, ast->getTermID()) << trust::toString(ast->getTermID());
    ASSERT_EQ("1\\1", ast->toString());
}

TEST_F(ParserTest, Rational2) {
    ASSERT_TRUE(Parse("1\\-20"));
    ASSERT_EQ(TermID::RATIONAL, ast->getTermID()) << trust::toString(ast->getTermID());
    ASSERT_EQ("1\\-20", ast->toString());
}

TEST_F(ParserTest, Rational3) {
    ASSERT_TRUE(Parse("-3\\11"));
    ASSERT_EQ(TermID::RATIONAL, ast->getTermID()) << trust::toString(ast->getTermID());
    ASSERT_EQ("-3\\11", ast->toString());
}

TEST_F(ParserTest, ArrayAdd9) {
    ASSERT_TRUE(Parse("$name  :=  term2"));
    ASSERT_EQ(TermID::CREATE_NAME, ast->getTermID()) << trust::toString(ast->getTermID());
    ASSERT_EQ("$name := term2;", ast->toString());
}

TEST_F(ParserTest, Ellipsis2) {
    ASSERT_TRUE(Parse("\\name  :=  term2(   ...   arg);"));
    ASSERT_EQ(TermID::CREATE_NAME, ast->getTermID()) << trust::toString(ast->getTermID());
    ASSERT_EQ("\\name := term2(...arg);", ast->toString());
}

TEST_F(ParserTest, Func1) {
    ASSERT_TRUE(Parse("func_arg(arg1 :Int8, arg2) :Int8 := { $arg1+$arg2; };"));
    ASSERT_EQ(TermID::CREATE_NAME, ast->getTermID()) << trust::toString(ast->getTermID());
    ASSERT_EQ("func_arg(arg1:Int8, arg2):Int8 := {$arg1 + $arg2;};", ast->toString());
}

TEST_F(ParserTest, Func2) {
    ASSERT_TRUE(Parse("func_arg(arg1:&Int8, &arg2) :&Int8 := { $arg1+$arg2; };"));
    ASSERT_EQ(TermID::CREATE_NAME, ast->getTermID()) << trust::toString(ast->getTermID());
    ASSERT_EQ("func_arg(arg1:&Int8, &arg2):&Int8 := {$arg1 + $arg2;};", ast->toString());
}

TEST_F(ParserTest, Func3) {
    ASSERT_TRUE(Parse("$res:Int8 ::= func_arg(100, 100);"));
    ASSERT_EQ(TermID::CREATE_TYPE, ast->getTermID()) << trust::toString(ast->getTermID());
    ASSERT_EQ("$res:Int8 ::= func_arg(100, 100);", ast->toString());
}

TEST_F(ParserTest, Func4) {
    ASSERT_TRUE(Parse("res() := func_arg(100, 100); res() := func_arg(100, 100); res() := func_arg(100, 100);"));
}
