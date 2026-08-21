#include "utils/io.hpp"
#include "semantic/pass_runner.hpp"
#include "semantic/name_resolution.hpp"
#include "semantic/inline_hook.hpp"
#include "semantic/symbol_table.hpp"
#include "ast/ast_nodes.hpp"
#include "ast/attr_builtin.hpp"
#include "ast/ident_name.hpp"
#include "ast/token.hpp"
#include "ast/token_type.hpp"
#include "diag/context.hpp"
#include "diag/diag.hpp"
#include "types/registry.hpp"
#include "types/type_names.hpp"
#include "syntax/term.h"
#include "syntax/term_types.h"
#include "gtest/gtest.h"
#include <sstream>
#include <string>
#include <vector>
#include <set>

namespace trust {
namespace {

class ErrsFixture : public ::testing::Test {
  protected:
    void SetUp() override {
        m_stream.str("");
        m_prev_err = setErrs(&m_stream);
        m_types = std::make_unique<TypeRegistry>(m_ctx.diag(), m_ctx.opts());
        m_ctx.setTypes(m_types.get());
    }

    void TearDown() override { setErrs(m_prev_err); }

    std::ostream* m_prev_err = nullptr;
    std::ostringstream m_stream;
    Context m_ctx;
    std::unique_ptr<TypeRegistry> m_types;
};

// -- Variable tests ---------------------------------------

class SemanticTest : public ErrsFixture {};

TEST_F(SemanticTest, VarDeclSimple) {
    // x := 42
    const std::string name = "x";
    auto term = Term::Create(TermID::NAME, name, {}, parser::token_type::NAME);
    auto var = std::make_shared<VarDecl>(std::move(term), nullptr, std::make_shared<Literal>(ParserToken::Kind::IntLiteral, "42"));

    std::vector<AstNodePtr> seq;
    seq.push_back(std::move(var));

    SemanticPassRunner runner(m_ctx);
    bool ok = runner.run(seq);

    EXPECT_TRUE(ok);
    EXPECT_EQ(runner.analysis().symbols().globalSize(), 1);
    auto* sym = runner.analysis().symbols().resolve("x");
    ASSERT_NE(sym, nullptr);
    EXPECT_EQ(sym->name, "x");
    ASSERT_NE(sym->decl, nullptr);
    EXPECT_EQ(sym->decl->kind(), ParserToken::Kind::VarDecl);
    EXPECT_TRUE(static_cast<const VarDecl&>(*sym->decl).m_initializer != nullptr);
}

// @[reftype("ptr")] перед объявлением переменной с аннотацией типа устанавливает вид ссылки
// (RefType) на тип переменной - fast-path бит (первая ссылка на тип без признака).
TEST_F(SemanticTest, VarDeclReftypeSetsRefType) {
    MapperFile input_file = m_ctx.source().add_source("reftype.src", "x:Int32 := 42;", true);
    MapperRange nameRange(m_ctx.source().makeLoc(input_file, 1), m_ctx.source().makeLoc(input_file, 2));
    auto nameTerm = Term::Create(TermID::NAME, "x", nameRange, parser::token_type::NAME);
    auto var =
        std::make_shared<VarDecl>(std::move(nameTerm), std::make_shared<IdentType>("Int32"), std::make_shared<Literal>(ParserToken::Kind::IntLiteral, "42"));

    auto rid = m_ctx.attrs().lookup(attr::Reftype);
    ASSERT_TRUE(rid.has_value());
    var->add_attr(*rid);
    var->set_attr_args(*rid, {"ptr"});

    std::vector<AstNodePtr> seq;
    seq.push_back(std::move(var));

    SemanticPassRunner runner(m_ctx);
    ASSERT_TRUE(runner.run(seq));

    auto* sym = runner.analysis().symbols().resolve("x");
    ASSERT_NE(sym, nullptr);
    ASSERT_NE(sym->type, INVALID_TYPE_ID);
    EXPECT_EQ(getRefType(getKindFromId(sym->type)), RefType::kPtr);
}

// Проверка утверждений: runtime-символы (trust::trust__abort__ / trust::formatMessage)
// зарегистрированы в TypeRegistry - это база для распознавания их вызовов
// (%trust::trust__abort__ и т.п.) как известных нативных функций (не «undefined name»).
TEST_F(SemanticTest, RuntimeSymbolsRegistered) {
    std::set<std::string> names;
    for (const auto& rs : m_ctx.types().runtimeSymbols()) {
        names.insert(rs.symbol);
    }
    EXPECT_NE(names.find("trust::trust__abort__"), names.end());
    EXPECT_NE(names.find("trust::formatMessage"), names.end());
}

// Диапазон `start..stop`: элементный тип - join типов операндов (Int+Int → Int64); тип
// выражения/переменной - ПАРАМЕТРИЗОВАННЫЙ структурный `Range<Int64>` (не абстрактный `:Range`).
TEST_F(SemanticTest, RangeExprElementTypeJoin) {
    auto rng = std::make_shared<RangeExpr>(ParserToken::Kind::RangeExpr, std::make_shared<Literal>(ParserToken::Kind::IntLiteral, "1"),
                                           std::make_shared<Literal>(ParserToken::Kind::IntLiteral, "10"));
    auto nameTerm = Term::Create(TermID::NAME, "r", {}, parser::token_type::NAME);
    auto var = std::make_shared<VarDecl>(std::move(nameTerm), nullptr, rng);
    std::vector<AstNodePtr> seq;
    seq.push_back(std::move(var));

    SemanticPassRunner runner(m_ctx);
    ASSERT_TRUE(runner.run(seq));

    // Элементный тип диапазона - Int64 (join двух Int-литералов).
    EXPECT_EQ(rng->elementType, m_ctx.types().getType(type::Int64));
    // Тип переменной - структурный Range<Int64> (не абстрактный :Range).
    auto* sym = runner.analysis().symbols().resolve("r");
    ASSERT_NE(sym, nullptr);
    const TypeId expected = m_ctx.types().getOrCreateRangeType(m_ctx.types().getType(type::Int64));
    EXPECT_EQ(m_ctx.types().getCanonicalTypeId(sym->type), expected);
    EXPECT_TRUE(m_ctx.types().isRangeType(expected));
    EXPECT_EQ(m_ctx.types().rangeElementType(expected), m_ctx.types().getType(type::Int64));
}

TEST_F(SemanticTest, DupDeclError) {
    // x := 1; x := 2;
    const std::string name1 = "x";
    auto t1 = Term::Create(TermID::NAME, name1, {}, parser::token_type::NAME);
    auto a1 = std::make_shared<VarDecl>(std::move(t1), nullptr, std::make_shared<Literal>(ParserToken::Kind::IntLiteral, "1"));

    const std::string name2 = "x";
    auto t2 = Term::Create(TermID::NAME, name2, {}, parser::token_type::NAME);
    auto a2 = std::make_shared<VarDecl>(std::move(t2), nullptr, std::make_shared<Literal>(ParserToken::Kind::IntLiteral, "2"));

    std::vector<AstNodePtr> seq;
    seq.push_back(std::move(a1));
    seq.push_back(std::move(a2));

    SemanticPassRunner runner(m_ctx);
    bool ok = runner.run(seq);

    EXPECT_FALSE(ok);
    EXPECT_GT(m_ctx.diag().errorCount(), 0);
}

TEST_F(SemanticTest, UndefinedNameRef) {
    // y := z; - error: undefined name
    const std::string name = "y";
    auto term = Term::Create(TermID::NAME, name, {}, parser::token_type::NAME);

    const std::string refName = "z";
    auto refTerm = Term::Create(TermID::NAME, refName, {}, parser::token_type::NAME);
    auto ref = std::make_shared<IdentName>(std::move(refTerm));

    auto var = std::make_shared<VarDecl>(std::move(term), nullptr, std::move(ref));

    std::vector<AstNodePtr> seq;
    seq.push_back(std::move(var));

    SemanticPassRunner runner(m_ctx);
    bool ok = runner.run(seq);

    EXPECT_FALSE(ok);
    EXPECT_GT(m_ctx.diag().errorCount(), 0);
}

TEST_F(SemanticTest, UseAfterDecl) {
    // a := 1; b := a;
    const std::string nameA = "a";
    auto tA = Term::Create(TermID::NAME, nameA, {}, parser::token_type::NAME);
    auto a1 = std::make_shared<VarDecl>(std::move(tA), nullptr, std::make_shared<Literal>(ParserToken::Kind::IntLiteral, "1"));

    const std::string nameB = "b";
    auto tB = Term::Create(TermID::NAME, nameB, {}, parser::token_type::NAME);
    auto a2 = std::make_shared<VarDecl>(std::move(tB), nullptr, std::make_shared<IdentName>("a"));

    std::vector<AstNodePtr> seq;
    seq.push_back(std::move(a1));
    seq.push_back(std::move(a2));

    SemanticPassRunner runner(m_ctx);
    bool ok = runner.run(seq);

    EXPECT_TRUE(ok);
    EXPECT_EQ(runner.analysis().symbols().globalSize(), 2);
}

TEST_F(SemanticTest, LiteralStandalone) {
    auto litTerm = Term::Create(TermID::INTEGER, "42", {}, parser::token_type::END);

    std::vector<AstNodePtr> seq;
    seq.push_back(std::make_shared<Literal>(ParserToken::Kind::IntLiteral, std::move(litTerm)));

    SemanticPassRunner runner(m_ctx);
    bool ok = runner.run(seq);

    EXPECT_TRUE(ok);
}

// -- SymbolTable tests ------------------------------------
// Единая таблица символов: стек вложенных скоупов. Создаётся без DiagnosticEngine -
// диагностику дубликатов формирует ядро (ему нужен range).

class SymbolTableTest : public ErrsFixture {};

// Helper: Color ::= :Enum(RED=1, GREEN=2,) - TypeDecl(left=Ident, right=DictLiteral с аннотацией «Enum»).
static std::shared_ptr<Binary> makeEnumTypeDecl(const char* name, std::initializer_list<std::pair<const char*, const char*>> ms) {
    auto dict = std::make_shared<DictLiteralNode>(ParserToken::Kind::DictLiteral, std::string(""));
    dict->m_type = std::make_shared<IdentType>(std::string("Enum"));
    for (const auto& [mname, mval] : ms) {
        dict->m_body.push_back(
            std::make_shared<ArgNode>(std::string(mname), nullptr, std::make_shared<Literal>(ParserToken::Kind::IntLiteral, std::string(mval))));
    }
    return std::make_shared<Binary>(ParserToken::Kind::TypeDecl, std::make_shared<IdentName>(std::string(name)), std::move(dict));
}

TEST_F(SemanticTest, EnumDeclRegistersType) {
    // Color ::= :Enum(RED=1, GREEN=2,) → TypeDecl+DictLiteral(аннотация Enum).
    auto enumDecl = makeEnumTypeDecl("Color", {{"RED", "1"}, {"GREEN", "2"}});

    std::vector<AstNodePtr> seq;
    seq.push_back(std::move(enumDecl));

    SemanticPassRunner runner(m_ctx);
    bool ok = runner.run(seq);
    EXPECT_TRUE(ok);
    EXPECT_EQ(m_ctx.diag().errorCount(), 0);

    auto tid = m_types->findType("Color");
    ASSERT_TRUE(tid.has_value());
    EXPECT_TRUE(isEnumType(*tid, *m_types));
    const auto* ed = m_types->getTypeDataAs<EnumTypeData>(*tid);
    ASSERT_NE(ed, nullptr);
    EXPECT_EQ(ed->members.size(), 2u);
    EXPECT_EQ(ed->members[0].name, "RED");
    EXPECT_EQ(ed->members[1].name, "GREEN");
    // Тип значений по СТАНДАРТНЫМ правилам: 1→Bool, 2→Int8 → join → Int64.
    EXPECT_EQ(m_types->getCanonicalTypeId(ed->valueType), m_types->getType(type::Int64));
    // Классические методы зарегистрированы (резолвятся семантикой): count/fromName/fromValue.
    EXPECT_NE(m_types->findMethod(*tid, "count"), INVALID_TYPE_ID);
    EXPECT_NE(m_types->findMethod(*tid, "fromName"), INVALID_TYPE_ID);
    EXPECT_NE(m_types->findMethod(*tid, "fromValue"), INVALID_TYPE_ID);
}

TEST_F(SemanticTest, EnumMemberAccessResolvesToEnumType) {
    // Color.RED - значение типа Color (работа только через имя типа); несуществующий член → ошибка.
    auto enumDecl = makeEnumTypeDecl("Color", {{"RED", "1"}, {"GREEN", "2"}});

    auto memberRef = [](const std::string& mname) {
        auto left = std::make_shared<IdentName>(std::string("Color"));
        auto right = std::make_shared<IdentName>(mname);
        return std::make_shared<Binary>(ParserToken::Kind::MemberAccess, std::move(left), std::move(right));
    };

    // 1) b : Color := Color.RED - допустимо, ошибок нет.
    {
        std::vector<AstNodePtr> seq;
        seq.push_back(std::make_shared<Binary>(*enumDecl));
        auto tname = std::make_shared<IdentType>(std::string("Color"));
        seq.push_back(std::make_shared<VarDecl>("b", std::move(tname), memberRef("RED")));
        m_ctx.diag().clear();
        SemanticPassRunner runner(m_ctx);
        EXPECT_TRUE(runner.run(seq));
        EXPECT_EQ(m_ctx.diag().errorCount(), 0);
    }
    // 2) b : Color := Color.BLUE - несуществующий член → ошибка «enum has no member».
    {
        m_ctx.diag().clear();
        std::vector<AstNodePtr> seq;
        seq.push_back(std::make_shared<Binary>(*enumDecl));
        auto tname = std::make_shared<IdentType>(std::string("Color"));
        seq.push_back(std::make_shared<VarDecl>("b", std::move(tname), memberRef("BLUE")));
        SemanticPassRunner runner(m_ctx);
        EXPECT_FALSE(runner.run(seq));
        EXPECT_GT(m_ctx.diag().errorCount(), 0);
    }
}

TEST_F(SemanticTest, VariantDeclRegistersType) {
    // Value ::= :Variant(RED=5, GREEN='g',) → TypeDecl+DictLiteral(аннотация Variant).
    // Каждый член имеет СВОЙ тип (Int8 и StrChar) - гетерогенный вариант.
    auto dict = std::make_shared<DictLiteralNode>(ParserToken::Kind::DictLiteral, std::string(""));
    dict->m_type = std::make_shared<IdentType>(std::string("Variant"));
    dict->m_body.push_back(std::make_shared<ArgNode>(std::string("RED"), nullptr, std::make_shared<Literal>(ParserToken::Kind::IntLiteral, "5")));
    dict->m_body.push_back(std::make_shared<ArgNode>(std::string("GREEN"), nullptr, std::make_shared<Literal>(ParserToken::Kind::StrChar, "g")));
    auto variantDecl = std::make_shared<Binary>(ParserToken::Kind::TypeDecl, std::make_shared<IdentName>(std::string("Value")), std::move(dict));

    std::vector<AstNodePtr> seq;
    seq.push_back(std::move(variantDecl));

    SemanticPassRunner runner(m_ctx);
    EXPECT_TRUE(runner.run(seq));
    EXPECT_EQ(m_ctx.diag().errorCount(), 0);

    auto tid = m_types->findType("Value");
    ASSERT_TRUE(tid.has_value());
    EXPECT_TRUE(isVariantType(*tid, *m_types));
    const auto* vd = m_types->getTypeDataAs<VariantTypeData>(*tid);
    ASSERT_NE(vd, nullptr);
    ASSERT_EQ(vd->members.size(), 2u);
    EXPECT_EQ(vd->members[0].name, "RED");
    EXPECT_EQ(m_types->getCanonicalTypeId(vd->members[0].type), m_types->getType(type::Int8));
    EXPECT_EQ(vd->members[1].name, "GREEN");
    EXPECT_EQ(m_types->getCanonicalTypeId(vd->members[1].type), m_types->getType(type::StrChar));
}

TEST_F(SemanticTest, VariantUnknownMemberTypeReportsError) {
    // Value ::= :Variant(OK=5, BAD:NonExistentType,) - явный тип члена не резолвится → ОШИБКА
    // «unknown member type» (симметрично enum), а не тихий fallback на тип из значения/ординал.
    auto dict = std::make_shared<DictLiteralNode>(ParserToken::Kind::DictLiteral, std::string(""));
    dict->m_type = std::make_shared<IdentType>(std::string("Variant"));
    dict->m_body.push_back(std::make_shared<ArgNode>(std::string("OK"), nullptr, std::make_shared<Literal>(ParserToken::Kind::IntLiteral, "5")));
    // Явный тип `BAD:NonExistentType` - тип в ArgNode.m_type (без обёртки значения).
    dict->m_body.push_back(std::make_shared<ArgNode>(std::string("BAD"), std::make_shared<IdentType>(std::string("NonExistentType")), nullptr));
    auto variantDecl = std::make_shared<Binary>(ParserToken::Kind::TypeDecl, std::make_shared<IdentName>(std::string("Value")), std::move(dict));

    std::vector<AstNodePtr> seq;
    seq.push_back(std::move(variantDecl));

    SemanticPassRunner runner(m_ctx);
    EXPECT_FALSE(runner.run(seq));
    EXPECT_GT(m_ctx.diag().errorCount(), 0);
}

TEST_F(SemanticTest, EnumTypedAndBareMembersViaArgNode) {
    // Flag ::= (LOW:Int8, HIGH,):Enum - явный тип члена (ArgNode.m_type) + безнарный член
    // (имя в value-Ident). Проверяет каноническое чтение членов из ArgNode (enumVariantMember).
    auto dict = std::make_shared<DictLiteralNode>(ParserToken::Kind::DictLiteral, std::string(""));
    dict->m_type = std::make_shared<IdentType>(std::string("Enum"));
    // LOW:Int8 - безнарный типизированный член: имя в value-Ident, тип в m_type.
    dict->m_body.push_back(
        std::make_shared<ArgNode>(std::string(""), std::make_shared<IdentType>(std::string("Int8")), std::make_shared<IdentName>(std::string("LOW"))));
    // HIGH - безнарный член: имя в value-Ident, без типа/значения.
    dict->m_body.push_back(std::make_shared<ArgNode>(std::string(""), nullptr, std::make_shared<IdentName>(std::string("HIGH"))));
    auto enumDecl = std::make_shared<Binary>(ParserToken::Kind::TypeDecl, std::make_shared<IdentName>(std::string("Flag")), std::move(dict));

    std::vector<AstNodePtr> seq;
    seq.push_back(std::move(enumDecl));

    SemanticPassRunner runner(m_ctx);
    EXPECT_TRUE(runner.run(seq));
    EXPECT_EQ(m_ctx.diag().errorCount(), 0);

    auto tid = m_types->findType("Flag");
    ASSERT_TRUE(tid.has_value());
    EXPECT_TRUE(isEnumType(*tid, *m_types));
    const auto* ed = m_types->getTypeDataAs<EnumTypeData>(*tid);
    ASSERT_NE(ed, nullptr);
    ASSERT_EQ(ed->members.size(), 2u);
    EXPECT_EQ(ed->members[0].name, "LOW");
    EXPECT_EQ(ed->members[1].name, "HIGH");
    // Явный тип члена LOW:Int8 задаёт единый тип значений.
    EXPECT_EQ(m_types->getCanonicalTypeId(ed->valueType), m_types->getType(type::Int8));
}

TEST_F(SymbolTableTest, DeclareResolveGlobal) {
    SymbolTable symtab;
    Symbol sym;
    sym.name = "x";
    auto node = std::make_shared<VarDecl>("x");
    sym.decl = node.get();

    EXPECT_TRUE(symtab.declare(sym));
    EXPECT_EQ(symtab.globalSize(), 1u);
    EXPECT_NE(symtab.resolve("x"), nullptr);
}

TEST_F(SymbolTableTest, DupRejectedInScope) {
    SymbolTable symtab;
    Symbol s1, s2;
    s1.name = "x";
    s2.name = "x";
    auto n1 = std::make_shared<VarDecl>("x");
    auto n2 = std::make_shared<VarDecl>("x");
    s1.decl = n1.get();
    s2.decl = n2.get();

    EXPECT_TRUE(symtab.declare(s1));
    EXPECT_FALSE(symtab.declare(s2)); // дубликат в том же скоупе
}

TEST_F(SymbolTableTest, ResolveNotFound) {
    SymbolTable symtab;
    EXPECT_EQ(symtab.resolve("nonexistent"), nullptr);
}

// -- Function forward declaration tests -------------------

class FuncDeclTest : public ErrsFixture {};

TEST_F(FuncDeclTest, ForwardDeclNoReturn) {
    // func(arg:Int32) := ... ;
    const std::string funcName = "func";
    auto funcTerm = Term::Create(TermID::NAME, funcName, {}, parser::token_type::NAME);
    auto func = std::make_shared<FuncDecl>(std::move(funcTerm));
    func->m_params = std::vector<AstNodePtr>{};

    auto paramType = std::make_shared<IdentType>("Int32");
    func->m_params->push_back(std::make_shared<ArgNode>("arg", paramType));

    std::vector<AstNodePtr> seq;
    seq.push_back(std::move(func));

    SemanticPassRunner runner(m_ctx);
    bool ok = runner.run(seq);

    EXPECT_TRUE(ok);
    EXPECT_EQ(runner.analysis().symbols().globalSize(), 1);

    auto* sym = runner.analysis().symbols().resolve("func");
    ASSERT_NE(sym, nullptr);
    EXPECT_EQ(sym->name, "func");
}

TEST_F(FuncDeclTest, ForwardDeclWithReturn) {
    // func(arg:Int32):Int32 := ... ;
    const std::string funcName = "func";
    auto funcTerm = Term::Create(TermID::NAME, funcName, {}, parser::token_type::NAME);
    auto func = std::make_shared<FuncDecl>(std::move(funcTerm));
    func->m_params = std::vector<AstNodePtr>{};

    auto paramType = std::make_shared<IdentType>("Int32");
    func->m_params->push_back(std::make_shared<ArgNode>("arg", paramType));
    func->m_type = std::make_shared<IdentType>("Int32");

    std::vector<AstNodePtr> seq;
    seq.push_back(std::move(func));

    SemanticPassRunner runner(m_ctx);
    bool ok = runner.run(seq);

    EXPECT_TRUE(ok);
    EXPECT_EQ(runner.analysis().symbols().globalSize(), 1);

    auto* sym = runner.analysis().symbols().resolve("func");
    ASSERT_NE(sym, nullptr);
}

TEST_F(FuncDeclTest, ForwardDeclMultipleParams) {
    // func(a:Int8, b:String) := ... ;
    const std::string funcName = "func";
    auto funcTerm = Term::Create(TermID::NAME, funcName, {}, parser::token_type::NAME);
    auto func = std::make_shared<FuncDecl>(std::move(funcTerm));
    func->m_params = std::vector<AstNodePtr>{};

    auto paType = std::make_shared<IdentType>("Int8");
    func->m_params->push_back(std::make_shared<ArgNode>("a", paType));
    auto pbType = std::make_shared<IdentType>("String");
    func->m_params->push_back(std::make_shared<ArgNode>("b", pbType));

    std::vector<AstNodePtr> seq;
    seq.push_back(std::move(func));

    SemanticPassRunner runner(m_ctx);
    bool ok = runner.run(seq);

    EXPECT_TRUE(ok);
    EXPECT_EQ(runner.analysis().symbols().globalSize(), 1);

    auto* sym = runner.analysis().symbols().resolve("func");
    ASSERT_NE(sym, nullptr);
}

TEST_F(FuncDeclTest, ForwardDeclNoParams) {
    // func() := ... ;
    const std::string funcName = "func";
    auto funcTerm = Term::Create(TermID::NAME, funcName, {}, parser::token_type::NAME);
    auto func = std::make_shared<FuncDecl>(std::move(funcTerm));
    // no m_params / m_body = forward declaration

    std::vector<AstNodePtr> seq;
    seq.push_back(std::move(func));

    SemanticPassRunner runner(m_ctx);
    bool ok = runner.run(seq);

    EXPECT_TRUE(ok);
    EXPECT_EQ(runner.analysis().symbols().globalSize(), 1);

    auto* sym = runner.analysis().symbols().resolve("func");
    ASSERT_NE(sym, nullptr);
}

TEST_F(FuncDeclTest, DuplicateFuncName) {
    // func(x:Int32) := ... ; func(y:Int32) := ... ;
    const std::string f1Name = "func";
    auto f1Term = Term::Create(TermID::NAME, f1Name, {}, parser::token_type::NAME);
    auto f1 = std::make_shared<FuncDecl>(std::move(f1Term));
    f1->m_params = std::vector<AstNodePtr>{};
    auto p1Type = std::make_shared<IdentType>("Int32");
    f1->m_params->push_back(std::make_shared<ArgNode>("x", p1Type));

    const std::string f2Name = "func";
    auto f2Term = Term::Create(TermID::NAME, f2Name, {}, parser::token_type::NAME);
    auto f2 = std::make_shared<FuncDecl>(std::move(f2Term));
    f2->m_params = std::vector<AstNodePtr>{};
    auto p2Type = std::make_shared<IdentType>("Int32");
    f2->m_params->push_back(std::make_shared<ArgNode>("y", p2Type));

    std::vector<AstNodePtr> seq;
    seq.push_back(std::move(f1));
    seq.push_back(std::move(f2));

    SemanticPassRunner runner(m_ctx);
    bool ok = runner.run(seq);

    EXPECT_FALSE(ok);
    EXPECT_GT(m_ctx.diag().errorCount(), 0);
}

TEST_F(FuncDeclTest, FuncAndVarSameName) {
    // func(x:Int32) := ... ; func := 42; - error: duplicate
    const std::string fName = "func";
    auto fTerm = Term::Create(TermID::NAME, fName, {}, parser::token_type::NAME);
    auto f1 = std::make_shared<FuncDecl>(std::move(fTerm));
    f1->m_params = std::vector<AstNodePtr>{};
    auto pType = std::make_shared<IdentType>("Int32");
    f1->m_params->push_back(std::make_shared<ArgNode>("x", pType));

    const std::string vName = "func";
    auto vTerm = Term::Create(TermID::NAME, vName, {}, parser::token_type::NAME);
    auto v1 = std::make_shared<VarDecl>(std::move(vTerm), nullptr, std::make_shared<Literal>(ParserToken::Kind::IntLiteral, "42"));

    std::vector<AstNodePtr> seq;
    seq.push_back(std::move(f1));
    seq.push_back(std::move(v1));

    SemanticPassRunner runner(m_ctx);
    bool ok = runner.run(seq);

    EXPECT_FALSE(ok);
    EXPECT_GT(m_ctx.diag().errorCount(), 0);
}

// -- Интеграция таблицы типов с анализом --

TEST_F(FuncDeclTest, BuildsFunctionType) {
    // func(arg:Int32):Bool := ... ; - строится FunctionTypeId сигнатуры.
    auto funcTerm = Term::Create(TermID::NAME, "func", {}, parser::token_type::NAME);
    auto func = std::make_shared<FuncDecl>(std::move(funcTerm));
    func->m_params = std::vector<AstNodePtr>{};
    func->m_params->push_back(std::make_shared<ArgNode>("arg", std::make_shared<IdentType>("Int32")));
    func->m_type = std::make_shared<IdentType>("Bool");

    std::vector<AstNodePtr> seq;
    seq.push_back(std::move(func));

    SemanticPassRunner runner(m_ctx);
    ASSERT_TRUE(runner.run(seq));

    auto* sym = runner.analysis().symbols().resolve("func");
    ASSERT_NE(sym, nullptr);
    EXPECT_NE(sym->type, INVALID_TYPE_ID);

    const auto* ft = m_ctx.types().getTypeDataAs<FunctionTypeData>(sym->type);
    ASSERT_NE(ft, nullptr);
    auto int32 = m_ctx.types().findType("Int32");
    auto bool_id = m_ctx.types().findType("Bool");
    ASSERT_TRUE(int32.has_value());
    ASSERT_TRUE(bool_id.has_value());
    EXPECT_EQ(ft->returnType, *bool_id);
    ASSERT_EQ(ft->paramTypes.size(), 1u);
    EXPECT_EQ(ft->paramTypes[0], *int32);
}

// Forward-объявление функции `%f(a:Int32):Int32 := ...;` - нативная функция с типом возврата
// регистрируется без ошибки (FunctionTypeId сигнатуры).
TEST_F(FuncDeclTest, NativeForwardFuncWithReturnType) {
    auto funcTerm = Term::Create(TermID::NAME, "%f", {}, parser::token_type::NAME);
    auto func = std::make_shared<FuncDecl>(std::move(funcTerm));
    func->m_params = std::vector<AstNodePtr>{};
    func->m_params->push_back(std::make_shared<ArgNode>("a", std::make_shared<IdentType>("Int32")));
    func->m_type = std::make_shared<IdentType>("Int32");

    std::vector<AstNodePtr> seq;
    seq.push_back(std::move(func));

    SemanticPassRunner runner(m_ctx);
    EXPECT_TRUE(runner.run(seq));
    auto* sym = runner.analysis().symbols().resolve("%f");
    ASSERT_NE(sym, nullptr);
    EXPECT_NE(sym->type, INVALID_TYPE_ID);
}

// Forward-объявление нативной функции `%f() := ...;` без типа возврата → ошибка (нативные имена
// транслируются в C++ напрямую, поэтому тип обязателен).
TEST_F(FuncDeclTest, NativeForwardFuncNoReturnTypeError) {
    auto funcTerm = Term::Create(TermID::NAME, "%f", {}, parser::token_type::NAME);
    auto func = std::make_shared<FuncDecl>(std::move(funcTerm));
    func->m_params = std::vector<AstNodePtr>{}; // no return type (m_type = nullptr)

    std::vector<AstNodePtr> seq;
    seq.push_back(std::move(func));

    SemanticPassRunner runner(m_ctx);
    EXPECT_FALSE(runner.run(seq));
    EXPECT_GT(m_ctx.diag().errorCount(), 0);
}

// Forward-объявление функции завершается определением того же имени в том же скоупе
// (declareOrComplete → Completed): не ошибка, у символа появляется тело.
TEST_F(FuncDeclTest, ForwardFuncCompletedByDefinition) {
    auto mk = [] {
        auto t = Term::Create(TermID::NAME, "%f", {}, parser::token_type::NAME);
        auto f = std::make_shared<FuncDecl>(std::move(t));
        f->m_params = std::vector<AstNodePtr>{};
        f->m_params->push_back(std::make_shared<ArgNode>("a", std::make_shared<IdentType>("Int32")));
        f->m_type = std::make_shared<IdentType>("Int32");
        return f;
    };
    auto fwd = mk();
    auto def = mk();
    auto retTerm = Term::Create(TermID::UNKNOWN, "++", {}, parser::token_type::END);
    auto ret = std::make_shared<JumpStmt>(ParserToken::Kind::ReturnStmt, std::move(retTerm));
    ret->m_value = std::make_shared<IdentName>("a");
    def->m_body = std::vector<AstNodePtr>{std::move(ret)};

    std::vector<AstNodePtr> seq;
    seq.push_back(std::move(fwd));
    seq.push_back(std::move(def));

    SemanticPassRunner runner(m_ctx);
    EXPECT_TRUE(runner.run(seq));
    auto* sym = runner.analysis().symbols().resolve("%f");
    ASSERT_NE(sym, nullptr);
    ASSERT_NE(sym->decl, nullptr);
    EXPECT_EQ(sym->decl->kind(), ParserToken::Kind::FuncDecl);
    EXPECT_TRUE(static_cast<const FuncDecl&>(*sym->decl).m_body.has_value()) << "определение должно заменить forward-объявление";
}

// Forward-объявление переменной `x:Int32 := ...;` (без инициализатора) - регистрируется в
// текущем скоупе без ошибки, тип берётся из аннотации.
TEST_F(SemanticTest, ForwardVarDecl) {
    auto var = std::make_shared<VarDecl>("x", std::make_shared<IdentType>("Int32"), nullptr);

    std::vector<AstNodePtr> seq;
    seq.push_back(std::move(var));

    SemanticPassRunner runner(m_ctx);
    EXPECT_TRUE(runner.run(seq));
    auto* sym = runner.analysis().symbols().resolve("x");
    ASSERT_NE(sym, nullptr);
    EXPECT_NE(sym->type, INVALID_TYPE_ID);
    ASSERT_NE(sym->decl, nullptr);
    EXPECT_EQ(sym->decl->kind(), ParserToken::Kind::VarDecl);
    EXPECT_FALSE(static_cast<const VarDecl&>(*sym->decl).m_initializer) << "forward-переменная без инициализатора";
}

// Forward-объявление ненативной переменной без типа `y := ...;` - тип опционален, ошибки нет.
TEST_F(SemanticTest, ForwardVarDeclNoType) {
    auto var = std::make_shared<VarDecl>("y", nullptr, nullptr);

    std::vector<AstNodePtr> seq;
    seq.push_back(std::move(var));

    SemanticPassRunner runner(m_ctx);
    EXPECT_TRUE(runner.run(seq));
    auto* sym = runner.analysis().symbols().resolve("y");
    ASSERT_NE(sym, nullptr);
}

// Forward-объявление нативной переменной `%x := ...;` без типа → ошибка (нативные имена
// транслируются в C++ напрямую, поэтому тип обязателен).
TEST_F(SemanticTest, NativeForwardVarNoTypeError) {
    auto var = std::make_shared<VarDecl>("%x", nullptr, nullptr);

    std::vector<AstNodePtr> seq;
    seq.push_back(std::move(var));

    SemanticPassRunner runner(m_ctx);
    EXPECT_FALSE(runner.run(seq));
    EXPECT_GT(m_ctx.diag().errorCount(), 0);
}

// Forward-объявление переменной завершается определением того же имени в том же скоупе
// (declareOrComplete → Completed): не ошибка, символ обновляется инициализатором.
TEST_F(SemanticTest, ForwardVarCompletedByDefinition) {
    auto fwd = std::make_shared<VarDecl>("x", std::make_shared<IdentType>("Int32"), nullptr);
    auto def = std::make_shared<VarDecl>("x", std::make_shared<IdentType>("Int32"), std::make_shared<Literal>(ParserToken::Kind::IntLiteral, "5"));

    std::vector<AstNodePtr> seq;
    seq.push_back(std::move(fwd));
    seq.push_back(std::move(def));

    SemanticPassRunner runner(m_ctx);
    EXPECT_TRUE(runner.run(seq));
    auto* sym = runner.analysis().symbols().resolve("x");
    ASSERT_NE(sym, nullptr);
    ASSERT_NE(sym->decl, nullptr);
    EXPECT_EQ(sym->decl->kind(), ParserToken::Kind::VarDecl);
    EXPECT_TRUE(static_cast<const VarDecl&>(*sym->decl).m_initializer != nullptr) << "определение должно заменить forward-объявление";
}

// Два определения переменной одного имени → ошибка duplicate.
TEST_F(SemanticTest, TwoVarDefinitionsError) {
    auto v1 = std::make_shared<VarDecl>("x", nullptr, std::make_shared<Literal>(ParserToken::Kind::IntLiteral, "1"));
    auto v2 = std::make_shared<VarDecl>("x", nullptr, std::make_shared<Literal>(ParserToken::Kind::IntLiteral, "2"));

    std::vector<AstNodePtr> seq;
    seq.push_back(std::move(v1));
    seq.push_back(std::move(v2));

    SemanticPassRunner runner(m_ctx);
    EXPECT_FALSE(runner.run(seq));
    EXPECT_GT(m_ctx.diag().errorCount(), 0);
}

// Определение раньше forward-объявления того же имени → ошибка duplicate (уже определено).
TEST_F(SemanticTest, DefinitionThenForwardError) {
    auto def = std::make_shared<VarDecl>("x", nullptr, std::make_shared<Literal>(ParserToken::Kind::IntLiteral, "1"));
    auto fwd = std::make_shared<VarDecl>("x", nullptr, nullptr);

    std::vector<AstNodePtr> seq;
    seq.push_back(std::move(def));
    seq.push_back(std::move(fwd));

    SemanticPassRunner runner(m_ctx);
    EXPECT_FALSE(runner.run(seq));
    EXPECT_GT(m_ctx.diag().errorCount(), 0);
}

TEST_F(SemanticTest, TypeAliasBoundInScopeAndResolvable) {
    // y ::= Int32;  x:y := 1; - алиас связан в скоуп-стеке и резолвится как тип переменной.
    auto opTerm = Term::Create(TermID::CREATE_TYPE, "::=", {}, parser::token_type::END);
    auto typeDecl = std::make_shared<Binary>(ParserToken::Kind::TypeDecl, std::move(opTerm));
    typeDecl->m_left = std::make_shared<IdentName>("y");
    typeDecl->m_right = std::make_shared<IdentType>("Int32");

    auto var = std::make_shared<VarDecl>("x", std::make_shared<IdentType>("y"), std::make_shared<Literal>(ParserToken::Kind::IntLiteral, "1"));

    std::vector<AstNodePtr> seq;
    seq.push_back(std::move(typeDecl));
    seq.push_back(std::move(var));

    SemanticPassRunner runner(m_ctx);
    ASSERT_TRUE(runner.run(seq));

    // Алиас зарегистрирован в реестре и связан в скоуп-стеке.
    auto y_id = m_ctx.types().findType("y");
    ASSERT_TRUE(y_id.has_value());
    const Symbol* ys = runner.analysis().symbols().resolve("y");
    ASSERT_NE(ys, nullptr);
    EXPECT_EQ(ys->type, *y_id);
    EXPECT_EQ(m_ctx.types().getCanonicalTypeId(*y_id), m_ctx.types().findType("Int32").value());

    // Переменная, аннотированная y, получила тип алиаса (единый резолв через скоуп).
    const Symbol* xs = runner.analysis().symbols().resolve("x");
    ASSERT_NE(xs, nullptr);
    EXPECT_EQ(xs->type, *y_id);
}

TEST_F(SemanticTest, TypeAliasCollidesWithVar) {
    // y ::= Int32; y := 1; - имя типа и переменной в одном скоупе → ошибка.
    auto opTerm = Term::Create(TermID::CREATE_TYPE, "::=", {}, parser::token_type::END);
    auto typeDecl = std::make_shared<Binary>(ParserToken::Kind::TypeDecl, std::move(opTerm));
    typeDecl->m_left = std::make_shared<IdentName>("y");
    typeDecl->m_right = std::make_shared<IdentType>("Int32");

    auto var = std::make_shared<VarDecl>("y", nullptr, std::make_shared<Literal>(ParserToken::Kind::IntLiteral, "1"));

    std::vector<AstNodePtr> seq;
    seq.push_back(std::move(typeDecl));
    seq.push_back(std::move(var));

    SemanticPassRunner runner(m_ctx);
    EXPECT_FALSE(runner.run(seq));
    EXPECT_GT(m_ctx.diag().errorCount(), 0);
}

TEST_F(SemanticTest, TypeRegistryResetPerRun) {
    // y ::= Int32; - первый run регистрирует алиас в реестре.
    auto opTerm = Term::Create(TermID::CREATE_TYPE, "::=", {}, parser::token_type::END);
    auto typeDecl = std::make_shared<Binary>(ParserToken::Kind::TypeDecl, std::move(opTerm));
    typeDecl->m_left = std::make_shared<IdentName>("y");
    typeDecl->m_right = std::make_shared<IdentType>("Int32");

    std::vector<AstNodePtr> seq;
    seq.push_back(std::move(typeDecl));

    SemanticPassRunner r1(m_ctx);
    ASSERT_TRUE(r1.run(seq));
    ASSERT_TRUE(m_ctx.types().findType("y").has_value());

    // Второй run сбрасывает реестр к builtin-состоянию (упорядоченный жизненный цикл).
    SemanticPassRunner r2(m_ctx);
    std::vector<AstNodePtr> empty;
    ASSERT_TRUE(r2.run(empty));
    EXPECT_FALSE(m_ctx.types().findType("y").has_value());
}

TEST_F(SemanticTest, LoweringWrapsSemicolonStmt) {
    // x += 3 → после analyze() обёрнут в SemicolonStmt: решение о ';' для statement-выражений
    // зашивается в AST анализатором (lowering), а не выводится транспилятором из контекста.
    MapperFile input_file = m_ctx.source().add_source("low.src", "x := 0; x += 3;", true);
    MapperRange range(m_ctx.source().makeLoc(input_file, 9), m_ctx.source().makeLoc(input_file, 16));

    auto opTerm = Term::Create(TermID::ASSIGN, "+=", range, parser::token_type::END);
    auto b = std::make_shared<Binary>(ParserToken::Kind::AssignOp, std::move(opTerm));
    b->m_left = std::make_shared<IdentName>("x");
    b->m_right = std::make_shared<Literal>(ParserToken::Kind::IntLiteral, "3");

    auto xTerm =
        Term::Create(TermID::NAME, "x", MapperRange(m_ctx.source().makeLoc(input_file, 1), m_ctx.source().makeLoc(input_file, 8)), parser::token_type::NAME);
    auto xVar = std::make_shared<VarDecl>(std::move(xTerm), nullptr, std::make_shared<Literal>(ParserToken::Kind::IntLiteral, "0"));
    std::vector<AstNodePtr> seq;
    seq.push_back(std::move(xVar));
    seq.push_back(std::move(b));

    SemanticPassRunner runner(m_ctx);
    ASSERT_TRUE(runner.run(seq));

    // После lowering: [VarDecl x, SemicolonStmt(AssignOp)] - statement-выражение обёрнуто в SemicolonStmt.
    ASSERT_EQ(seq.size(), 2u);
    ASSERT_EQ(seq[0]->kind(), ParserToken::Kind::VarDecl);
    ASSERT_EQ(seq[1]->kind(), ParserToken::Kind::SemicolonStmt);
    auto* es = static_cast<SemicolonStmt*>(seq[1].get());
    ASSERT_NE(es->m_expr, nullptr);
    EXPECT_EQ(es->m_expr->kind(), ParserToken::Kind::AssignOp);
}
// Именованные элементы словаря `(1, two=2, name='3',)` - метки полей, НЕ переменные:
// их имена не резолвятся как ссылки (нет «undefined name») и НЕ регистрируются
// в таблице символов (регистрация имён - только для аргументов функций).
TEST_F(SemanticTest, DictLiteralNamedElementsNotRegistered) {
    auto dictTerm = Term::Create(TermID::DICT, "", {}, parser::token_type::END);
    auto dict = std::make_shared<DictLiteralNode>(ParserToken::Kind::DictLiteral, std::move(dictTerm));
    dict->m_body.push_back(std::make_shared<Literal>(ParserToken::Kind::IntLiteral, "1"));
    dict->m_body.push_back(std::make_shared<ArgNode>(std::string("two"), nullptr, std::make_shared<Literal>(ParserToken::Kind::IntLiteral, "2")));
    dict->m_body.push_back(std::make_shared<ArgNode>(std::string("name"), nullptr, std::make_shared<Literal>(ParserToken::Kind::StrChar, "3")));

    auto nameTerm = Term::Create(TermID::NAME, "d", {}, parser::token_type::NAME);
    auto var = std::make_shared<VarDecl>(std::move(nameTerm), nullptr, std::move(dict));
    std::vector<AstNodePtr> seq;
    seq.push_back(std::move(var));

    SemanticPassRunner runner(m_ctx);
    ASSERT_TRUE(runner.run(seq));
    // Никаких ошибок (в т.ч. «undefined name 'two'/'name'»).
    EXPECT_EQ(m_ctx.diag().errorCount(), 0);
    // Имена элементов НЕ объявлены в таблице символов.
    EXPECT_EQ(runner.analysis().symbols().resolve("two"), nullptr);
    EXPECT_EQ(runner.analysis().symbols().resolve("name"), nullptr);
}

TEST_F(SemanticTest, DictFieldTypeInference) {
    // d := (1, two=2, name='3',);  x := d.two;  y := d[0];
    // Вывод типа поля: d.two → Int8, d[0] → Bool (из Dims литерала).
    auto dictTerm = Term::Create(TermID::DICT, "", {}, parser::token_type::END);
    auto dict = std::make_shared<DictLiteralNode>(ParserToken::Kind::DictLiteral, std::move(dictTerm));
    dict->m_body.push_back(std::make_shared<Literal>(ParserToken::Kind::IntLiteral, "1"));
    dict->m_body.push_back(std::make_shared<ArgNode>(std::string("two"), nullptr, std::make_shared<Literal>(ParserToken::Kind::IntLiteral, "2")));
    dict->m_body.push_back(std::make_shared<ArgNode>(std::string("name"), nullptr, std::make_shared<Literal>(ParserToken::Kind::StrChar, "3")));

    auto dNameTerm = Term::Create(TermID::NAME, "d", {}, parser::token_type::NAME);
    auto dVar = std::make_shared<VarDecl>(std::move(dNameTerm), nullptr, std::move(dict));

    // x := d.two;  (MemberAccess по имени)
    auto accName = std::make_shared<Binary>(ParserToken::Kind::MemberAccess);
    accName->m_left = std::make_shared<IdentName>("d");
    accName->m_right = std::make_shared<IdentName>("two");
    auto xNameTerm = Term::Create(TermID::NAME, "x", {}, parser::token_type::NAME);
    auto xVar = std::make_shared<VarDecl>(std::move(xNameTerm), nullptr, std::move(accName));

    // y := d[0];  (ArrayAccess по статическому индексу)
    auto accIdx = std::make_shared<Binary>(ParserToken::Kind::ArrayAccess);
    accIdx->m_left = std::make_shared<IdentName>("d");
    accIdx->m_right = std::make_shared<Literal>(ParserToken::Kind::IntLiteral, "0");
    auto yNameTerm = Term::Create(TermID::NAME, "y", {}, parser::token_type::NAME);
    auto yVar = std::make_shared<VarDecl>(std::move(yNameTerm), nullptr, std::move(accIdx));

    std::vector<AstNodePtr> seq;
    seq.push_back(std::move(dVar));
    seq.push_back(std::move(xVar));
    seq.push_back(std::move(yVar));

    SemanticPassRunner runner(m_ctx);
    ASSERT_TRUE(runner.run(seq));
    EXPECT_EQ(m_ctx.diag().errorCount(), 0);

    const TypeId int8 = m_types->findType(type::Int8).value_or(INVALID_TYPE_ID);
    const TypeId boolT = m_types->findType(type::Bool).value_or(INVALID_TYPE_ID);
    // d.two → Int8; d[0] → Bool (элемент 0 - `1` → Bool).
    EXPECT_EQ(static_cast<VarDecl&>(*seq[1]).inferredType, int8);
    EXPECT_EQ(static_cast<VarDecl&>(*seq[2]).inferredType, boolT);
    // На символе d: размерность и типы полей.
    const Symbol* dSym = runner.analysis().symbols().resolve("d");
    ASSERT_NE(dSym, nullptr);
    EXPECT_EQ(dSym->dims, 3);
    ASSERT_EQ(dSym->dictFieldTypes.size(), 3u);
    EXPECT_EQ(dSym->dictFieldTypes[1].first, "two");
    EXPECT_EQ(clearInferred(dSym->dictFieldTypes[1].second), int8);
}

TEST_F(SemanticTest, DictAppendSpreadLiteral) {
    // d := (1, two=2,);  d []= ... (three=3, 4,);
    // Spread-merge из литерала: размер и типы полей переносятся (dims 2 → 4).
    auto dictTerm = Term::Create(TermID::DICT, "", {}, parser::token_type::END);
    auto dict = std::make_shared<DictLiteralNode>(ParserToken::Kind::DictLiteral, std::move(dictTerm));
    dict->m_body.push_back(std::make_shared<Literal>(ParserToken::Kind::IntLiteral, "1"));
    dict->m_body.push_back(std::make_shared<ArgNode>(std::string("two"), nullptr, std::make_shared<Literal>(ParserToken::Kind::IntLiteral, "2")));
    auto dNameTerm = Term::Create(TermID::NAME, "d", {}, parser::token_type::NAME);
    auto dVar = std::make_shared<VarDecl>(std::move(dNameTerm), nullptr, std::move(dict));

    // Операнд spread: литерал (three=3, 4,).
    auto srcTerm = Term::Create(TermID::DICT, "", {}, parser::token_type::END);
    auto src = std::make_shared<DictLiteralNode>(ParserToken::Kind::DictLiteral, std::move(srcTerm));
    src->m_body.push_back(std::make_shared<ArgNode>(std::string("three"), nullptr, std::make_shared<Literal>(ParserToken::Kind::IntLiteral, "3")));
    src->m_body.push_back(std::make_shared<Literal>(ParserToken::Kind::IntLiteral, "4"));

    auto ellTerm = Term::Create(TermID::ELLIPSIS, "...");
    auto ell = std::make_shared<Sequence>(ParserToken::Kind::Ellipsis, std::move(ellTerm));
    ell->m_body.push_back(std::move(src));

    auto append = std::make_shared<Binary>(ParserToken::Kind::AppendStmt);
    append->m_left = std::make_shared<IdentName>("d");
    append->m_right = std::move(ell);

    std::vector<AstNodePtr> seq;
    seq.push_back(std::move(dVar));
    seq.push_back(std::move(append));

    SemanticPassRunner runner(m_ctx);
    ASSERT_TRUE(runner.run(seq));
    EXPECT_EQ(m_ctx.diag().errorCount(), 0);

    const TypeId int8 = m_types->findType(type::Int8).value_or(INVALID_TYPE_ID);
    const Symbol* dSym = runner.analysis().symbols().resolve("d");
    ASSERT_NE(dSym, nullptr);
    EXPECT_EQ(dSym->dims, 4);
    ASSERT_EQ(dSym->dictFieldTypes.size(), 4u);
    EXPECT_EQ(dSym->dictFieldTypes[2].first, "three");
    EXPECT_EQ(clearInferred(dSym->dictFieldTypes[2].second), int8);
    EXPECT_EQ(dSym->dictFieldTypes[3].first, "");
    EXPECT_EQ(clearInferred(dSym->dictFieldTypes[3].second), int8);
}

TEST_F(SemanticTest, DictAppendSpreadVar) {
    // d := (1,);  d2 := (a=5, b=6,);  d []= ... d2;
    // Spread-merge из переменной: dims и типы полей переносятся (1 → 3).
    auto dictTerm = Term::Create(TermID::DICT, "", {}, parser::token_type::END);
    auto dict = std::make_shared<DictLiteralNode>(ParserToken::Kind::DictLiteral, std::move(dictTerm));
    dict->m_body.push_back(std::make_shared<Literal>(ParserToken::Kind::IntLiteral, "1"));
    auto dNameTerm = Term::Create(TermID::NAME, "d", {}, parser::token_type::NAME);
    auto dVar = std::make_shared<VarDecl>(std::move(dNameTerm), nullptr, std::move(dict));

    auto d2Term = Term::Create(TermID::DICT, "", {}, parser::token_type::END);
    auto d2 = std::make_shared<DictLiteralNode>(ParserToken::Kind::DictLiteral, std::move(d2Term));
    d2->m_body.push_back(std::make_shared<ArgNode>(std::string("a"), nullptr, std::make_shared<Literal>(ParserToken::Kind::IntLiteral, "5")));
    d2->m_body.push_back(std::make_shared<ArgNode>(std::string("b"), nullptr, std::make_shared<Literal>(ParserToken::Kind::IntLiteral, "6")));
    auto d2NameTerm = Term::Create(TermID::NAME, "d2", {}, parser::token_type::NAME);
    auto d2Var = std::make_shared<VarDecl>(std::move(d2NameTerm), nullptr, std::move(d2));

    auto ellTerm = Term::Create(TermID::ELLIPSIS, "...");
    auto ell = std::make_shared<Sequence>(ParserToken::Kind::Ellipsis, std::move(ellTerm));
    ell->m_body.push_back(std::make_shared<IdentName>("d2"));

    auto append = std::make_shared<Binary>(ParserToken::Kind::AppendStmt);
    append->m_left = std::make_shared<IdentName>("d");
    append->m_right = std::move(ell);

    std::vector<AstNodePtr> seq;
    seq.push_back(std::move(dVar));
    seq.push_back(std::move(d2Var));
    seq.push_back(std::move(append));

    SemanticPassRunner runner(m_ctx);
    ASSERT_TRUE(runner.run(seq));
    EXPECT_EQ(m_ctx.diag().errorCount(), 0);

    const Symbol* dSym = runner.analysis().symbols().resolve("d");
    ASSERT_NE(dSym, nullptr);
    EXPECT_EQ(dSym->dims, 3);
    ASSERT_EQ(dSym->dictFieldTypes.size(), 3u);
    EXPECT_EQ(dSym->dictFieldTypes[1].first, "a");
    EXPECT_EQ(dSym->dictFieldTypes[2].first, "b");
}

TEST_F(SemanticTest, LoweringRewritesNamedBreakToGoto) {

    // Строим тело функции с безымянным while и именованным break на цикл (label "L").
    auto funcTerm = Term::Create(TermID::NAME, "%f", {}, parser::token_type::NAME);
    auto func = std::make_shared<FuncDecl>(std::move(funcTerm));

    // Именованный break: L:: ++ (BreakStmt с label L).
    auto breakTerm = Term::Create(TermID::INT_PLUS, "L::");
    auto breakStmt = std::make_shared<JumpStmt>(ParserToken::Kind::BreakStmt, std::move(breakTerm));

    func->m_body = std::vector<AstNodePtr>{std::move(breakStmt)};

    std::vector<AstNodePtr> seq;
    seq.push_back(std::move(func));

    SemanticPassRunner runner(m_ctx);
    ASSERT_TRUE(runner.run(seq));

    // Тело функции: [BreakStmt] → после lowering именованный break стал GotoStmt (LabelRef).
    auto* fd = static_cast<FuncDecl*>(seq[0].get());
    ASSERT_TRUE(fd->m_body.has_value());
    ASSERT_EQ(fd->m_body->size(), 1u);
    ASSERT_EQ((*fd->m_body)[0]->kind(), ParserToken::Kind::GotoStmt);
    auto* gs = static_cast<LabelRef*>((*fd->m_body)[0].get());
    EXPECT_EQ(gs->m_name, "L_break");
}

// -- Опциональный анализатор LintHook (управляется флагом FlagKind::Lint) --

TEST_F(SemanticTest, LintDisabledByDefault) {
    // x := 42; - lint выключен по умолчанию → диагностик unused-var нет.
    auto t = Term::Create(TermID::NAME, "x", {}, parser::token_type::NAME);
    auto var = std::make_shared<VarDecl>(std::move(t), nullptr, std::make_shared<Literal>(ParserToken::Kind::IntLiteral, "42"));
    std::vector<AstNodePtr> seq;
    seq.push_back(std::move(var));

    SemanticPassRunner runner(m_ctx);
    EXPECT_TRUE(runner.run(seq));
    EXPECT_EQ(m_ctx.diag().warningCount(), 0);
}

TEST_F(SemanticTest, LintUnusedVarWarning) {
    // -Wlint → включён LintHook: неиспользуемая x порождает warning (OptKind::UnusedVar).
    m_ctx.opts().set_enabled(FlagKind::Lint, true);
    auto t = Term::Create(TermID::NAME, "x", {}, parser::token_type::NAME);
    auto var = std::make_shared<VarDecl>(std::move(t), nullptr, std::make_shared<Literal>(ParserToken::Kind::IntLiteral, "42"));
    std::vector<AstNodePtr> seq;
    seq.push_back(std::move(var));

    SemanticPassRunner runner(m_ctx);
    EXPECT_TRUE(runner.run(seq));
    EXPECT_GT(m_ctx.diag().warningCount(), 0);
}

TEST_F(SemanticTest, LintUsedVarNoWarning) {
    // -Wlint: a используется в b := a → a без warning, b (неиспользуемый) - с warning.
    m_ctx.opts().set_enabled(FlagKind::Lint, true);

    auto ta = Term::Create(TermID::NAME, "a", {}, parser::token_type::NAME);
    auto a = std::make_shared<VarDecl>(std::move(ta), nullptr, std::make_shared<Literal>(ParserToken::Kind::IntLiteral, "1"));
    auto tb = Term::Create(TermID::NAME, "b", {}, parser::token_type::NAME);
    auto b = std::make_shared<VarDecl>(std::move(tb), nullptr, std::make_shared<IdentName>("a"));

    std::vector<AstNodePtr> seq;
    seq.push_back(std::move(a));
    seq.push_back(std::move(b));

    SemanticPassRunner runner(m_ctx);
    EXPECT_TRUE(runner.run(seq));

    // Ровно одно unused-var (b); a используется в инициализаторе b.
    int unusedWarnings = 0;
    for (const auto& d : m_ctx.diag().diagnostics()) {
        if (d.severity == Severity::Warning && d.message.find("unused variable") != std::string::npos) {
            ++unusedWarnings;
        }
    }
    EXPECT_EQ(unusedWarnings, 1);
}

TEST_F(SemanticTest, LintUnusedVarInModule) {
    // -Wlint: корневой узел реального pipeline - ModuleNode; ядро обходит m_body модуля.
    m_ctx.opts().set_enabled(FlagKind::Lint, true);

    auto t = Term::Create(TermID::NAME, "x", {}, parser::token_type::NAME);
    auto var = std::make_shared<VarDecl>(std::move(t), nullptr, std::make_shared<Literal>(ParserToken::Kind::IntLiteral, "42"));
    auto mod = std::make_shared<ModuleNode>(0u, "m");
    mod->m_body = std::vector<AstNodePtr>{std::move(var)};

    std::vector<AstNodePtr> seq;
    seq.push_back(std::move(mod));

    SemanticPassRunner runner(m_ctx);
    EXPECT_TRUE(runner.run(seq));
    EXPECT_GT(m_ctx.diag().warningCount(), 0);
}

// -- Контекст-макросы (@::, @__FUNCTION__, @__FUNCSIG__, @__FUNCDNAME__) --

TEST_F(SemanticTest, ContextMacroNamespaceStringified) {
    // ns:: { x := @# @::; };  → инициализатор становится StrChar "::ns::"
    auto ns = std::make_shared<ScopeBlock>("ns::", 0);
    auto var = std::make_shared<VarDecl>("x", nullptr, std::make_shared<ContextMacro>(ParserToken::Kind::ContextMacro, "@#@::"));
    ns->m_body.push_back(std::move(var));

    std::vector<AstNodePtr> seq;
    seq.push_back(std::move(ns));

    SemanticPassRunner runner(m_ctx);
    EXPECT_TRUE(runner.run(seq));

    const auto& v = static_cast<const VarDecl&>(*seq[0]->as_sequence()->m_body[0]);
    ASSERT_NE(v.m_initializer, nullptr);
    EXPECT_EQ(v.m_initializer->kind(), ParserToken::Kind::StrChar);
    EXPECT_EQ(v.m_initializer->text(), "::ns::");
}

TEST_F(SemanticTest, ContextMacroFuncSigAlwaysString) {
    // @__FUNCSIG__ внутри функции → сразу StrChar с сигнатурой.
    auto fn = std::make_shared<FuncDecl>("%f");
    fn->m_type = std::make_shared<IdentType>("Void");
    fn->m_body =
        std::vector<AstNodePtr>{std::make_shared<VarDecl>("s", nullptr, std::make_shared<ContextMacro>(ParserToken::Kind::ContextMacro, "@__FUNCSIG__"))};

    std::vector<AstNodePtr> seq;
    seq.push_back(std::move(fn));

    SemanticPassRunner runner(m_ctx);
    EXPECT_TRUE(runner.run(seq));

    auto& fd = static_cast<FuncDecl&>(*seq[0]);
    ASSERT_TRUE(fd.m_body.has_value());
    const auto& v = static_cast<const VarDecl&>(*fd.m_body->at(0));
    ASSERT_NE(v.m_initializer, nullptr);
    EXPECT_EQ(v.m_initializer->kind(), ParserToken::Kind::StrChar);
    EXPECT_EQ(v.m_initializer->text(), "f():Void");
}

TEST_F(SemanticTest, ContextMacroFuncOutsideFunctionError) {
    // @# @__FUNCTION__ вне функции → диагностика ошибки.
    auto var = std::make_shared<VarDecl>("x", nullptr, std::make_shared<ContextMacro>(ParserToken::Kind::ContextMacro, "@#@__FUNCTION__"));
    std::vector<AstNodePtr> seq;
    seq.push_back(std::move(var));

    SemanticPassRunner runner(m_ctx);
    EXPECT_FALSE(runner.run(seq));
    EXPECT_GT(m_ctx.diag().errorCount(), 0);
}

// -- Инференс типов выражений (inferred vs explicit) --

TEST_F(SemanticTest, InferredLiteralType) {
    // x := 42  →  выведенный минимальный Int8 (42 ≤ 127); тип на узле и в Symbol.
    auto t = Term::Create(TermID::NAME, "x", {}, parser::token_type::NAME);
    auto var = std::make_shared<VarDecl>(std::move(t), nullptr, std::make_shared<Literal>(ParserToken::Kind::IntLiteral, "42"));
    std::vector<AstNodePtr> seq;
    seq.push_back(std::move(var));

    SemanticPassRunner runner(m_ctx);
    ASSERT_TRUE(runner.run(seq));

    const TypeId int8 = m_types->findType(type::Int8).value_or(INVALID_TYPE_ID);
    auto* sym = runner.analysis().symbols().resolve("x");
    ASSERT_NE(sym, nullptr);
    EXPECT_TRUE(typeIsInferred(sym->type));
    EXPECT_EQ(clearInferred(sym->type), int8);
    // Выведенный тип записан на узле объявления - для кодогенерации после сброса скоуп-стека.
    EXPECT_EQ(static_cast<VarDecl&>(*seq[0]).inferredType, int8);
}

TEST_F(SemanticTest, InferredWideningByAssignment) {
    // x := 1; x = 1000;  →  join {Int8, Int16} = Int16 (расширение по истории присвоений).
    auto t = Term::Create(TermID::NAME, "x", {}, parser::token_type::NAME);
    auto var = std::make_shared<VarDecl>(std::move(t), nullptr, std::make_shared<Literal>(ParserToken::Kind::IntLiteral, "1"));

    auto opTerm = Term::Create(TermID::ASSIGN, "=", {}, parser::token_type::END);
    auto assign = std::make_shared<Binary>(ParserToken::Kind::AssignOp, std::move(opTerm));
    assign->m_left = std::make_shared<IdentName>("x");
    assign->m_right = std::make_shared<Literal>(ParserToken::Kind::IntLiteral, "1000");

    std::vector<AstNodePtr> seq;
    seq.push_back(std::move(var));
    seq.push_back(std::move(assign));

    SemanticPassRunner runner(m_ctx);
    ASSERT_TRUE(runner.run(seq));

    const TypeId int16 = m_types->findType(type::Int16).value_or(INVALID_TYPE_ID);
    auto* sym = runner.analysis().symbols().resolve("x");
    ASSERT_NE(sym, nullptr);
    EXPECT_TRUE(typeIsInferred(sym->type));
    EXPECT_EQ(clearInferred(sym->type), int16);
    EXPECT_EQ(static_cast<VarDecl&>(*seq[0]).inferredType, int16);
}

TEST_F(SemanticTest, InferredBoolWidenedToInt64ByCompoundArith) {
    // mult := 1 (авто-выведенный Bool); mult += 5 → Bool расширяется до Int64
    // (использование в составной числовой арифметике продвигает Bool до максимального Int).
    auto t = Term::Create(TermID::NAME, "mult", {}, parser::token_type::NAME);
    auto var = std::make_shared<VarDecl>(std::move(t), nullptr, std::make_shared<Literal>(ParserToken::Kind::IntLiteral, "1"));
    auto opTerm = Term::Create(TermID::ASSIGN, "+=", {}, parser::token_type::END);
    auto assign = std::make_shared<Binary>(ParserToken::Kind::AssignOp, std::move(opTerm));
    assign->m_left = std::make_shared<IdentName>("mult");
    assign->m_right = std::make_shared<Literal>(ParserToken::Kind::IntLiteral, "5");

    std::vector<AstNodePtr> seq;
    seq.push_back(std::move(var));
    seq.push_back(std::move(assign));

    SemanticPassRunner runner(m_ctx);
    ASSERT_TRUE(runner.run(seq));

    const TypeId int64 = m_types->findType(type::Int64).value_or(INVALID_TYPE_ID);
    auto* sym = runner.analysis().symbols().resolve("mult");
    ASSERT_NE(sym, nullptr);
    EXPECT_TRUE(typeIsInferred(sym->type));
    EXPECT_EQ(clearInferred(sym->type), int64);
    EXPECT_EQ(static_cast<VarDecl&>(*seq[0]).inferredType, int64);
}

TEST_F(SemanticTest, ExplicitBoolNotWidenedByCompoundArith) {
    // x:Bool := 1; x += 5 → явный Bool НЕ расширяется до Int64 → ошибка.
    auto t = Term::Create(TermID::NAME, "x", {}, parser::token_type::NAME);
    auto var = std::make_shared<VarDecl>(std::move(t), std::make_shared<IdentType>("Bool"), std::make_shared<Literal>(ParserToken::Kind::IntLiteral, "1"));
    auto opTerm = Term::Create(TermID::ASSIGN, "+=", {}, parser::token_type::END);
    auto assign = std::make_shared<Binary>(ParserToken::Kind::AssignOp, std::move(opTerm));
    assign->m_left = std::make_shared<IdentName>("x");
    assign->m_right = std::make_shared<Literal>(ParserToken::Kind::IntLiteral, "5");

    std::vector<AstNodePtr> seq;
    seq.push_back(std::move(var));
    seq.push_back(std::move(assign));

    SemanticPassRunner runner(m_ctx);
    bool ok = runner.run(seq);

    EXPECT_FALSE(ok);
    EXPECT_GT(m_ctx.diag().errorCount(), 0);
}

TEST_F(SemanticTest, ExpressionResultTypeCppPromotion) {
    // x := 2 (Int8); y := x + 3  →  Int8 + Int8 = Int32 (обычные арифметические преобразования C++).
    auto tx = Term::Create(TermID::NAME, "x", {}, parser::token_type::NAME);
    auto xvar = std::make_shared<VarDecl>(std::move(tx), nullptr, std::make_shared<Literal>(ParserToken::Kind::IntLiteral, "2"));

    auto ty = Term::Create(TermID::NAME, "y", {}, parser::token_type::NAME);
    auto opTerm = Term::Create(TermID::OP_MATH, "+", {}, parser::token_type::END);
    auto add = std::make_shared<Binary>(ParserToken::Kind::MathOp, std::move(opTerm));
    add->m_left = std::make_shared<IdentName>("x");
    add->m_right = std::make_shared<Literal>(ParserToken::Kind::IntLiteral, "3");
    auto yvar = std::make_shared<VarDecl>(std::move(ty), nullptr, std::move(add));

    std::vector<AstNodePtr> seq;
    seq.push_back(std::move(xvar));
    seq.push_back(std::move(yvar));

    SemanticPassRunner runner(m_ctx);
    ASSERT_TRUE(runner.run(seq));

    EXPECT_EQ(static_cast<VarDecl&>(*seq[1]).inferredType, m_types->findType(type::Int32).value_or(INVALID_TYPE_ID));
}

TEST_F(SemanticTest, ExplicitTypeNotWidened) {
    // x:Int32 := 1; x = 1000;  →  явный тип фиксирован: x остаётся Int32.
    auto t = Term::Create(TermID::NAME, "x", {}, parser::token_type::NAME);
    auto xvar = std::make_shared<VarDecl>(std::move(t), std::make_shared<IdentType>("Int32"), std::make_shared<Literal>(ParserToken::Kind::IntLiteral, "1"));

    auto opTerm = Term::Create(TermID::ASSIGN, "=", {}, parser::token_type::END);
    auto assign = std::make_shared<Binary>(ParserToken::Kind::AssignOp, std::move(opTerm));
    assign->m_left = std::make_shared<IdentName>("x");
    assign->m_right = std::make_shared<Literal>(ParserToken::Kind::IntLiteral, "1000");

    std::vector<AstNodePtr> seq;
    seq.push_back(std::move(xvar));
    seq.push_back(std::move(assign));

    SemanticPassRunner runner(m_ctx);
    ASSERT_TRUE(runner.run(seq));

    const TypeId int32 = m_types->findType(type::Int32).value_or(INVALID_TYPE_ID);
    auto* sym = runner.analysis().symbols().resolve("x");
    ASSERT_NE(sym, nullptr);
    EXPECT_FALSE(typeIsInferred(sym->type));
    EXPECT_EQ(sym->type, int32); // явный тип - структурный, без бита inferred
}

// -- Константность (kConstFlag) на переменных ('^' → attr::ReadOnly) --

TEST_F(SemanticTest, ReadOnlyVarSetsConstBitTyped) {
    // x^: Int32 := 1 → тип несёт бит kConstFlag («константность в типе»); структурный базис - Int32.
    auto t = Term::Create(TermID::NAME, "x", {}, parser::token_type::NAME);
    auto var = std::make_shared<VarDecl>(std::move(t), std::make_shared<IdentType>("Int32"), std::make_shared<Literal>(ParserToken::Kind::IntLiteral, "1"));
    var->add_attr(m_ctx.attrs().lookup(attr::ReadOnly).value());
    std::vector<AstNodePtr> seq;
    seq.push_back(std::move(var));

    SemanticPassRunner runner(m_ctx);
    ASSERT_TRUE(runner.run(seq));

    const TypeId int32 = m_types->findType(type::Int32).value_or(INVALID_TYPE_ID);
    auto* sym = runner.analysis().symbols().resolve("x");
    ASSERT_NE(sym, nullptr);
    EXPECT_TRUE(typeIsConst(sym->type));
    EXPECT_EQ(clearConst(sym->type), int32);
    EXPECT_FALSE(typeIsInferred(sym->type)); // явный тип - без бита inferred
}

TEST_F(SemanticTest, ReadOnlyVarSetsConstBitUntyped) {
    // x^ := 42 → выведенный минимальный Int8 несёт биты inferred и const.
    auto t = Term::Create(TermID::NAME, "x", {}, parser::token_type::NAME);
    auto var = std::make_shared<VarDecl>(std::move(t), nullptr, std::make_shared<Literal>(ParserToken::Kind::IntLiteral, "42"));
    var->add_attr(m_ctx.attrs().lookup(attr::ReadOnly).value());
    std::vector<AstNodePtr> seq;
    seq.push_back(std::move(var));

    SemanticPassRunner runner(m_ctx);
    ASSERT_TRUE(runner.run(seq));

    const TypeId int8 = m_types->findType(type::Int8).value_or(INVALID_TYPE_ID);
    auto* sym = runner.analysis().symbols().resolve("x");
    ASSERT_NE(sym, nullptr);
    EXPECT_TRUE(typeIsConst(sym->type));
    EXPECT_TRUE(typeIsInferred(sym->type));
    EXPECT_EQ(clearConst(clearInferred(sym->type)), int8);
    // inferredType на узле - структурный (без битов), для кодогенерации.
    EXPECT_EQ(static_cast<VarDecl&>(*seq[0]).inferredType, int8);
}

// -- Become-const (`x^ = ...`) и защита от записи в константу --
namespace {
AttrId ReadOnlyAttr(Context& ctx) {
    auto id = ctx.attrs().lookup(attr::ReadOnly);
    EXPECT_TRUE(id.has_value()) << "attr::ReadOnly must be registered";
    return id.value();
}
} // namespace

TEST_F(SemanticTest, BecomeConstViaCaretAssignment) {
    // x := 42; x^ += 1 → x становится константной (kConstFlag на Symbol::type), ошибок нет.
    auto t = Term::Create(TermID::NAME, "x", {}, parser::token_type::NAME);
    auto var = std::make_shared<VarDecl>(std::move(t), nullptr, std::make_shared<Literal>(ParserToken::Kind::IntLiteral, "42"));

    // x^ += 1 - LHS IdentName с attr::ReadOnly (как после парсинга каретки на имени).
    auto opTerm = Term::Create(TermID::ASSIGN, "+=", {}, parser::token_type::END);
    auto assign = std::make_shared<Binary>(ParserToken::Kind::AssignOp, std::move(opTerm));
    auto lhs = std::make_shared<IdentName>("x");
    lhs->add_attr(ReadOnlyAttr(m_ctx));
    assign->m_left = std::move(lhs);
    assign->m_right = std::make_shared<Literal>(ParserToken::Kind::IntLiteral, "1");

    std::vector<AstNodePtr> seq;
    seq.push_back(std::move(var));
    seq.push_back(std::move(assign));

    SemanticPassRunner runner(m_ctx);
    ASSERT_TRUE(runner.run(seq));

    auto* sym = runner.analysis().symbols().resolve("x");
    ASSERT_NE(sym, nullptr);
    EXPECT_TRUE(typeIsConst(sym->type));
}

TEST_F(SemanticTest, WriteToConstDeclarationIsError) {
    // x^ := 42; x = 5 → ошибка записи в константную переменную.
    auto t = Term::Create(TermID::NAME, "x", {}, parser::token_type::NAME);
    auto var = std::make_shared<VarDecl>(std::move(t), nullptr, std::make_shared<Literal>(ParserToken::Kind::IntLiteral, "42"));
    var->add_attr(ReadOnlyAttr(m_ctx));

    auto opTerm = Term::Create(TermID::ASSIGN, "=", {}, parser::token_type::END);
    auto assign = std::make_shared<Binary>(ParserToken::Kind::AssignOp, std::move(opTerm));
    assign->m_left = std::make_shared<IdentName>("x");
    assign->m_right = std::make_shared<Literal>(ParserToken::Kind::IntLiteral, "5");

    std::vector<AstNodePtr> seq;
    seq.push_back(std::move(var));
    seq.push_back(std::move(assign));

    SemanticPassRunner runner(m_ctx);
    EXPECT_FALSE(runner.run(seq));
    EXPECT_GT(m_ctx.diag().errorCount(), 0);
}

TEST_F(SemanticTest, WriteToBecomeConstIsError) {
    // x := 42; x^ += 1; x = 5 → x константна после x^ += 1, запись - ошибка.
    auto t = Term::Create(TermID::NAME, "x", {}, parser::token_type::NAME);
    auto var = std::make_shared<VarDecl>(std::move(t), nullptr, std::make_shared<Literal>(ParserToken::Kind::IntLiteral, "42"));

    auto makeCaretAssign = [&]() {
        auto opTerm = Term::Create(TermID::ASSIGN, "+=", {}, parser::token_type::END);
        auto assign = std::make_shared<Binary>(ParserToken::Kind::AssignOp, std::move(opTerm));
        auto lhs = std::make_shared<IdentName>("x");
        lhs->add_attr(ReadOnlyAttr(m_ctx));
        assign->m_left = std::move(lhs);
        assign->m_right = std::make_shared<Literal>(ParserToken::Kind::IntLiteral, "1");
        return assign;
    };
    auto assignConst = std::make_shared<Binary>(ParserToken::Kind::AssignOp, Term::Create(TermID::ASSIGN, "=", {}, parser::token_type::END));
    assignConst->m_left = std::make_shared<IdentName>("x");
    assignConst->m_right = std::make_shared<Literal>(ParserToken::Kind::IntLiteral, "5");

    std::vector<AstNodePtr> seq;
    seq.push_back(std::move(var));
    seq.push_back(makeCaretAssign());
    seq.push_back(std::move(assignConst));

    SemanticPassRunner runner(m_ctx);
    EXPECT_FALSE(runner.run(seq));
    EXPECT_GT(m_ctx.diag().errorCount(), 0);
}

TEST_F(SemanticTest, NarrowingVariableErrorWithFixit) {
    // a:Int64 := 5; b:Int8 := a;  →  сужение Int64→Int8: ошибка.
    // (fixit-подсказка «use cast :Int8(a)» проверяется в lit-тесте narrowing_error.src -
    //  для ручных узлов range невалиден, и fixit не прикрепляется.)
    auto ta = Term::Create(TermID::NAME, "a", {}, parser::token_type::NAME);
    auto avar = std::make_shared<VarDecl>(std::move(ta), std::make_shared<IdentType>("Int64"), std::make_shared<Literal>(ParserToken::Kind::IntLiteral, "5"));
    auto tb = Term::Create(TermID::NAME, "b", {}, parser::token_type::NAME);
    auto bvar = std::make_shared<VarDecl>(std::move(tb), std::make_shared<IdentType>("Int8"), std::make_shared<IdentName>("a"));

    std::vector<AstNodePtr> seq;
    seq.push_back(std::move(avar));
    seq.push_back(std::move(bvar));

    SemanticPassRunner runner(m_ctx);
    EXPECT_FALSE(runner.run(seq));
    EXPECT_GT(m_ctx.diag().errorCount(), 0);
}

TEST_F(SemanticTest, NarrowingLiteralFitsNoError) {
    // y:Int8 := 100;  →  литерал влезает в Int8 → без ошибки.
    auto ty = Term::Create(TermID::NAME, "y", {}, parser::token_type::NAME);
    auto yvar = std::make_shared<VarDecl>(std::move(ty), std::make_shared<IdentType>("Int8"), std::make_shared<Literal>(ParserToken::Kind::IntLiteral, "100"));
    std::vector<AstNodePtr> seq;
    seq.push_back(std::move(yvar));

    SemanticPassRunner runner(m_ctx);
    EXPECT_TRUE(runner.run(seq));
    EXPECT_EQ(m_ctx.diag().errorCount(), 0);
}

TEST_F(SemanticTest, NarrowingLiteralTooWideError) {
    // x:Int8 := 1000;  →  литерал 1000 (Int16) не влезает в Int8 → ошибка.
    auto tx = Term::Create(TermID::NAME, "x", {}, parser::token_type::NAME);
    auto xvar = std::make_shared<VarDecl>(std::move(tx), std::make_shared<IdentType>("Int8"), std::make_shared<Literal>(ParserToken::Kind::IntLiteral, "1000"));
    std::vector<AstNodePtr> seq;
    seq.push_back(std::move(xvar));

    SemanticPassRunner runner(m_ctx);
    EXPECT_FALSE(runner.run(seq));
    EXPECT_GT(m_ctx.diag().errorCount(), 0);
}

TEST_F(SemanticTest, AnyOperandResultPromotion) {
    // x:Any := 5; y := x + 2;  →  результат Int32 (any + конкретный → продвинутый конкретный).
    auto tx = Term::Create(TermID::NAME, "x", {}, parser::token_type::NAME);
    auto xvar = std::make_shared<VarDecl>(std::move(tx), std::make_shared<IdentType>("Any"), std::make_shared<Literal>(ParserToken::Kind::IntLiteral, "5"));
    auto ty = Term::Create(TermID::NAME, "y", {}, parser::token_type::NAME);
    auto opTerm = Term::Create(TermID::OP_MATH, "+", {}, parser::token_type::END);
    auto add = std::make_shared<Binary>(ParserToken::Kind::MathOp, std::move(opTerm));
    add->m_left = std::make_shared<IdentName>("x");
    add->m_right = std::make_shared<Literal>(ParserToken::Kind::IntLiteral, "2");
    auto yvar = std::make_shared<VarDecl>(std::move(ty), nullptr, std::move(add));

    std::vector<AstNodePtr> seq;
    seq.push_back(std::move(xvar));
    seq.push_back(std::move(yvar));

    SemanticPassRunner runner(m_ctx);
    ASSERT_TRUE(runner.run(seq));
    EXPECT_EQ(static_cast<VarDecl&>(*seq[1]).inferredType, m_types->findType(type::Int32).value_or(INVALID_TYPE_ID));
}

TEST_F(SemanticTest, LiteralBoolAndStringWidthTyping) {
    // t := 1 → Bool; c := 'w' (одинарные кавычки, узкая строка) → StrChar;
    // s := "hi" (двойные кавычки, широкая строка) → StrWide.
    auto tt = Term::Create(TermID::NAME, "t", {}, parser::token_type::NAME);
    auto tvar = std::make_shared<VarDecl>(std::move(tt), nullptr, std::make_shared<Literal>(ParserToken::Kind::IntLiteral, "1"));
    auto tc = Term::Create(TermID::NAME, "c", {}, parser::token_type::NAME);
    auto cvar = std::make_shared<VarDecl>(std::move(tc), nullptr, std::make_shared<Literal>(ParserToken::Kind::StrChar, "w"));
    auto ts = Term::Create(TermID::NAME, "s", {}, parser::token_type::NAME);
    auto svar = std::make_shared<VarDecl>(std::move(ts), nullptr, std::make_shared<Literal>(ParserToken::Kind::StrWide, "hi"));

    std::vector<AstNodePtr> seq;
    seq.push_back(std::move(tvar));
    seq.push_back(std::move(cvar));
    seq.push_back(std::move(svar));

    SemanticPassRunner runner(m_ctx);
    ASSERT_TRUE(runner.run(seq));

    EXPECT_EQ(static_cast<VarDecl&>(*seq[0]).inferredType, m_types->findType(type::Bool).value_or(INVALID_TYPE_ID));
    EXPECT_EQ(static_cast<VarDecl&>(*seq[1]).inferredType, m_types->findType(type::StrChar).value_or(INVALID_TYPE_ID));
    EXPECT_EQ(static_cast<VarDecl&>(*seq[2]).inferredType, m_types->findType(type::StrWide).value_or(INVALID_TYPE_ID));
}

TEST_F(SemanticTest, DictBoolFieldPromotedInArithmetic) {
    // d := (1, 2); x := d[0] + d[1];  →  d[0]=Bool(из литерала 1), d[1]=Int8;
    // auto-Bool продвигается по общим правилам приведения (bool→int) → результат Int32.
    auto dictTerm = Term::Create(TermID::DICT, "", {}, parser::token_type::END);
    auto dict = std::make_shared<DictLiteralNode>(ParserToken::Kind::DictLiteral, std::move(dictTerm));
    dict->m_body.push_back(std::make_shared<Literal>(ParserToken::Kind::IntLiteral, "1"));
    dict->m_body.push_back(std::make_shared<Literal>(ParserToken::Kind::IntLiteral, "2"));

    auto dNameTerm = Term::Create(TermID::NAME, "d", {}, parser::token_type::NAME);
    auto dVar = std::make_shared<VarDecl>(std::move(dNameTerm), nullptr, std::move(dict));

    auto acc0 = std::make_shared<Binary>(ParserToken::Kind::ArrayAccess);
    acc0->m_left = std::make_shared<IdentName>("d");
    acc0->m_right = std::make_shared<Literal>(ParserToken::Kind::IntLiteral, "0");
    auto acc1 = std::make_shared<Binary>(ParserToken::Kind::ArrayAccess);
    acc1->m_left = std::make_shared<IdentName>("d");
    acc1->m_right = std::make_shared<Literal>(ParserToken::Kind::IntLiteral, "1");
    auto opTerm = Term::Create(TermID::OP_MATH, "+", {}, parser::token_type::END);
    auto add = std::make_shared<Binary>(ParserToken::Kind::MathOp, std::move(opTerm));
    add->m_left = std::move(acc0);
    add->m_right = std::move(acc1);
    auto xNameTerm = Term::Create(TermID::NAME, "x", {}, parser::token_type::NAME);
    auto xVar = std::make_shared<VarDecl>(std::move(xNameTerm), nullptr, std::move(add));

    std::vector<AstNodePtr> seq;
    seq.push_back(std::move(dVar));
    seq.push_back(std::move(xVar));

    SemanticPassRunner runner(m_ctx);
    ASSERT_TRUE(runner.run(seq));
    EXPECT_EQ(m_ctx.diag().errorCount(), 0);
    EXPECT_EQ(static_cast<VarDecl&>(*seq[1]).inferredType, m_types->findType(type::Int32).value_or(INVALID_TYPE_ID));
}

TEST_F(SemanticTest, LiteralBoolPromotedInArithmetic) {
    // x := 1 + 2;  →  `1`→Bool(auto), `2`→Int8; auto-Bool → Int32 → результат Int32.
    auto opTerm = Term::Create(TermID::OP_MATH, "+", {}, parser::token_type::END);
    auto add = std::make_shared<Binary>(ParserToken::Kind::MathOp, std::move(opTerm));
    add->m_left = std::make_shared<Literal>(ParserToken::Kind::IntLiteral, "1");
    add->m_right = std::make_shared<Literal>(ParserToken::Kind::IntLiteral, "2");
    auto t = Term::Create(TermID::NAME, "x", {}, parser::token_type::NAME);
    auto var = std::make_shared<VarDecl>(std::move(t), nullptr, std::move(add));
    std::vector<AstNodePtr> seq;
    seq.push_back(std::move(var));

    SemanticPassRunner runner(m_ctx);
    ASSERT_TRUE(runner.run(seq));
    EXPECT_EQ(m_ctx.diag().errorCount(), 0);
    EXPECT_EQ(static_cast<VarDecl&>(*seq[0]).inferredType, m_types->findType(type::Int32).value_or(INVALID_TYPE_ID));
}

TEST_F(SemanticTest, ExplicitBoolInArithmeticError) {
    // x:Bool := 1; y := x + 2;  →  явный Bool в арифметике → ошибка компиляции.
    auto tx = Term::Create(TermID::NAME, "x", {}, parser::token_type::NAME);
    auto xvar = std::make_shared<VarDecl>(std::move(tx), std::make_shared<IdentType>("Bool"), std::make_shared<Literal>(ParserToken::Kind::IntLiteral, "1"));
    auto opTerm = Term::Create(TermID::OP_MATH, "+", {}, parser::token_type::END);
    auto add = std::make_shared<Binary>(ParserToken::Kind::MathOp, std::move(opTerm));
    add->m_left = std::make_shared<IdentName>("x");
    add->m_right = std::make_shared<Literal>(ParserToken::Kind::IntLiteral, "2");
    auto ty = Term::Create(TermID::NAME, "y", {}, parser::token_type::NAME);
    auto yvar = std::make_shared<VarDecl>(std::move(ty), nullptr, std::move(add));
    std::vector<AstNodePtr> seq;
    seq.push_back(std::move(xvar));
    seq.push_back(std::move(yvar));

    SemanticPassRunner runner(m_ctx);
    EXPECT_FALSE(runner.run(seq));
    EXPECT_GT(m_ctx.diag().errorCount(), 0);
}

// -- Storage (месторасположение переменной) + нормализация имён без сигила --

// Хук, записывающий месторасположение (storage) объявляемых переменных во время обхода.
class StorageProbe : public InlineAnalysisHook {
  public:
    void onDeclare(const Symbol& sym) override {
        // ВНИМАНИЕ: declareOrComplete делает std::move(sym), поэтому sym.name опустошён
        // (moved-from); имя читаем из узла объявления (storage/decl не перемещаются).
        if (sym.decl && sym.decl->kind() == ParserToken::Kind::VarDecl) {
            m_storage[std::string(sym.decl->text())] = sym.storage;
        }
    }
    std::map<std::string, Storage> m_storage;
};

TEST_F(SemanticTest, StorageGlobalIsGlobal) {
    // g := 1; на глобальном уровне → Storage::Global, имя без нормализации.
    auto var = std::make_shared<VarDecl>("g", nullptr, std::make_shared<Literal>(ParserToken::Kind::IntLiteral, "1"));
    std::vector<AstNodePtr> seq;
    seq.push_back(std::move(var));
    SemanticPassRunner runner(m_ctx);
    ASSERT_TRUE(runner.run(seq));
    auto* sym = runner.analysis().symbols().resolve("g");
    ASSERT_NE(sym, nullptr);
    EXPECT_EQ(sym->name, "g");
    EXPECT_EQ(sym->storage, Storage::Global);
}

TEST_F(SemanticTest, StorageLocalNormalizedToSigil) {
    // f() { x := 1; } - локальная переменная без сигила нормализуется в $x, Storage::Local.
    AnalysisContext actx(m_ctx);
    NameResolutionPass core(actx);
    auto probe = std::make_unique<StorageProbe>();
    StorageProbe* probePtr = probe.get();
    core.addHook(std::move(probe));

    auto funcTerm = Term::Create(TermID::NAME, "f", {}, parser::token_type::NAME);
    auto func = std::make_shared<FuncDecl>(std::move(funcTerm));
    func->m_params = std::vector<AstNodePtr>{};
    func->m_body = std::vector<AstNodePtr>{};
    func->m_body->push_back(std::make_shared<VarDecl>("x", nullptr, std::make_shared<Literal>(ParserToken::Kind::IntLiteral, "1")));

    std::vector<AstNodePtr> seq;
    seq.push_back(std::move(func));
    core.run(seq);
    core.finalize();

    EXPECT_EQ(m_ctx.diag().errorCount(), 0);
    EXPECT_EQ(probePtr->m_storage["$x"], Storage::Local);
}

TEST_F(SemanticTest, StorageNamespaceIsStatic) {
    // ns::x := 1; - имя с областью имён (::) → Storage::Static, без нормализации в $.
    AnalysisContext actx(m_ctx);
    NameResolutionPass core(actx);
    auto probe = std::make_unique<StorageProbe>();
    StorageProbe* probePtr = probe.get();
    core.addHook(std::move(probe));

    auto var = std::make_shared<VarDecl>("ns::x", nullptr, std::make_shared<Literal>(ParserToken::Kind::IntLiteral, "1"));
    std::vector<AstNodePtr> seq;
    seq.push_back(std::move(var));
    core.run(seq);
    core.finalize();

    EXPECT_EQ(m_ctx.diag().errorCount(), 0);
    EXPECT_EQ(probePtr->m_storage["ns::x"], Storage::Static);
}

TEST_F(SemanticTest, NoSigilLocalResolution) {
    // f() { x := 1; y := x; } - x/y нормализуются в $x/$y; ссылка x резолвится в $x
    // (без «undefined name»), единый алгоритм разрешения простых имён.
    AnalysisContext actx(m_ctx);
    NameResolutionPass core(actx);
    auto funcTerm = Term::Create(TermID::NAME, "f", {}, parser::token_type::NAME);
    auto func = std::make_shared<FuncDecl>(std::move(funcTerm));
    func->m_params = std::vector<AstNodePtr>{};
    func->m_body = std::vector<AstNodePtr>{};
    func->m_body->push_back(std::make_shared<VarDecl>("x", nullptr, std::make_shared<Literal>(ParserToken::Kind::IntLiteral, "1")));
    func->m_body->push_back(std::make_shared<VarDecl>("y", nullptr, std::make_shared<IdentName>("x")));

    std::vector<AstNodePtr> seq;
    seq.push_back(std::move(func));
    core.run(seq);
    core.finalize();

    EXPECT_EQ(m_ctx.diag().errorCount(), 0);
}

TEST_F(SemanticTest, BareNameInfersSigilForFunctionAndParam) {
    // %f(n:Int32):Void := { x := f($n); }; - bare `f` в вызове резолвится в нативную `%f`,
    // а `$n` - в параметр `n` (правила вывода сигилов). Никаких «undefined name».
    AnalysisContext actx(m_ctx);
    NameResolutionPass core(actx);

    auto funcTerm = Term::Create(TermID::NAME, "%f", {}, parser::token_type::NAME);
    auto func = std::make_shared<FuncDecl>(std::move(funcTerm));
    auto pTerm = Term::Create(TermID::NAME, "n", {}, parser::token_type::NAME);
    func->m_params = std::vector<AstNodePtr>{};
    func->m_params->push_back(std::make_shared<ArgNode>(std::move(pTerm), std::make_shared<IdentType>("Int32")));
    func->m_body = std::vector<AstNodePtr>{};
    auto call = std::make_shared<CallExpr>(ParserToken::Kind::CallExpr, std::make_shared<IdentName>("f"));
    call->m_args = std::vector<AstNodePtr>{std::make_shared<IdentName>("$n")};
    func->m_body->push_back(std::make_shared<VarDecl>("x", nullptr, std::move(call)));

    std::vector<AstNodePtr> seq;
    seq.push_back(std::move(func));
    core.run(seq);
    core.finalize();

    // Ни «undefined name 'f'» (bare f → %f), ни «undefined name '$n'» ($n → параметр n).
    EXPECT_EQ(m_ctx.diag().errorCount(), 0);
}

TEST_F(SemanticTest, EmbedUsageOptionDefaultWarns) {
    // Опция -Wembed (default Warning): сам факт использования узла {% ... %} даёт предупреждение.
    auto embedTerm = Term::Create(TermID::EMBED, "int x = 5;", {}, parser::token_type::END);
    auto embed = std::make_shared<AstNodeAttr>(ParserToken::Kind::EmbedExpr, std::move(embedTerm));
    std::vector<AstNodePtr> seq;
    seq.push_back(std::move(embed));
    SemanticPassRunner runner(m_ctx);
    const int before = m_ctx.diag().warningCount();
    ASSERT_TRUE(runner.run(seq));
    EXPECT_GT(m_ctx.diag().warningCount(), before);
}

TEST_F(SemanticTest, EmbedUsageOptionIgnore) {
    // -Wembed=ignore → предупреждение за сам факт {% ... %} подавляется.
    m_ctx.opts().set(OptKind::Embed, std::nullopt);
    auto embedTerm = Term::Create(TermID::EMBED, "int x = 5;", {}, parser::token_type::END);
    auto embed = std::make_shared<AstNodeAttr>(ParserToken::Kind::EmbedExpr, std::move(embedTerm));
    std::vector<AstNodePtr> seq;
    seq.push_back(std::move(embed));
    SemanticPassRunner runner(m_ctx);
    const int before = m_ctx.diag().warningCount();
    ASSERT_TRUE(runner.run(seq));
    EXPECT_EQ(m_ctx.diag().warningCount(), before);
}

} // namespace
} // namespace trust