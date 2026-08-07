#include "utils/io.hpp"
#include "transpiler/transpiler.hpp"
#include "semantic/pass_runner.hpp"
#include "pipeline/pipeline.hpp"
#include "ast/ast_nodes.hpp"
#include "ast/attr_builtin.hpp"
#include "ast/ident_name.hpp"
#include "ast/token.hpp"
#include "ast/token_type.hpp"
#include "diag/context.hpp"
#include "types/registry.hpp"
#include "syntax/term.h"
#include "syntax/term_types.h"
#include "gtest/gtest.h"
#include <filesystem>
#include <fstream>
#include <sstream>
#include <vector>

namespace trust {
namespace {

class TranspilerTest : public ::testing::Test {
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

/// Test: generateToFile for int variable (x := 42 → int8_t c_x = 42;)
TEST_F(TranspilerTest, GenerateToFileIntVar) {

    MapperFile input_file = m_ctx.source().add_source("test.src", "x := 42;", true);
    MapperRange range(m_ctx.source().makeLoc(input_file, 1), m_ctx.source().makeLoc(input_file, 9));

    std::string name = "x";
    auto term = Term::Create(TermID::NAME, name, range, parser::token_type::NAME);
    auto var = std::make_shared<VarDecl>(std::move(term), nullptr, std::make_shared<Literal>(ParserToken::Kind::IntLiteral, "42"));
    std::vector<AstNodePtr> seq;
    seq.push_back(std::move(var));

    SemanticPassRunner runner(m_ctx);
    ASSERT_TRUE(runner.run(seq));

    MapperFile out_idx = m_ctx.source().add_output("test_output.cpp", true);
    ASSERT_FALSE(out_idx.isInvalid());

    CppTranspiler gen(m_ctx);
    gen.generateToFile(seq, out_idx);

    std::string result = m_ctx.source().output_result(out_idx);
    EXPECT_TRUE(result.find("int8_t c_x = 42;") != std::string::npos);

    auto* reader = m_ctx.source().toReader();
    ASSERT_NE(reader, nullptr);
}

/// Test: generateToFile для рационального литерала `1\1` (отдельная лексема RationalLiteral)
/// → `trust::Rational("1\1")` (создание непосредственно из строки).
TEST_F(TranspilerTest, GenerateToFileRationalLiteral) {
    MapperFile input_file = m_ctx.source().add_source("rat.src", "x :Rational := 1\\1;", true);
    MapperRange nameRange(m_ctx.source().makeLoc(input_file, 1), m_ctx.source().makeLoc(input_file, 2));
    MapperRange litRange(m_ctx.source().makeLoc(input_file, 15), m_ctx.source().makeLoc(input_file, 19));
    auto nameTerm = Term::Create(TermID::NAME, "x", nameRange, parser::token_type::NAME);
    auto litTerm = Term::Create(TermID::RATIONAL, "1\\1", litRange, parser::token_type::END);
    auto var = std::make_shared<VarDecl>(std::move(nameTerm), std::make_shared<IdentType>("Rational"),
                                         std::make_shared<Literal>(ParserToken::Kind::RationalLiteral, std::move(litTerm)));
    std::vector<AstNodePtr> seq;
    seq.push_back(std::move(var));

    SemanticPassRunner runner(m_ctx);
    ASSERT_TRUE(runner.run(seq));

    MapperFile out_idx = m_ctx.source().add_output("rat_out.cpp", true);
    ASSERT_FALSE(out_idx.isInvalid());

    CppTranspiler gen(m_ctx);
    gen.generateToFile(seq, out_idx);

    std::string result = m_ctx.source().output_result(out_idx);
    EXPECT_NE(result.find("trust::Rational(\"1\\\\1\")"), std::string::npos);
}
/// Test: generateToFile для литерала словаря `(1, two=2, name='3',)` (TermID::DICT)
/// → `trust::Dict{ {"", 1}, {"two", 2}, {"name", std::string("3")} }` + include заголовка.
TEST_F(TranspilerTest, GenerateToFileDictLiteral) {
    MapperFile input_file = m_ctx.source().add_source("dict.src", "d := (1, two=2, name='3',);", true);
    MapperRange nameRange(m_ctx.source().makeLoc(input_file, 1), m_ctx.source().makeLoc(input_file, 2));
    MapperRange dictRange(m_ctx.source().makeLoc(input_file, 6), m_ctx.source().makeLoc(input_file, 27));

    // Литерал словаря: контракт — все элементы ArgNode (имя в text(), значение в m_value).
    auto dictTerm = Term::Create(TermID::DICT, "", dictRange, parser::token_type::END);
    auto dict = std::make_shared<DictLiteralNode>(ParserToken::Kind::DictLiteral, std::move(dictTerm));
    // Безымянный элемент: ArgNode(имя="", значение).
    dict->m_body.push_back(std::make_shared<ArgNode>(std::string(""), nullptr, std::make_shared<Literal>(ParserToken::Kind::IntLiteral, "1")));

    auto twoEl = std::make_shared<ArgNode>(std::string("two"), nullptr, std::make_shared<Literal>(ParserToken::Kind::IntLiteral, "2"));
    dict->m_body.push_back(std::move(twoEl));

    auto nameEl = std::make_shared<ArgNode>(std::string("name"), nullptr, std::make_shared<Literal>(ParserToken::Kind::StrChar, "3"));
    dict->m_body.push_back(std::move(nameEl));

    auto nameTerm = Term::Create(TermID::NAME, "d", nameRange, parser::token_type::NAME);
    auto var = std::make_shared<VarDecl>(std::move(nameTerm), nullptr, std::move(dict));
    std::vector<AstNodePtr> seq;
    seq.push_back(std::move(var));

    SemanticPassRunner runner(m_ctx);
    ASSERT_TRUE(runner.run(seq));

    MapperFile out_idx = m_ctx.source().add_output("dict_out.cpp", true);
    ASSERT_FALSE(out_idx.isInvalid());

    CppTranspiler gen(m_ctx);
    gen.generateToFile(seq, out_idx);

    std::string result = m_ctx.source().output_result(out_idx);
    // Инклуд рантайм-заголовка словаря.
    EXPECT_NE(result.find("#include \"trust/dict.hpp\""), std::string::npos) << result;
    // Конструкция trust::Dict{ ... } с элементами TypedValue{kind, значение-точного-типа}.
    EXPECT_NE(result.find("trust::Dict c_d = trust::Dict{"), std::string::npos) << result;
    EXPECT_NE(result.find("trust::TypedValue{"), std::string::npos) << result;
    // `1`→Bool→bool(1); `2`→Int8→int8_t(2); `'3'`→StrChar→std::string("3").
    EXPECT_NE(result.find("bool(1)"), std::string::npos) << result;
    EXPECT_NE(result.find("int8_t(2)"), std::string::npos) << result;
    EXPECT_NE(result.find("std::string(\"3\")"), std::string::npos) << result;
}

/// Test: generateToFile для литерала диапазона `1..10` (RangeExpr, 2 операнда без шага)
/// → `auto c_r = trust::Range<int64_t>(1, 10);` + include `trust/range.hpp`.
TEST_F(TranspilerTest, GenerateToFileRangeLiteral) {
    MapperFile input_file = m_ctx.source().add_source("range.src", "r := 1..10;", true);
    MapperRange nameRange(m_ctx.source().makeLoc(input_file, 1), m_ctx.source().makeLoc(input_file, 2));
    MapperRange rangeRange(m_ctx.source().makeLoc(input_file, 6), m_ctx.source().makeLoc(input_file, 12));

    auto rangeTerm = Term::Create(TermID::RANGE, "..", rangeRange, parser::token_type::END);
    auto rng = std::make_shared<RangeExpr>(ParserToken::Kind::RangeExpr, std::move(rangeTerm));
    rng->m_body.push_back(std::make_shared<Literal>(ParserToken::Kind::IntLiteral, "1"));
    rng->m_body.push_back(std::make_shared<Literal>(ParserToken::Kind::IntLiteral, "10"));

    auto nameTerm = Term::Create(TermID::NAME, "r", nameRange, parser::token_type::NAME);
    auto var = std::make_shared<VarDecl>(std::move(nameTerm), nullptr, std::move(rng));
    std::vector<AstNodePtr> seq;
    seq.push_back(std::move(var));

    SemanticPassRunner runner(m_ctx);
    ASSERT_TRUE(runner.run(seq));

    MapperFile out_idx = m_ctx.source().add_output("range_out.cpp", true);
    ASSERT_FALSE(out_idx.isInvalid());

    CppTranspiler gen(m_ctx);
    gen.generateToFile(seq, out_idx);

    std::string result = m_ctx.source().output_result(out_idx);
    // Инклуд рантайм-заголовка диапазона.
    EXPECT_NE(result.find("#include \"trust/range.hpp\""), std::string::npos) << result;
    // Переменная типа Range<Int64> (структурный параметризованный тип) → trust::Range<int64_t>.
    EXPECT_NE(result.find("trust::Range<int64_t> c_r = trust::Range<int64_t>(1, 10);"), std::string::npos) << result;
}

/// Test: составной литерал (ArrayInit) как значение члена Variant — анализ/регистрация работают,
/// кодогенерация выдаёт «не реализовано» и НЕ эмитит сломанный C++ (memberValueCpp — скалярные).
TEST_F(TranspilerTest, VariantCompositeMemberValueNotImplemented) {
    auto dict = std::make_shared<DictLiteralNode>(ParserToken::Kind::DictLiteral, std::string(""));
    dict->m_type = std::make_shared<IdentType>(std::string("Variant"));
    dict->m_body.push_back(
        std::make_shared<ArgNode>(std::string("a"), nullptr, std::make_shared<DictLiteralNode>(ParserToken::Kind::ArrayInit, std::string(""))));
    auto variantDecl = std::make_shared<Binary>(ParserToken::Kind::TypeDecl, std::make_shared<IdentName>(std::string("Value")), std::move(dict));
    std::vector<AstNodePtr> seq;
    seq.push_back(std::move(variantDecl));

    SemanticPassRunner runner(m_ctx);
    ASSERT_TRUE(runner.run(seq)); // анализ не блокирует — ошибка на кодогенерации

    MapperFile out_idx = m_ctx.source().add_output("value_out.cpp", true);
    ASSERT_FALSE(out_idx.isInvalid());

    CppTranspiler gen(m_ctx);
    gen.generateToFile(seq, out_idx);

    std::string result = m_ctx.source().output_result(out_idx);
    EXPECT_GT(m_ctx.diag().errorCount(), 0);
    EXPECT_EQ(result.find("std::variant"), std::string::npos); // сломанный код не эмитится
}

/// Test: составной литерал (ArrayInit) как значение члена Enum — кодогенерация «не реализовано».
TEST_F(TranspilerTest, EnumCompositeMemberValueNotImplemented) {
    auto dict = std::make_shared<DictLiteralNode>(ParserToken::Kind::DictLiteral, std::string(""));
    dict->m_type = std::make_shared<IdentType>(std::string("Enum"));
    dict->m_body.push_back(
        std::make_shared<ArgNode>(std::string("a"), nullptr, std::make_shared<DictLiteralNode>(ParserToken::Kind::ArrayInit, std::string(""))));
    auto enumDecl = std::make_shared<Binary>(ParserToken::Kind::TypeDecl, std::make_shared<IdentName>(std::string("Color")), std::move(dict));
    std::vector<AstNodePtr> seq;
    seq.push_back(std::move(enumDecl));

    SemanticPassRunner runner(m_ctx);
    ASSERT_TRUE(runner.run(seq));

    MapperFile out_idx = m_ctx.source().add_output("color_out.cpp", true);
    ASSERT_FALSE(out_idx.isInvalid());

    CppTranspiler gen(m_ctx);
    gen.generateToFile(seq, out_idx);

    std::string result = m_ctx.source().output_result(out_idx);
    EXPECT_GT(m_ctx.diag().errorCount(), 0);
    EXPECT_EQ(result.find("EnumMember"), std::string::npos); // сломанный код не эмитится
}

/// Test: generateToFile для const-вызова `$r.size^()` (attr::ReadOnly на callee) → семантика
/// помечает methodConstCall, кодогенерация эмитит `const_cast<const T&>(c_r).size()`.
TEST_F(TranspilerTest, GenerateToFileRangeConstCall) {
    MapperFile input_file = m_ctx.source().add_source("rc.src", "r := 1..10;\nx := 0;\n", true);
    MapperRange nameRange(m_ctx.source().makeLoc(input_file, 1), m_ctx.source().makeLoc(input_file, 2));
    MapperRange rangeRange(m_ctx.source().makeLoc(input_file, 6), m_ctx.source().makeLoc(input_file, 12));
    MapperRange xRange(m_ctx.source().makeLoc(input_file, 14), m_ctx.source().makeLoc(input_file, 15));

    auto rangeTerm = Term::Create(TermID::RANGE, "..", rangeRange, parser::token_type::END);
    auto rng = std::make_shared<RangeExpr>(ParserToken::Kind::RangeExpr, std::move(rangeTerm));
    rng->m_body.push_back(std::make_shared<Literal>(ParserToken::Kind::IntLiteral, "1"));
    rng->m_body.push_back(std::make_shared<Literal>(ParserToken::Kind::IntLiteral, "10"));

    auto nameTerm = Term::Create(TermID::NAME, "r", nameRange, parser::token_type::NAME);
    auto var = std::make_shared<VarDecl>(std::move(nameTerm), nullptr, std::move(rng));

    // $r.size^() — const-вызов: attr::ReadOnly на ВЫЗОВЕ (CallExpr; в реальном потоке ставит
    // convertAttrsToNode, т.к. canHaveImmutableQualifier(CallExpr)=true). Инициализатор переменной.
    auto callee = std::make_shared<IdentName>("size");
    auto call = std::make_shared<CallExpr>(ParserToken::Kind::CallExpr, std::move(callee));
    if (auto rid = m_ctx.attrs().lookup(attr::ReadOnly); rid) {
        call->add_attr(*rid);
    }
    auto lhs = std::make_shared<IdentName>("r");
    auto access = std::make_shared<Binary>(ParserToken::Kind::MemberAccess, std::move(lhs), std::move(call));

    auto xTerm = Term::Create(TermID::NAME, "x", xRange, parser::token_type::NAME);
    auto xvar = std::make_shared<VarDecl>(std::move(xTerm), nullptr, std::move(access));

    std::vector<AstNodePtr> seq;
    seq.push_back(std::move(var));
    seq.push_back(std::move(xvar));

    SemanticPassRunner runner(m_ctx);
    ASSERT_TRUE(runner.run(seq));

    MapperFile out_idx = m_ctx.source().add_output("rc_out.cpp", true);
    ASSERT_FALSE(out_idx.isInvalid());

    CppTranspiler gen(m_ctx);
    gen.generateToFile(seq, out_idx);

    std::string result = m_ctx.source().output_result(out_idx);
    // const-вызов → const_cast<const trust::Range<int64_t>&>(c_r).size().
    EXPECT_NE(result.find("const_cast<const trust::Range<int64_t>&>(c_r).size()"), std::string::npos) << result;
}

/// Test: entry-функция (*__main__) без явного типа возврата → сырое имя, тип `int`,
/// и `return 0;` в конце тела (согласовано с `extern int <имя>__main__()` в _main.cppt).
TEST_F(TranspilerTest, GenerateEntryFunction) {
    MapperFile input_file = m_ctx.source().add_source("entry.src", "@main():={}", true);
    MapperRange fnRange(m_ctx.source().makeLoc(input_file, 1), m_ctx.source().makeLoc(input_file, 12));
    MapperRange bodyRange(m_ctx.source().makeLoc(input_file, 11), m_ctx.source().makeLoc(input_file, 12));
    auto funcTerm = Term::Create(TermID::NAME, "foo__main__", fnRange, parser::token_type::NAME);
    auto func = std::make_shared<FuncDecl>(std::move(funcTerm));
    auto bodyTerm = Term::Create(TermID::INTEGER, "1", bodyRange, parser::token_type::END);
    func->m_body = std::vector<AstNodePtr>{std::make_shared<Literal>(ParserToken::Kind::IntLiteral, std::move(bodyTerm))};

    std::vector<AstNodePtr> seq;
    seq.push_back(std::move(func));

    SemanticPassRunner runner(m_ctx);
    ASSERT_TRUE(runner.run(seq));

    MapperFile out_idx = m_ctx.source().add_output("entry_out.cpp", true);
    ASSERT_FALSE(out_idx.isInvalid());

    CppTranspiler gen(m_ctx);
    gen.generateToFile(seq, out_idx);

    std::string result = m_ctx.source().output_result(out_idx);
    EXPECT_NE(result.find("int foo__main__()"), std::string::npos);
    EXPECT_NE(result.find("return 0;"), std::string::npos);
    // Entry не манглируется (иначе не совпадёт с extern-объявлением в _main.cppt).
    EXPECT_EQ(result.find("c_foo__main__"), std::string::npos);
}

/// Test: обычная функция (не entry) без явного типа возврата → `void` + манглинг `c_`.
TEST_F(TranspilerTest, GenerateRegularFunctionVoidMangled) {
    MapperFile input_file = m_ctx.source().add_source("helper.src", "helper():={}", true);
    MapperRange fnRange(m_ctx.source().makeLoc(input_file, 1), m_ctx.source().makeLoc(input_file, 7));
    auto funcTerm = Term::Create(TermID::NAME, "helper", fnRange, parser::token_type::NAME);
    auto func = std::make_shared<FuncDecl>(std::move(funcTerm));
    func->m_body = std::vector<AstNodePtr>{};

    std::vector<AstNodePtr> seq;
    seq.push_back(std::move(func));

    SemanticPassRunner runner(m_ctx);
    ASSERT_TRUE(runner.run(seq));

    MapperFile out_idx = m_ctx.source().add_output("helper_out.cpp", true);
    ASSERT_FALSE(out_idx.isInvalid());

    CppTranspiler gen(m_ctx);
    gen.generateToFile(seq, out_idx);

    std::string result = m_ctx.source().output_result(out_idx);
    EXPECT_NE(result.find("void c_helper()"), std::string::npos);
}

/// Test: generateToFile для Document-узла — комментарий эмитится сырым текстом с маркерами.
TEST_F(TranspilerTest, GenerateToFileWithDocument) {
    MapperFile input_file = m_ctx.source().add_source("doc.src", "/// doc\nx := 42;", true);
    MapperRange range(m_ctx.source().makeLoc(input_file, 1), m_ctx.source().makeLoc(input_file, 8));

    auto docTerm = Term::Create(TermID::DOCUMENT, "/// doc", range, parser::token_type::DOCUMENT);
    auto doc = std::make_shared<AstNodeAttr>(ParserToken::Kind::Document, std::move(docTerm));

    auto nameTerm = Term::Create(TermID::NAME, "x", range, parser::token_type::NAME);
    auto var = std::make_shared<VarDecl>(std::move(nameTerm), nullptr, std::make_shared<Literal>(ParserToken::Kind::IntLiteral, "42"));

    std::vector<AstNodePtr> seq;
    seq.push_back(std::move(doc));
    seq.push_back(std::move(var));

    SemanticPassRunner runner(m_ctx);
    ASSERT_TRUE(runner.run(seq));

    MapperFile out_idx = m_ctx.source().add_output("doc_out.cpp", true);
    ASSERT_FALSE(out_idx.isInvalid());

    CppTranspiler gen(m_ctx);
    gen.generateToFile(seq, out_idx);

    std::string result = m_ctx.source().output_result(out_idx);
    EXPECT_NE(result.find("/// doc"), std::string::npos);
    EXPECT_NE(result.find("int8_t c_x = 42;"), std::string::npos);
}

/// Test: флаг -Wno-comments подавляет Document-узлы в C++-выводе.
TEST_F(TranspilerTest, GenerateToFileSuppressDocument) {
    MapperFile input_file = m_ctx.source().add_source("doc_suppress.src", "/// doc\nx := 42;", true);
    MapperRange range(m_ctx.source().makeLoc(input_file, 1), m_ctx.source().makeLoc(input_file, 8));

    auto docTerm = Term::Create(TermID::DOCUMENT, "/// doc", range, parser::token_type::DOCUMENT);
    auto doc = std::make_shared<AstNodeAttr>(ParserToken::Kind::Document, std::move(docTerm));

    auto nameTerm = Term::Create(TermID::NAME, "x", range, parser::token_type::NAME);
    auto var = std::make_shared<VarDecl>(std::move(nameTerm), nullptr, std::make_shared<Literal>(ParserToken::Kind::IntLiteral, "42"));

    std::vector<AstNodePtr> seq;
    seq.push_back(std::move(doc));
    seq.push_back(std::move(var));

    // Подавление комментариев: флаг «comments» выключен (эквивалент -Wno-comments).
    m_ctx.opts().set_enabled(FlagKind::Comments, false);

    SemanticPassRunner runner(m_ctx);
    ASSERT_TRUE(runner.run(seq));

    MapperFile out_idx = m_ctx.source().add_output("doc_suppress_out.cpp", true);
    ASSERT_FALSE(out_idx.isInvalid());

    CppTranspiler gen(m_ctx);
    gen.generateToFile(seq, out_idx);

    std::string result = m_ctx.source().output_result(out_idx);
    EXPECT_EQ(result.find("/// doc"), std::string::npos);
    EXPECT_NE(result.find("int8_t c_x = 42;"), std::string::npos);
}

/// Test: Trust-доки `##`/`##<` невалидны в C++ — в выводе нормализуются в `///`/`///<`.
TEST_F(TranspilerTest, GenerateToFileNormalizesHashDocument) {
    MapperFile input_file = m_ctx.source().add_source("doc_hash.src", "## hash\ny := 7;", true);
    MapperRange range(m_ctx.source().makeLoc(input_file, 1), m_ctx.source().makeLoc(input_file, 7));

    auto docTerm = Term::Create(TermID::DOCUMENT, "## hash", range, parser::token_type::DOCUMENT);
    auto doc = std::make_shared<AstNodeAttr>(ParserToken::Kind::Document, std::move(docTerm));

    auto nameTerm = Term::Create(TermID::NAME, "y", range, parser::token_type::NAME);
    auto var = std::make_shared<VarDecl>(std::move(nameTerm), nullptr, std::make_shared<Literal>(ParserToken::Kind::IntLiteral, "7"));

    std::vector<AstNodePtr> seq;
    seq.push_back(std::move(doc));
    seq.push_back(std::move(var));

    SemanticPassRunner runner(m_ctx);
    ASSERT_TRUE(runner.run(seq));

    MapperFile out_idx = m_ctx.source().add_output("doc_hash_out.cpp", true);
    ASSERT_FALSE(out_idx.isInvalid());

    CppTranspiler gen(m_ctx);
    gen.generateToFile(seq, out_idx);

    std::string result = m_ctx.source().output_result(out_idx);
    EXPECT_NE(result.find("/// hash"), std::string::npos);
    EXPECT_EQ(result.find("## hash"), std::string::npos); // исходный '##' в выводе отсутствует
    EXPECT_NE(result.find("int8_t c_y = 7;"), std::string::npos);
}

/// Test: generateToFile for string variable (s := hello → std::string c_s = "hello";)
TEST_F(TranspilerTest, GenerateToFileStringVar) {

    MapperFile input_file = m_ctx.source().add_source("test2.src", "s := hello;", true);
    MapperRange range(m_ctx.source().makeLoc(input_file, 1), m_ctx.source().makeLoc(input_file, 12));

    std::string name = "s";
    auto term = Term::Create(TermID::NAME, name, range, parser::token_type::NAME);
    auto var = std::make_shared<VarDecl>(std::move(term), nullptr, std::make_shared<Literal>(ParserToken::Kind::StrChar, "hello"));
    std::vector<AstNodePtr> seq;
    seq.push_back(std::move(var));

    SemanticPassRunner runner(m_ctx);
    ASSERT_TRUE(runner.run(seq));

    MapperFile out_idx = m_ctx.source().add_output("test_str.cpp", true);
    ASSERT_FALSE(out_idx.isInvalid());

    CppTranspiler gen(m_ctx);
    gen.generateToFile(seq, out_idx);

    std::string result = m_ctx.source().output_result(out_idx);
    EXPECT_TRUE(result.find("std::string c_s = \"hello\";") != std::string::npos);

    auto* reader = m_ctx.source().toReader();
    ASSERT_NE(reader, nullptr);
}

/// Test: generateToFile for typed variable x:Int32 := 42;
TEST_F(TranspilerTest, GenerateToFileTypedVar) {

    MapperFile input_file = m_ctx.source().add_source("test.src", "x:Int32 := 42;", true);
    MapperRange range(m_ctx.source().makeLoc(input_file, 1), m_ctx.source().makeLoc(input_file, 7));

    std::string name = "x";
    auto term = Term::Create(TermID::NAME, name, range, parser::token_type::NAME);
    auto var = std::make_shared<VarDecl>(std::move(term), std::make_shared<IdentType>("Int32"), std::make_shared<Literal>(ParserToken::Kind::IntLiteral, "42"));
    std::vector<AstNodePtr> seq;
    seq.push_back(std::move(var));

    SemanticPassRunner runner(m_ctx);
    ASSERT_TRUE(runner.run(seq));

    MapperFile out_idx = m_ctx.source().add_output("test_output.cpp", true);
    ASSERT_FALSE(out_idx.isInvalid());

    CppTranspiler gen(m_ctx);
    gen.generateToFile(seq, out_idx);

    std::string result = m_ctx.source().output_result(out_idx);
    EXPECT_TRUE(result.find("int32_t c_x = 42;") != std::string::npos);

    auto* reader = m_ctx.source().toReader();
    ASSERT_NE(reader, nullptr);
}

/// Test: generateToFile for type declaration MyInt ::= :Int32;
TEST_F(TranspilerTest, GenerateToFileTypeDecl) {

    MapperFile input_file = m_ctx.source().add_source("test2.src", "MyInt ::= :Int32;", true);
    MapperRange range(m_ctx.source().makeLoc(input_file, 1), m_ctx.source().makeLoc(input_file, 18));

    auto opTerm = Term::Create(TermID::CREATE_TYPE, "::=", range, parser::token_type::END);
    auto b = std::make_shared<Binary>(ParserToken::Kind::TypeDecl, std::move(opTerm));

    const std::string leftName = "MyInt";
    MapperRange left_range(m_ctx.source().makeLoc(input_file, 1), m_ctx.source().makeLoc(input_file, 6));
    auto leftTerm = Term::Create(TermID::NAME, leftName, left_range, parser::token_type::NAME);
    b->m_left = std::make_shared<IdentName>(std::move(leftTerm));

    const std::string rightName = "Int32";
    auto rightTerm = Term::Create(TermID::NAME, rightName, {}, parser::token_type::NAME);
    b->m_right = std::make_shared<IdentType>(std::move(rightTerm));
    std::vector<AstNodePtr> seq;
    seq.push_back(std::move(b));

    SemanticPassRunner runner(m_ctx);
    ASSERT_TRUE(runner.run(seq));

    MapperFile out_idx = m_ctx.source().add_output("test_type.cpp", true);
    ASSERT_FALSE(out_idx.isInvalid());

    CppTranspiler gen(m_ctx);
    gen.generateToFile(seq, out_idx);

    std::string result = m_ctx.source().output_result(out_idx);
    EXPECT_TRUE(result.find("using c_MyInt = int32_t;") != std::string::npos);

    auto* reader = m_ctx.source().toReader();
    ASSERT_NE(reader, nullptr);
}

/// Test: expression statement with simple assignment (x = 5 → x = 5;)
TEST_F(TranspilerTest, GenerateSemicolonStmtSimpleAssign) {

    MapperFile input_file = m_ctx.source().add_source("test.src", "x := 0; x = 5;", true);
    MapperRange range(m_ctx.source().makeLoc(input_file, 9), m_ctx.source().makeLoc(input_file, 15));

    auto opTerm = Term::Create(TermID::ASSIGN, "=", range, parser::token_type::END);
    auto b = std::make_shared<Binary>(ParserToken::Kind::AssignOp, std::move(opTerm));
    b->m_left = std::make_shared<IdentName>("x");
    b->m_right = std::make_shared<Literal>(ParserToken::Kind::IntLiteral, "5");
    std::vector<AstNodePtr> seq;
    auto xTerm =
        Term::Create(TermID::NAME, "x", MapperRange(m_ctx.source().makeLoc(input_file, 1), m_ctx.source().makeLoc(input_file, 8)), parser::token_type::NAME);
    seq.push_back(std::make_shared<VarDecl>(std::move(xTerm), nullptr, std::make_shared<Literal>(ParserToken::Kind::IntLiteral, "0")));
    seq.push_back(std::move(b));

    SemanticPassRunner runner(m_ctx);
    ASSERT_TRUE(runner.run(seq));

    MapperFile out_idx = m_ctx.source().add_output("test_assign.cpp", true);
    ASSERT_FALSE(out_idx.isInvalid());

    CppTranspiler gen(m_ctx);
    gen.generateToFile(seq, out_idx);

    std::string result = m_ctx.source().output_result(out_idx);
    EXPECT_TRUE(result.find("x = 5;") != std::string::npos) << "result: " << result;

    auto* reader = m_ctx.source().toReader();
    ASSERT_NE(reader, nullptr);
}

/// Test: call expression as var initializer (x := foo(1, 2) → std::any x = foo(1, 2);)
TEST_F(TranspilerTest, GenerateCallExprInInitializer) {
    MapperFile input_file = m_ctx.source().add_source("test.src", "foo := 0; x := foo(1, 2);", true);
    MapperRange fooRange(m_ctx.source().makeLoc(input_file, 1), m_ctx.source().makeLoc(input_file, 8));
    MapperRange xRange(m_ctx.source().makeLoc(input_file, 9), m_ctx.source().makeLoc(input_file, 22));

    // foo := 0 — чтобы вызов foo(...) имел объявление (semantic lookup).
    auto fooTerm = Term::Create(TermID::NAME, "foo", fooRange, parser::token_type::NAME);
    auto fooVar = std::make_shared<VarDecl>(std::move(fooTerm), nullptr, std::make_shared<Literal>(ParserToken::Kind::IntLiteral, "0"));

    // x := foo(1, 2) — инициализатор — CallExpr.
    auto callee = std::make_shared<IdentName>("foo");
    auto callTerm = Term::Create(TermID::NAME, "foo", xRange, parser::token_type::NAME);
    auto call = std::make_shared<CallExpr>(ParserToken::Kind::CallExpr, std::move(callee));
    call->m_args = std::vector<AstNodePtr>{
        std::make_shared<Literal>(ParserToken::Kind::IntLiteral, "1"),
        std::make_shared<Literal>(ParserToken::Kind::IntLiteral, "2"),
    };

    auto xTerm = Term::Create(TermID::NAME, "x", xRange, parser::token_type::NAME);
    auto xVar = std::make_shared<VarDecl>(std::move(xTerm), nullptr, std::move(call));

    std::vector<AstNodePtr> seq;
    seq.push_back(std::move(fooVar));
    seq.push_back(std::move(xVar));

    SemanticPassRunner runner(m_ctx);
    ASSERT_TRUE(runner.run(seq));

    MapperFile out_idx = m_ctx.source().add_output("test_call.cpp", true);
    ASSERT_FALSE(out_idx.isInvalid());

    CppTranspiler gen(m_ctx);
    gen.generateToFile(seq, out_idx);

    std::string result = m_ctx.source().output_result(out_idx);
    EXPECT_TRUE(result.find("foo(1, 2)") != std::string::npos) << "result: " << result;
}
/// Test: expression statement with compound addition (x += 3 → x += 3;)

TEST_F(TranspilerTest, GenerateSemicolonStmtCompoundAdd) {

    MapperFile input_file = m_ctx.source().add_source("test.src", "x := 0; x += 3;", true);
    MapperRange range(m_ctx.source().makeLoc(input_file, 9), m_ctx.source().makeLoc(input_file, 16));

    auto opTerm = Term::Create(TermID::ASSIGN, "+=", range, parser::token_type::END);
    auto b = std::make_shared<Binary>(ParserToken::Kind::AssignOp, std::move(opTerm));
    b->m_left = std::make_shared<IdentName>("x");
    b->m_right = std::make_shared<Literal>(ParserToken::Kind::IntLiteral, "3");
    std::vector<AstNodePtr> seq;
    auto xTerm =
        Term::Create(TermID::NAME, "x", MapperRange(m_ctx.source().makeLoc(input_file, 1), m_ctx.source().makeLoc(input_file, 8)), parser::token_type::NAME);
    seq.push_back(std::make_shared<VarDecl>(std::move(xTerm), nullptr, std::make_shared<Literal>(ParserToken::Kind::IntLiteral, "0")));
    seq.push_back(std::move(b));

    SemanticPassRunner runner(m_ctx);
    ASSERT_TRUE(runner.run(seq));

    MapperFile out_idx = m_ctx.source().add_output("test_add.cpp", true);
    ASSERT_FALSE(out_idx.isInvalid());

    CppTranspiler gen(m_ctx);
    gen.generateToFile(seq, out_idx);

    std::string result = m_ctx.source().output_result(out_idx);
    EXPECT_TRUE(result.find("x += 3;") != std::string::npos) << "result: " << result;

    auto* reader = m_ctx.source().toReader();
    ASSERT_NE(reader, nullptr);
}

/// Test: expression statement with compound subtraction (x -= 3 → x -= 3;)
TEST_F(TranspilerTest, GenerateSemicolonStmtCompoundSub) {

    MapperFile input_file = m_ctx.source().add_source("test.src", "x := 0; x -= 3;", true);
    MapperRange range(m_ctx.source().makeLoc(input_file, 9), m_ctx.source().makeLoc(input_file, 16));

    auto opTerm = Term::Create(TermID::ASSIGN, "-=", range, parser::token_type::END);
    auto b = std::make_shared<Binary>(ParserToken::Kind::AssignOp, std::move(opTerm));
    b->m_left = std::make_shared<IdentName>("x");
    b->m_right = std::make_shared<Literal>(ParserToken::Kind::IntLiteral, "3");
    std::vector<AstNodePtr> seq;
    auto xTerm =
        Term::Create(TermID::NAME, "x", MapperRange(m_ctx.source().makeLoc(input_file, 1), m_ctx.source().makeLoc(input_file, 8)), parser::token_type::NAME);
    seq.push_back(std::make_shared<VarDecl>(std::move(xTerm), nullptr, std::make_shared<Literal>(ParserToken::Kind::IntLiteral, "0")));
    seq.push_back(std::move(b));

    SemanticPassRunner runner(m_ctx);
    ASSERT_TRUE(runner.run(seq));

    MapperFile out_idx = m_ctx.source().add_output("test_sub.cpp", true);
    ASSERT_FALSE(out_idx.isInvalid());

    CppTranspiler gen(m_ctx);
    gen.generateToFile(seq, out_idx);

    std::string result = m_ctx.source().output_result(out_idx);
    EXPECT_TRUE(result.find("x -= 3;") != std::string::npos) << "result: " << result;

    auto* reader = m_ctx.source().toReader();
    ASSERT_NE(reader, nullptr);
}

/// Test: expression statement with compound multiplication
TEST_F(TranspilerTest, GenerateSemicolonStmtCompoundMul) {

    MapperFile input_file = m_ctx.source().add_source("test.src", "x := 0; x *= 3;", true);
    MapperRange range(m_ctx.source().makeLoc(input_file, 9), m_ctx.source().makeLoc(input_file, 16));

    auto opTerm = Term::Create(TermID::ASSIGN, "*=", range, parser::token_type::END);
    auto b = std::make_shared<Binary>(ParserToken::Kind::AssignOp, std::move(opTerm));
    b->m_left = std::make_shared<IdentName>("x");
    b->m_right = std::make_shared<Literal>(ParserToken::Kind::IntLiteral, "3");
    std::vector<AstNodePtr> seq;
    auto xTerm =
        Term::Create(TermID::NAME, "x", MapperRange(m_ctx.source().makeLoc(input_file, 1), m_ctx.source().makeLoc(input_file, 8)), parser::token_type::NAME);
    seq.push_back(std::make_shared<VarDecl>(std::move(xTerm), nullptr, std::make_shared<Literal>(ParserToken::Kind::IntLiteral, "0")));
    seq.push_back(std::move(b));

    SemanticPassRunner runner(m_ctx);
    ASSERT_TRUE(runner.run(seq));

    MapperFile out_idx = m_ctx.source().add_output("test_mul.cpp", true);
    ASSERT_FALSE(out_idx.isInvalid());

    CppTranspiler gen(m_ctx);
    gen.generateToFile(seq, out_idx);

    std::string result = m_ctx.source().output_result(out_idx);
    EXPECT_TRUE(result.find("x *= 3;") != std::string::npos) << "result: " << result;

    auto* reader = m_ctx.source().toReader();
    ASSERT_NE(reader, nullptr);
}

/// Test: expression statement with compound division
TEST_F(TranspilerTest, GenerateSemicolonStmtCompoundDiv) {

    MapperFile input_file = m_ctx.source().add_source("test.src", "x := 0; x /= 3;", true);
    MapperRange range(m_ctx.source().makeLoc(input_file, 9), m_ctx.source().makeLoc(input_file, 16));

    auto opTerm = Term::Create(TermID::ASSIGN, "/=", range, parser::token_type::END);
    auto b = std::make_shared<Binary>(ParserToken::Kind::AssignOp, std::move(opTerm));
    b->m_left = std::make_shared<IdentName>("x");
    b->m_right = std::make_shared<Literal>(ParserToken::Kind::IntLiteral, "3");
    std::vector<AstNodePtr> seq;
    auto xTerm =
        Term::Create(TermID::NAME, "x", MapperRange(m_ctx.source().makeLoc(input_file, 1), m_ctx.source().makeLoc(input_file, 8)), parser::token_type::NAME);
    seq.push_back(std::make_shared<VarDecl>(std::move(xTerm), nullptr, std::make_shared<Literal>(ParserToken::Kind::IntLiteral, "0")));
    seq.push_back(std::move(b));

    SemanticPassRunner runner(m_ctx);
    ASSERT_TRUE(runner.run(seq));

    MapperFile out_idx = m_ctx.source().add_output("test_div.cpp", true);
    ASSERT_FALSE(out_idx.isInvalid());

    CppTranspiler gen(m_ctx);
    gen.generateToFile(seq, out_idx);

    std::string result = m_ctx.source().output_result(out_idx);
    EXPECT_TRUE(result.find("x /= 3;") != std::string::npos) << "result: " << result;

    auto* reader = m_ctx.source().toReader();
    ASSERT_NE(reader, nullptr);
}

/// Test: expression statement with compound remainder
TEST_F(TranspilerTest, GenerateSemicolonStmtCompoundRem) {

    MapperFile input_file = m_ctx.source().add_source("test.src", "x := 0; x %= 3;", true);
    MapperRange range(m_ctx.source().makeLoc(input_file, 9), m_ctx.source().makeLoc(input_file, 16));

    auto opTerm = Term::Create(TermID::ASSIGN, "%=", range, parser::token_type::END);
    auto b = std::make_shared<Binary>(ParserToken::Kind::AssignOp, std::move(opTerm));
    b->m_left = std::make_shared<IdentName>("x");
    b->m_right = std::make_shared<Literal>(ParserToken::Kind::IntLiteral, "3");
    std::vector<AstNodePtr> seq;
    auto xTerm =
        Term::Create(TermID::NAME, "x", MapperRange(m_ctx.source().makeLoc(input_file, 1), m_ctx.source().makeLoc(input_file, 8)), parser::token_type::NAME);
    seq.push_back(std::make_shared<VarDecl>(std::move(xTerm), nullptr, std::make_shared<Literal>(ParserToken::Kind::IntLiteral, "0")));
    seq.push_back(std::move(b));

    SemanticPassRunner runner(m_ctx);
    ASSERT_TRUE(runner.run(seq));

    MapperFile out_idx = m_ctx.source().add_output("test_rem.cpp", true);
    ASSERT_FALSE(out_idx.isInvalid());

    CppTranspiler gen(m_ctx);
    gen.generateToFile(seq, out_idx);

    std::string result = m_ctx.source().output_result(out_idx);
    EXPECT_TRUE(result.find("x %= 3;") != std::string::npos) << "result: " << result;

    auto* reader = m_ctx.source().toReader();
    ASSERT_NE(reader, nullptr);
}

/// Test: integer division operator // → static_cast<int64_t>(x) / static_cast<int64_t>(y)
TEST_F(TranspilerTest, GenerateExprIntDiv) {

    MapperFile input_file = m_ctx.source().add_source("test.src", "x := 0; x // 3;", true);
    MapperRange range(m_ctx.source().makeLoc(input_file, 9), m_ctx.source().makeLoc(input_file, 16));

    auto opTerm = Term::Create(TermID::OP_MATH, "//", range, parser::token_type::END);
    auto b = std::make_shared<Binary>(ParserToken::Kind::MathOp, std::move(opTerm));
    b->m_left = std::make_shared<IdentName>("x");
    b->m_right = std::make_shared<Literal>(ParserToken::Kind::IntLiteral, "3");
    std::vector<AstNodePtr> seq;
    auto xTerm =
        Term::Create(TermID::NAME, "x", MapperRange(m_ctx.source().makeLoc(input_file, 1), m_ctx.source().makeLoc(input_file, 8)), parser::token_type::NAME);
    seq.push_back(std::make_shared<VarDecl>(std::move(xTerm), nullptr, std::make_shared<Literal>(ParserToken::Kind::IntLiteral, "0")));
    seq.push_back(std::move(b));

    SemanticPassRunner runner(m_ctx);
    ASSERT_TRUE(runner.run(seq));

    MapperFile out_idx = m_ctx.source().add_output("test_intdiv.cpp", true);
    ASSERT_FALSE(out_idx.isInvalid());

    CppTranspiler gen(m_ctx);
    gen.generateToFile(seq, out_idx);

    std::string result = m_ctx.source().output_result(out_idx);
    EXPECT_TRUE(result.find("static_cast<int64_t>(c_x) / static_cast<int64_t>(3)") != std::string::npos) << "result: " << result;

    auto* reader = m_ctx.source().toReader();
    ASSERT_NE(reader, nullptr);
}

/// Test: compound integer division //= → x = static_cast<int64_t>(x) / y
TEST_F(TranspilerTest, GenerateSemicolonStmtCompoundIntDiv) {

    MapperFile input_file = m_ctx.source().add_source("test.src", "x := 0; x //= 3;", true);
    MapperRange range(m_ctx.source().makeLoc(input_file, 9), m_ctx.source().makeLoc(input_file, 17));

    auto opTerm = Term::Create(TermID::ASSIGN, "//=", range, parser::token_type::END);
    auto b = std::make_shared<Binary>(ParserToken::Kind::AssignOp, std::move(opTerm));
    b->m_left = std::make_shared<IdentName>("x");
    b->m_right = std::make_shared<Literal>(ParserToken::Kind::IntLiteral, "3");
    std::vector<AstNodePtr> seq;
    auto xTerm =
        Term::Create(TermID::NAME, "x", MapperRange(m_ctx.source().makeLoc(input_file, 1), m_ctx.source().makeLoc(input_file, 8)), parser::token_type::NAME);
    seq.push_back(std::make_shared<VarDecl>(std::move(xTerm), nullptr, std::make_shared<Literal>(ParserToken::Kind::IntLiteral, "0")));
    seq.push_back(std::move(b));

    SemanticPassRunner runner(m_ctx);
    ASSERT_TRUE(runner.run(seq));

    MapperFile out_idx = m_ctx.source().add_output("test_intdiv_assign.cpp", true);
    ASSERT_FALSE(out_idx.isInvalid());

    CppTranspiler gen(m_ctx);
    gen.generateToFile(seq, out_idx);

    std::string result = m_ctx.source().output_result(out_idx);
    EXPECT_TRUE(result.find("x = static_cast<int64_t>(c_x) / static_cast<int64_t>(3)") != std::string::npos) << "result: " << result;

    auto* reader = m_ctx.source().toReader();
    ASSERT_NE(reader, nullptr);
}

/// Test: function declaration with Int8 param, Void return
TEST_F(TranspilerTest, GenerateFuncDeclInt8Param) {

    MapperFile input_file = m_ctx.source().add_source("test.src", "%func(arg:Int8):Void := { };", true);
    MapperRange range(m_ctx.source().makeLoc(input_file, 1), m_ctx.source().makeLoc(input_file, 5));

    auto funcTerm = Term::Create(TermID::NAME, "%func", range, parser::token_type::NAME);
    auto func = std::make_shared<FuncDecl>(std::move(funcTerm));
    func->m_params = std::vector<AstNodePtr>{};
    MapperRange param_range(m_ctx.source().makeLoc(input_file, 6), m_ctx.source().makeLoc(input_file, 9));
    auto paramTerm = Term::Create(TermID::NAME, "arg", param_range, parser::token_type::NAME);
    auto param = std::make_shared<ArgNode>(std::move(paramTerm), std::make_shared<IdentType>("Int8"));
    func->m_params->push_back(param);
    func->m_type = std::make_shared<IdentType>("None");
    func->m_body = std::vector<AstNodePtr>{};

    std::vector<AstNodePtr> seq;
    seq.push_back(std::move(func));

    {
        SemanticPassRunner runner(m_ctx);
        runner.run(seq); // register function name, result ignored for codegen test
    }

    MapperFile out_idx = m_ctx.source().add_output("test_func.cpp", true);
    ASSERT_FALSE(out_idx.isInvalid());

    CppTranspiler gen(m_ctx);
    gen.generateToFile(seq, out_idx);

    std::string result = m_ctx.source().output_result(out_idx);
    EXPECT_TRUE(result.find("void func(int8_t c_arg)") != std::string::npos) << "result: " << result;
    EXPECT_TRUE(result.find("{\n}") != std::string::npos) << "result: " << result;

    auto* reader = m_ctx.source().toReader();
    ASSERT_NE(reader, nullptr);
}

/// Test: function declaration with multiple params and return type
TEST_F(TranspilerTest, GenerateFuncDeclTyped) {

    MapperFile input_file = m_ctx.source().add_source("test.src", "%func(a:Int32, b:Byte):Int64 := { ++ 0.0. ++; };", true);
    MapperRange range(m_ctx.source().makeLoc(input_file, 1), m_ctx.source().makeLoc(input_file, 5));
    MapperRange ret_range(m_ctx.source().makeLoc(input_file, 30), m_ctx.source().makeLoc(input_file, 45));

    auto funcTerm = Term::Create(TermID::NAME, "%func", range, parser::token_type::NAME);
    auto func = std::make_shared<FuncDecl>(std::move(funcTerm));
    func->m_params = std::vector<AstNodePtr>{};
    MapperRange p1_range(m_ctx.source().makeLoc(input_file, 7), m_ctx.source().makeLoc(input_file, 8));
    auto p1Term = Term::Create(TermID::NAME, "a", p1_range, parser::token_type::NAME);
    auto p1 = std::make_shared<ArgNode>(std::move(p1Term), std::make_shared<IdentType>("Int32"));
    func->m_params->push_back(p1);
    MapperRange p2_range(m_ctx.source().makeLoc(input_file, 7), m_ctx.source().makeLoc(input_file, 8));
    auto p2Term = Term::Create(TermID::NAME, "b", p2_range, parser::token_type::NAME);
    auto p2 = std::make_shared<ArgNode>(std::move(p2Term), std::make_shared<IdentType>("Byte"));
    func->m_params->push_back(p2);
    func->m_type = std::make_shared<IdentType>("Int64");

    auto retTerm = Term::Create(TermID::UNKNOWN, "++", ret_range, parser::token_type::END);
    auto ret = std::make_shared<JumpStmt>(ParserToken::Kind::ReturnStmt, std::move(retTerm));
    auto val_ptr = std::make_shared<Literal>(ParserToken::Kind::IntLiteral, "0");
    ret->m_value = std::move(val_ptr);
    func->m_body = std::vector<AstNodePtr>{};
    func->m_body->push_back(ret);

    std::vector<AstNodePtr> seq;
    seq.push_back(std::move(func));

    {
        SemanticPassRunner runner(m_ctx);
        runner.run(seq); // register function name, result ignored for codegen test
    }

    MapperFile out_idx = m_ctx.source().add_output("test_func2.cpp", true);
    ASSERT_FALSE(out_idx.isInvalid());

    CppTranspiler gen(m_ctx);
    gen.generateToFile(seq, out_idx);

    std::string result = m_ctx.source().output_result(out_idx);
    // Возврат — зарегистрированный тип Int64 → каноническое C++-имя int64_t.
    EXPECT_TRUE(result.find("int64_t func(int32_t c_a, uint8_t c_b)") != std::string::npos) << "result: " << result;
    EXPECT_TRUE(result.find("return 0;") != std::string::npos) << "result: " << result;

    auto* reader = m_ctx.source().toReader();
    ASSERT_NE(reader, nullptr);
}

/// Test: forward function declaration (no body)
TEST_F(TranspilerTest, GenerateFuncDeclForward) {

    MapperFile input_file = m_ctx.source().add_source("test.src", "%forward_func():Void := ;", true);
    MapperRange range(m_ctx.source().makeLoc(input_file, 1), m_ctx.source().makeLoc(input_file, 13));

    auto funcTerm = Term::Create(TermID::NAME, "%forward_func", range, parser::token_type::NAME);
    auto func = std::make_shared<FuncDecl>(std::move(funcTerm));
    func->m_type = std::make_shared<IdentType>("Void");
    // no m_body = forward decl

    std::vector<AstNodePtr> seq;
    seq.push_back(std::move(func));

    {
        SemanticPassRunner runner(m_ctx);
        runner.run(seq); // register function name, result ignored for codegen test
    }

    MapperFile out_idx = m_ctx.source().add_output("test_forward.cpp", true);
    ASSERT_FALSE(out_idx.isInvalid());

    CppTranspiler gen(m_ctx);
    gen.generateToFile(seq, out_idx);

    std::string result = m_ctx.source().output_result(out_idx);
    EXPECT_TRUE(result.find("void forward_func()") != std::string::npos) << "result: " << result;
    EXPECT_TRUE(result.find(";\n") != std::string::npos) << "result: " << result;

    auto* reader = m_ctx.source().toReader();
    ASSERT_NE(reader, nullptr);
}

/// Test: forward function declaration with Int32 return and params → декларация + #include <cstdint>
TEST_F(TranspilerTest, GenerateForwardFuncDeclInt32) {
    MapperFile input_file = m_ctx.source().add_source("test.src", "%add(a:Int32, b:Int32):Int32 := ...;", true);
    MapperRange range(m_ctx.source().makeLoc(input_file, 1), m_ctx.source().makeLoc(input_file, 5));

    auto funcTerm = Term::Create(TermID::NAME, "%add", range, parser::token_type::NAME);
    auto func = std::make_shared<FuncDecl>(std::move(funcTerm));
    func->m_params = std::vector<AstNodePtr>{};
    MapperRange p1(m_ctx.source().makeLoc(input_file, 7), m_ctx.source().makeLoc(input_file, 8));
    func->m_params->push_back(
        std::make_shared<ArgNode>(std::move(Term::Create(TermID::NAME, "a", p1, parser::token_type::NAME)), std::make_shared<IdentType>("Int32")));
    MapperRange p2(m_ctx.source().makeLoc(input_file, 10), m_ctx.source().makeLoc(input_file, 11));
    func->m_params->push_back(
        std::make_shared<ArgNode>(std::move(Term::Create(TermID::NAME, "b", p2, parser::token_type::NAME)), std::make_shared<IdentType>("Int32")));
    func->m_type = std::make_shared<IdentType>("Int32");
    // no m_body = forward decl

    std::vector<AstNodePtr> seq;
    seq.push_back(std::move(func));

    {
        SemanticPassRunner runner(m_ctx);
        ASSERT_TRUE(runner.run(seq));
    }

    MapperFile out_idx = m_ctx.source().add_output("test_forward_int32.cpp", true);
    ASSERT_FALSE(out_idx.isInvalid());

    CppTranspiler gen(m_ctx);
    gen.generateToFile(seq, out_idx);

    std::string result = m_ctx.source().output_result(out_idx);
    EXPECT_TRUE(result.find("int32_t add(int32_t c_a, int32_t c_b);") != std::string::npos) << "result: " << result;
    EXPECT_TRUE(result.find("#include <cstdint>") != std::string::npos) << "result: " << result;

    auto* reader = m_ctx.source().toReader();
    ASSERT_NE(reader, nullptr);
}

/// Test: forward variable declaration (x:Int32 := ...; → extern int32_t c_x;)
TEST_F(TranspilerTest, GenerateForwardVarDecl) {
    MapperFile input_file = m_ctx.source().add_source("test.src", "x:Int32 := ...;", true);
    MapperRange range(m_ctx.source().makeLoc(input_file, 1), m_ctx.source().makeLoc(input_file, 2));

    auto nameTerm = Term::Create(TermID::NAME, "x", range, parser::token_type::NAME);
    auto var = std::make_shared<VarDecl>(std::move(nameTerm), std::make_shared<IdentType>("Int32"), nullptr);
    std::vector<AstNodePtr> seq;
    seq.push_back(std::move(var));

    {
        SemanticPassRunner runner(m_ctx);
        ASSERT_TRUE(runner.run(seq));
    }

    MapperFile out_idx = m_ctx.source().add_output("test_fwdvar.cpp", true);
    ASSERT_FALSE(out_idx.isInvalid());

    CppTranspiler gen(m_ctx);
    gen.generateToFile(seq, out_idx);

    std::string result = m_ctx.source().output_result(out_idx);
    EXPECT_TRUE(result.find("extern int32_t c_x;") != std::string::npos) << "result: " << result;

    auto* reader = m_ctx.source().toReader();
    ASSERT_NE(reader, nullptr);
}

/// Test: native variable `%x:Int32 := 5;` → манглинг `%` в C++-имя `int32_t x = 5;`.
TEST_F(TranspilerTest, GenerateNativeVarManglesPercent) {
    MapperFile input_file = m_ctx.source().add_source("test.src", "%x:Int32 := 5;", true);
    MapperRange range(m_ctx.source().makeLoc(input_file, 1), m_ctx.source().makeLoc(input_file, 2));

    auto nameTerm = Term::Create(TermID::NAME, "%x", range, parser::token_type::NAME);
    auto var =
        std::make_shared<VarDecl>(std::move(nameTerm), std::make_shared<IdentType>("Int32"), std::make_shared<Literal>(ParserToken::Kind::IntLiteral, "5"));
    std::vector<AstNodePtr> seq;
    seq.push_back(std::move(var));

    {
        SemanticPassRunner runner(m_ctx);
        ASSERT_TRUE(runner.run(seq));
    }

    MapperFile out_idx = m_ctx.source().add_output("test_nativevar.cpp", true);
    ASSERT_FALSE(out_idx.isInvalid());

    CppTranspiler gen(m_ctx);
    gen.generateToFile(seq, out_idx);

    std::string result = m_ctx.source().output_result(out_idx);
    EXPECT_TRUE(result.find("int32_t x = 5;") != std::string::npos) << "result: " << result;
    EXPECT_EQ(result.find("%x"), std::string::npos) << "нативное '%' не должно попадать в C++: " << result;

    auto* reader = m_ctx.source().toReader();
    ASSERT_NE(reader, nullptr);
}

/// Test: throw statement (-- expr -- → throw expr;)
TEST_F(TranspilerTest, GenerateThrowStmt) {
    MapperFile input_file = m_ctx.source().add_source("test.src", "-- value --;", true);
    MapperRange range(m_ctx.source().makeLoc(input_file, 1), m_ctx.source().makeLoc(input_file, 14));

    auto opTerm = Term::Create(TermID::INT_MINUS, "--", range, parser::token_type::END);
    auto stmt = std::make_shared<JumpStmt>(ParserToken::Kind::ThrowStmt, std::move(opTerm));
    stmt->m_value = std::make_shared<IdentName>("value");
    std::vector<AstNodePtr> seq;
    seq.push_back(std::move(stmt));

    {
        SemanticPassRunner runner(m_ctx);
        runner.run(seq);
    }

    MapperFile out_idx = m_ctx.source().add_output("test_throw.cpp", true);
    ASSERT_FALSE(out_idx.isInvalid());

    CppTranspiler gen(m_ctx);
    gen.generateToFile(seq, out_idx);

    std::string result = m_ctx.source().output_result(out_idx);
    EXPECT_TRUE(result.find("throw c_value;") != std::string::npos) << "result: " << result;
}

/// Test: throw void (-- -- → throw;)
TEST_F(TranspilerTest, GenerateThrowVoid) {
    MapperFile input_file = m_ctx.source().add_source("test.src", "-- --;", true);
    MapperRange range(m_ctx.source().makeLoc(input_file, 1), m_ctx.source().makeLoc(input_file, 7));

    auto opTerm = Term::Create(TermID::INT_MINUS, "--", range, parser::token_type::END);
    auto stmt = std::make_shared<JumpStmt>(ParserToken::Kind::ThrowStmt, std::move(opTerm));
    std::vector<AstNodePtr> seq;
    seq.push_back(std::move(stmt));

    {
        SemanticPassRunner runner(m_ctx);
        runner.run(seq);
    }

    MapperFile out_idx = m_ctx.source().add_output("test_throw_void.cpp", true);
    ASSERT_FALSE(out_idx.isInvalid());

    CppTranspiler gen(m_ctx);
    gen.generateToFile(seq, out_idx);

    std::string result = m_ctx.source().output_result(out_idx);
    EXPECT_TRUE(result.find("throw;") != std::string::npos) << "result: " << result;
}

/// Test: standalone literal in sequence (42 → 42;)
TEST_F(TranspilerTest, GenerateStandaloneLiteral) {
    MapperFile input_file = m_ctx.source().add_source("test.src", "42;", true);
    MapperRange range(m_ctx.source().makeLoc(input_file, 1), m_ctx.source().makeLoc(input_file, 4));

    auto litTerm = Term::Create(TermID::INTEGER, "42", range, parser::token_type::END);
    std::vector<AstNodePtr> seq;
    seq.push_back(std::make_shared<Literal>(ParserToken::Kind::IntLiteral, std::move(litTerm)));

    {
        SemanticPassRunner runner(m_ctx);
        runner.run(seq);
    }

    MapperFile out_idx = m_ctx.source().add_output("test_literal.cpp", true);
    ASSERT_FALSE(out_idx.isInvalid());

    CppTranspiler gen(m_ctx);
    gen.generateToFile(seq, out_idx);

    std::string result = m_ctx.source().output_result(out_idx);
    EXPECT_TRUE(result.find("42;") != std::string::npos) << "result: " << result;
}

/// Test: standalone narrow string literal ('hello' → "hello";)
TEST_F(TranspilerTest, GenerateStandaloneStringLiteral) {
    MapperFile input_file = m_ctx.source().add_source("test.src", "'hello';", true);
    MapperRange range(m_ctx.source().makeLoc(input_file, 1), m_ctx.source().makeLoc(input_file, 9));

    auto litTerm = Term::Create(TermID::STRCHAR, "hello", range, parser::token_type::END);
    std::vector<AstNodePtr> seq;
    seq.push_back(std::make_shared<Literal>(ParserToken::Kind::StrChar, std::move(litTerm)));

    {
        SemanticPassRunner runner(m_ctx);
        runner.run(seq);
    }

    MapperFile out_idx = m_ctx.source().add_output("test_str_literal.cpp", true);
    ASSERT_FALSE(out_idx.isInvalid());

    CppTranspiler gen(m_ctx);
    gen.generateToFile(seq, out_idx);

    std::string result = m_ctx.source().output_result(out_idx);
    EXPECT_TRUE(result.find("\"hello\";") != std::string::npos) << "result: " << result;
}

/// Test: exports() contains variable declarations and function declarations
TEST_F(TranspilerTest, GenerateExports) {
    MapperFile input_file = m_ctx.source().add_source("test.src", "x := 42; %foo():Void := { };", true);
    MapperRange var_range(m_ctx.source().makeLoc(input_file, 1), m_ctx.source().makeLoc(input_file, 9));
    MapperRange func_range(m_ctx.source().makeLoc(input_file, 10), m_ctx.source().makeLoc(input_file, 28));

    std::string varName = "x";
    auto varTerm = Term::Create(TermID::NAME, varName, var_range, parser::token_type::NAME);
    auto var = std::make_shared<VarDecl>(std::move(varTerm), nullptr, std::make_shared<Literal>(ParserToken::Kind::IntLiteral, "42"));

    auto funcTerm = Term::Create(TermID::NAME, "%foo", func_range, parser::token_type::NAME);
    auto func = std::make_shared<FuncDecl>(std::move(funcTerm));
    func->m_params = std::vector<AstNodePtr>{};
    func->m_type = std::make_shared<IdentType>("None");
    func->m_body = std::vector<AstNodePtr>{};

    std::vector<AstNodePtr> seq;
    seq.push_back(std::move(var));
    seq.push_back(std::move(func));

    {
        SemanticPassRunner runner(m_ctx);
        runner.run(seq);
    }

    MapperFile out_idx = m_ctx.source().add_output("test_exports.cpp", true);
    ASSERT_FALSE(out_idx.isInvalid());

    CppTranspiler gen(m_ctx);
    gen.generateToFile(seq, out_idx);

    const auto& exports = gen.exports();
    ASSERT_EQ(exports.size(), 2u);
    EXPECT_EQ(exports[0].trustName, "x");
    EXPECT_EQ(exports[0].cppName, "c_x");
    EXPECT_EQ(exports[1].trustName, "%foo");
    EXPECT_EQ(exports[1].cppName, "foo");
}

/// Test: Sequence node walks body (nested declarations)
TEST_F(TranspilerTest, GenerateSequenceWalk) {
    MapperFile input_file = m_ctx.source().add_source("test.src", "{ x := 1; y := 2; }", true);
    MapperRange range(m_ctx.source().makeLoc(input_file, 1), m_ctx.source().makeLoc(input_file, 22));

    MapperRange range_x(m_ctx.source().makeLoc(input_file, 3), m_ctx.source().makeLoc(input_file, 10));
    MapperRange range_y(m_ctx.source().makeLoc(input_file, 12), m_ctx.source().makeLoc(input_file, 19));

    auto seqTerm = Term::Create(TermID::SEQUENCE, "", range, parser::token_type::END);
    auto seq_node = std::make_shared<Sequence>(ParserToken::Kind::sequence, std::move(seqTerm));

    std::string xName = "x";
    auto xTerm = Term::Create(TermID::NAME, xName, range_x, parser::token_type::NAME);
    seq_node->m_body.push_back(std::make_shared<VarDecl>(std::move(xTerm), nullptr, std::make_shared<Literal>(ParserToken::Kind::IntLiteral, "1")));

    std::string yName = "y";
    auto yTerm = Term::Create(TermID::NAME, yName, range_y, parser::token_type::NAME);
    seq_node->m_body.push_back(std::make_shared<VarDecl>(std::move(yTerm), nullptr, std::make_shared<Literal>(ParserToken::Kind::IntLiteral, "2")));

    std::vector<AstNodePtr> seq;
    seq.push_back(std::move(seq_node));

    {
        SemanticPassRunner runner(m_ctx);
        runner.run(seq);
    }

    MapperFile out_idx = m_ctx.source().add_output("test_seq.cpp", true);
    ASSERT_FALSE(out_idx.isInvalid());

    CppTranspiler gen(m_ctx);
    gen.generateToFile(seq, out_idx);

    std::string result = m_ctx.source().output_result(out_idx);
    EXPECT_TRUE(result.find("bool c_x = 1;") != std::string::npos) << "result: " << result;
    EXPECT_TRUE(result.find("int8_t c_y = 2;") != std::string::npos) << "result: " << result;
}

/// Test: narrow string literal with embedded quote is escaped ('a"b' → "a\"b")
TEST_F(TranspilerTest, GenerateStringWithEscapedQuote) {
    MapperFile input_file = m_ctx.source().add_source("test.src", "s := 'a\"b';", true);
    MapperRange range(m_ctx.source().makeLoc(input_file, 1), m_ctx.source().makeLoc(input_file, 7));

    std::string name = "s";
    auto term = Term::Create(TermID::NAME, name, range, parser::token_type::NAME);
    auto var = std::make_shared<VarDecl>(std::move(term), nullptr, std::make_shared<Literal>(ParserToken::Kind::StrChar, "a\\\"b"));
    std::vector<AstNodePtr> seq;
    seq.push_back(std::move(var));

    {
        SemanticPassRunner runner(m_ctx);
        runner.run(seq);
    }

    MapperFile out_idx = m_ctx.source().add_output("test_esc.cpp", true);
    ASSERT_FALSE(out_idx.isInvalid());

    CppTranspiler gen(m_ctx);
    gen.generateToFile(seq, out_idx);

    std::string result = m_ctx.source().output_result(out_idx);
    EXPECT_TRUE(result.find("std::string c_s = \"a\\\"b\";") != std::string::npos) << "result: " << result;
}

/// Test: атрибут иммутабельности (attr::ReadOnly) не меняет генерируемый код (current behaviour).
TEST_F(TranspilerTest, GenerateReadOnlyAttrUnchanged) {
    MapperFile input_file = m_ctx.source().add_source("test.src", "x := 42;", true);
    MapperRange range(m_ctx.source().makeLoc(input_file, 1), m_ctx.source().makeLoc(input_file, 13));

    std::string name = "x";
    auto term = Term::Create(TermID::NAME, name, range, parser::token_type::NAME);
    auto var = std::make_shared<VarDecl>(std::move(term), nullptr, std::make_shared<Literal>(ParserToken::Kind::IntLiteral, "42"));
    var->add_attr(m_ctx.attrs().lookup(attr::ReadOnly).value()); // иммутабельная переменная (атрибут)
    std::vector<AstNodePtr> seq;
    seq.push_back(std::move(var));

    {
        SemanticPassRunner runner(m_ctx);
        runner.run(seq);
    }

    MapperFile out_idx = m_ctx.source().add_output("test_mut.cpp", true);
    ASSERT_FALSE(out_idx.isInvalid());

    CppTranspiler gen(m_ctx);
    gen.generateToFile(seq, out_idx);

    std::string result = m_ctx.source().output_result(out_idx);
    EXPECT_TRUE(result.find("const int8_t c_x = 42;") != std::string::npos) << "result: " << result;
}

/// Test: `x := :Int32` невалидно — в `:=` справа должно быть значение, а не тип-имя
/// (тип объявляется через `::=`). Семантика выдаёт явную ошибку.
TEST_F(TranspilerTest, GenerateTypeNameExprIsError) {
    MapperFile input_file = m_ctx.source().add_source("test.src", "x := :Int32;", true);
    MapperRange range(m_ctx.source().makeLoc(input_file, 1), m_ctx.source().makeLoc(input_file, 13));

    std::string name = "x";
    auto term = Term::Create(TermID::NAME, name, range, parser::token_type::NAME);
    auto var = std::make_shared<VarDecl>(std::move(term), nullptr, std::make_shared<IdentType>("Int32"));
    std::vector<AstNodePtr> seq;
    seq.push_back(std::move(var));

    SemanticPassRunner runner(m_ctx);
    EXPECT_FALSE(runner.run(seq));
    EXPECT_GT(m_ctx.diag().errorCount(), 0);
}

/// Test: нетипизированная переменная с инициализатором, у которой тип не выведен (семантика не
/// запускалась → inferredType INVALID) и таблица символов недоступна → ЯВНАЯ диагностика, а НЕ
/// тихий fallback на std::any (AGENTS rule 5 / без fallback).
TEST_F(TranspilerTest, GenerateUntypedVarUnknownTypeIsErrorNoFallback) {
    MapperFile input_file = m_ctx.source().add_source("test.src", "x := 42;", true);
    MapperRange range(m_ctx.source().makeLoc(input_file, 1), m_ctx.source().makeLoc(input_file, 8));

    auto term = Term::Create(TermID::NAME, "x", range, parser::token_type::NAME);
    auto var = std::make_shared<VarDecl>(std::move(term), nullptr, std::make_shared<Literal>(ParserToken::Kind::IntLiteral, "42"));
    std::vector<AstNodePtr> seq;
    seq.push_back(std::move(var));

    MapperFile out_idx = m_ctx.source().add_output("test_err.cpp", true);
    ASSERT_FALSE(out_idx.isInvalid());
    CppTranspiler gen(m_ctx); // без разрешённой таблицы символов
    gen.generateToFile(seq, out_idx);

    // Диагностика ошибки вывода (нет тихого std::any).
    EXPECT_GT(m_ctx.diag().errorCount(), 0);
    std::string result = m_ctx.source().output_result(out_idx);
    EXPECT_EQ(result.find("std::any"), std::string::npos) << "no silent std::any fallback: " << result;
}

/// Test: addNameMapping for variable name is queryable via getCppName.
TEST_F(TranspilerTest, NameMapping_VarDecl) {
    MapperFile input_file = m_ctx.source().add_source("test.src", "x := 42;", true);
    MapperRange range(m_ctx.source().makeLoc(input_file, 1), m_ctx.source().makeLoc(input_file, 2));
    std::string name = "x";
    auto term = Term::Create(TermID::NAME, name, range, parser::token_type::NAME);
    auto var = std::make_shared<VarDecl>(std::move(term), nullptr, std::make_shared<Literal>(ParserToken::Kind::IntLiteral, "42"));
    std::vector<AstNodePtr> seq;
    seq.push_back(std::move(var));

    SemanticPassRunner runner(m_ctx);
    ASSERT_TRUE(runner.run(seq));

    MapperFile out_idx = m_ctx.source().add_output("test_nm_var.cpp", true);
    ASSERT_FALSE(out_idx.isInvalid());
    CppTranspiler gen(m_ctx);
    gen.generateToFile(seq, out_idx);

    auto* reader = m_ctx.source().toReader();
    ASSERT_NE(reader, nullptr);
    auto cppName = reader->getCppName(static_cast<ReaderLocation>(m_ctx.source().makeLoc(input_file, 1)), "x");
    ASSERT_TRUE(cppName.has_value());
    EXPECT_EQ(cppName->toName, "c_x");
    // The cpp range must point at the variable name 'x' in the generated output.
    EXPECT_EQ(reader->getText(cppName->rangeMap.to), "c_x");
}

/// Test: addNameMapping for type name is queryable via getCppName.
TEST_F(TranspilerTest, NameMapping_TypeDecl) {
    MapperFile input_file = m_ctx.source().add_source("test.src", "MyInt ::= :Int32;", true);
    MapperRange range(m_ctx.source().makeLoc(input_file, 1), m_ctx.source().makeLoc(input_file, 18));
    auto opTerm = Term::Create(TermID::CREATE_TYPE, "::=", range, parser::token_type::END);
    auto b = std::make_shared<Binary>(ParserToken::Kind::TypeDecl, std::move(opTerm));
    const std::string leftName = "MyInt";
    MapperRange left_range(m_ctx.source().makeLoc(input_file, 1), m_ctx.source().makeLoc(input_file, 6));
    auto leftTerm = Term::Create(TermID::NAME, leftName, left_range, parser::token_type::NAME);
    b->m_left = std::make_shared<IdentName>(std::move(leftTerm));
    const std::string rightName = "Int32";
    auto rightTerm = Term::Create(TermID::NAME, rightName, {}, parser::token_type::NAME);
    b->m_right = std::make_shared<IdentType>(std::move(rightTerm));
    std::vector<AstNodePtr> seq;
    seq.push_back(std::move(b));

    SemanticPassRunner runner(m_ctx);
    ASSERT_TRUE(runner.run(seq));

    MapperFile out_idx = m_ctx.source().add_output("test_nm_type.cpp", true);
    ASSERT_FALSE(out_idx.isInvalid());
    CppTranspiler gen(m_ctx);
    gen.generateToFile(seq, out_idx);

    auto* reader = m_ctx.source().toReader();
    ASSERT_NE(reader, nullptr);
    auto cppName = reader->getCppName(static_cast<ReaderLocation>(m_ctx.source().makeLoc(input_file, 1)), "MyInt");
    ASSERT_TRUE(cppName.has_value());
    EXPECT_EQ(cppName->toName, "c_MyInt");
    // The cpp range must point at the type name 'MyInt' in the generated output.
    EXPECT_EQ(reader->getText(cppName->rangeMap.to), "c_MyInt");
}

/// Test: addNameMapping + range-mapping для объявления типа через алиас-цепочку
/// (Big ::= MyInt;). Новый тип Big должен маппиться как statement (using c_Big = c_MyInt;)
/// и регистрировать своё имя в name-маппинге (hover/definition).
TEST_F(TranspilerTest, NameMapping_TypeDeclAliasChain) {
    const std::string name = "aliasmap.src";
    // "Big" — строка 2, offset 19 (1-based).
    const std::string src = "MyInt ::= :Int32;\nBig ::= MyInt;\n";

    MapperFile input = m_ctx.source().add_source(name, src, true);
    MapperFile out = m_ctx.source().add_output(name + ".cpp", true);
    ASSERT_FALSE(out.isInvalid());

    PipelineOpts opts;
    opts.no_dsl = true;
    Pipeline pipeline(m_ctx, opts);
    std::vector<CppTranspiler::ExportEntry> exports;
    auto res = pipeline.runPipeline(PipelineSteps::ParseAST | PipelineSteps::Semantic | PipelineSteps::Transpile, input, out, &exports);
    ASSERT_TRUE(res.isValid());
    EXPECT_EQ(m_ctx.diag().errorCount(), 0);

    const std::string cpp = m_ctx.source().output_result(out);
    EXPECT_NE(cpp.find("using c_Big = c_MyInt;"), std::string::npos) << cpp;

    auto* reader = m_ctx.source().toReader();
    ASSERT_NE(reader, nullptr);

    // Имя Big зарегистрировано в name-маппинге: trust "Big" → cpp "Big", в выводе текст "Big".
    auto cppBig = reader->getCppName(static_cast<ReaderLocation>(m_ctx.source().makeLoc(input, 19)), "Big");
    ASSERT_TRUE(cppBig.has_value()) << "name mapping for 'Big' not found";
    EXPECT_EQ(cppBig->toName, "c_Big");
    EXPECT_EQ(reader->getText(cppBig->rangeMap.to), "c_Big");

    // Statement-маппинг: доверяем MapperScope — forward-маппинг оператора Big ::= MyInt;
    // ведёт на сгенерированную строку "using c_Big = c_MyInt;".
    bool foundStmt = false;
    for (const auto& [key, m] : reader->getForwardMappings()) {
        (void)key;
        // Range оператора TypeDecl не включает завершающий ';' (это терминатор statement).
        std::string_view fromText = reader->getText(m.from);
        if (fromText == "Big ::= MyInt") {
            foundStmt = true;
            EXPECT_EQ(reader->getText(m.to), "using c_Big = c_MyInt;");
        }
    }
    EXPECT_TRUE(foundStmt) << "statement mapping for 'Big ::= MyInt' not found";
}

/// Test: name-маппинг (hover/definition) для типа Enum и его членов и для типа Variant и его
/// членов: тип → c_<Тип>, член → c_<Член>. Маппинг добавлен в emitEnumStruct/emitVariantStruct
/// (раньше у Enum/Variant его не было вовсе — только у алиасов/переменных/функций).
TEST_F(TranspilerTest, NameMapping_EnumVariantTypeAndMembers) {
    const std::string src = "Level ::= (LOW='low', HIGH='high',):Enum;\nData ::= (i=42, s='text',):Variant;\n";
    MapperFile input = m_ctx.source().add_source("evmap.src", src, true);
    MapperFile out = m_ctx.source().add_output("evmap.cpp", true);
    ASSERT_FALSE(out.isInvalid());
    PipelineOpts opts;
    opts.no_dsl = true;
    Pipeline pipeline(m_ctx, opts);
    auto res = pipeline.runPipeline(PipelineSteps::ParseAST | PipelineSteps::Semantic | PipelineSteps::Transpile, input, out, nullptr);
    ASSERT_TRUE(res.isValid());
    EXPECT_EQ(m_ctx.diag().errorCount(), 0);

    auto* reader = m_ctx.source().toReader();
    ASSERT_NE(reader, nullptr);
    auto loc = [&](size_t off) { return static_cast<ReaderLocation>(m_ctx.source().makeLoc(input, static_cast<uint32_t>(off))); };
    auto check = [&](size_t off, const char* tName, const char* cppName) {
        auto m = reader->getCppName(loc(off), tName);
        ASSERT_TRUE(m.has_value()) << "no name mapping for trust name '" << tName << "'";
        EXPECT_EQ(m->toName, cppName);
        EXPECT_EQ(reader->getText(m->rangeMap.to), cppName);
    };
    // Тип Level (строка 1) и его член LOW; тип Data (строка 2) и его член i.
    check(src.find("Level") + 1, "Level", "c_Level");
    check(src.find("LOW") + 1, "LOW", "c_LOW");
    check(src.find("Data") + 1, "Data", "c_Data");
    check(src.find("i=42") + 1, "i", "c_i");
}

/// Test: name-маппинг (hover) для переменных структурных типов Tuple/Dict/Array: имя переменной
/// маппится на C++-имя (c_t/c_d/c_a) через generateVarDeclToFile (стандартный var-маппинг).
TEST_F(TranspilerTest, NameMapping_TupleDictArrayVars) {
    const std::string src = "t := (1, 2,):Tuple;\nd := (1, two=2, name='3',);\na := [1,2,3,];\n";
    MapperFile input = m_ctx.source().add_source("tdamap.src", src, true);
    MapperFile out = m_ctx.source().add_output("tdamap.cpp", true);
    ASSERT_FALSE(out.isInvalid());
    PipelineOpts opts;
    opts.no_dsl = true;
    Pipeline pipeline(m_ctx, opts);
    auto res = pipeline.runPipeline(PipelineSteps::ParseAST | PipelineSteps::Semantic | PipelineSteps::Transpile, input, out, nullptr);
    ASSERT_TRUE(res.isValid());
    EXPECT_EQ(m_ctx.diag().errorCount(), 0);

    auto* reader = m_ctx.source().toReader();
    ASSERT_NE(reader, nullptr);
    auto loc = [&](size_t off) { return static_cast<ReaderLocation>(m_ctx.source().makeLoc(input, static_cast<uint32_t>(off))); };
    auto check = [&](size_t off, const char* tName, const char* cppName) {
        auto m = reader->getCppName(loc(off), tName);
        ASSERT_TRUE(m.has_value()) << "no name mapping for trust name '" << tName << "'";
        EXPECT_EQ(m->toName, cppName);
        EXPECT_EQ(reader->getText(m->rangeMap.to), cppName);
    };
    check(src.find("t :=") + 1, "t", "c_t");
    check(src.find("d :=") + 1, "d", "c_d");
    check(src.find("a :=") + 1, "a", "c_a");
}

/// Test: addNameMapping for function name is queryable via getCppName.
TEST_F(TranspilerTest, NameMapping_FuncDeclName) {
    MapperFile input_file = m_ctx.source().add_source("test.src", "%func():Void := ;", true);
    MapperRange range(m_ctx.source().makeLoc(input_file, 1), m_ctx.source().makeLoc(input_file, 6));
    auto funcTerm = Term::Create(TermID::NAME, "%func", range, parser::token_type::NAME);
    auto func = std::make_shared<FuncDecl>(std::move(funcTerm));
    func->m_type = std::make_shared<IdentType>("Void");
    std::vector<AstNodePtr> seq;
    seq.push_back(std::move(func));

    SemanticPassRunner runner(m_ctx);
    runner.run(seq);

    MapperFile out_idx = m_ctx.source().add_output("test_nm_func.cpp", true);
    ASSERT_FALSE(out_idx.isInvalid());
    CppTranspiler gen(m_ctx);
    gen.generateToFile(seq, out_idx);

    std::string result = m_ctx.source().output_result(out_idx);
    EXPECT_NE(result.find("void func();"), std::string::npos) << result;

    auto* reader = m_ctx.source().toReader();
    ASSERT_NE(reader, nullptr);
    auto cppName = reader->getCppName(static_cast<ReaderLocation>(m_ctx.source().makeLoc(input_file, 1)), "%func");
    ASSERT_TRUE(cppName.has_value());
    EXPECT_EQ(cppName->toName, "func");
    // The cpp range must point at the function name 'func' in the generated output.
    EXPECT_EQ(reader->getText(cppName->rangeMap.to), "func");
}

/// Test: addNameMapping for function parameters is queryable via getCppName.
TEST_F(TranspilerTest, NameMapping_FuncParams) {
    MapperFile input_file = m_ctx.source().add_source("test.src", "%func(a:Int32,b:Int32):Void := ;", true);
    MapperRange range(m_ctx.source().makeLoc(input_file, 1), m_ctx.source().makeLoc(input_file, 6));
    auto funcTerm = Term::Create(TermID::NAME, "%func", range, parser::token_type::NAME);
    auto func = std::make_shared<FuncDecl>(std::move(funcTerm));
    func->m_params = std::vector<AstNodePtr>{};
    MapperRange a_range(m_ctx.source().makeLoc(input_file, 7), m_ctx.source().makeLoc(input_file, 8));
    auto aTerm = Term::Create(TermID::NAME, "a", a_range, parser::token_type::NAME);
    auto pa = std::make_shared<ArgNode>(std::move(aTerm), std::make_shared<IdentType>("Int32"));
    func->m_params->push_back(pa);
    MapperRange b_range(m_ctx.source().makeLoc(input_file, 15), m_ctx.source().makeLoc(input_file, 16));
    auto bTerm = Term::Create(TermID::NAME, "b", b_range, parser::token_type::NAME);
    auto pb = std::make_shared<ArgNode>(std::move(bTerm), std::make_shared<IdentType>("Int32"));
    func->m_params->push_back(pb);
    func->m_type = std::make_shared<IdentType>("Void");
    std::vector<AstNodePtr> seq;
    seq.push_back(std::move(func));

    SemanticPassRunner runner(m_ctx);
    runner.run(seq);

    MapperFile out_idx = m_ctx.source().add_output("test_nm_params.cpp", true);
    ASSERT_FALSE(out_idx.isInvalid());
    CppTranspiler gen(m_ctx);
    gen.generateToFile(seq, out_idx);

    std::string result = m_ctx.source().output_result(out_idx);
    EXPECT_NE(result.find("void func(int32_t c_a, int32_t c_b);"), std::string::npos) << result;

    auto* reader = m_ctx.source().toReader();
    ASSERT_NE(reader, nullptr);
    auto aCpp = reader->getCppName(static_cast<ReaderLocation>(m_ctx.source().makeLoc(input_file, 7)), "a");
    ASSERT_TRUE(aCpp.has_value());
    EXPECT_EQ(aCpp->toName, "c_a");
    EXPECT_EQ(reader->getText(aCpp->rangeMap.to), "c_a");

    auto bCpp = reader->getCppName(static_cast<ReaderLocation>(m_ctx.source().makeLoc(input_file, 15)), "b");
    ASSERT_TRUE(bCpp.has_value());
    EXPECT_EQ(bCpp->toName, "c_b");
    EXPECT_EQ(reader->getText(bCpp->rangeMap.to), "c_b");
}

/// Test: control-flow (if/else-if/else, while, do-while) — кодогенерация C++ и маппинг range.
TEST_F(TranspilerTest, GenerateControlFlow_MapsRanges) {
    const std::string name = "cf.src";
    const std::string src = "x:Int32 := 10;\n"
                            "[x > 0] --> { a := 1; }, [x < 0] --> { b := -1; }, [...] --> { c := 0; };\n"
                            "z := 0;\n"
                            "[z < 10] <-> { z := z + 1; };\n"
                            "{ z := z - 1; } <-> [z > 0];";

    MapperFile input = m_ctx.source().add_source(name, src, true);
    MapperFile out = m_ctx.source().add_output(name + ".cpp", true);
    ASSERT_FALSE(out.isInvalid());

    PipelineOpts opts;
    opts.no_dsl = true;
    Pipeline pipeline(m_ctx, opts);
    std::vector<CppTranspiler::ExportEntry> exports;
    auto res = pipeline.runPipeline(PipelineSteps::ParseAST | PipelineSteps::Semantic | PipelineSteps::Transpile, input, out, &exports);
    ASSERT_TRUE(res.isValid());

    const std::string cpp = m_ctx.source().output_result(out);
    // if / else-if / else (многострочное форматирование)
    EXPECT_NE(cpp.find("if ((c_x > 0)) {\n    bool c_a = 1;\n} else if ((c_x < 0)) {\n    std::any c_b = -1;\n} else {\n    bool c_c = 0;\n}"),
              std::string::npos)
        << cpp;
    // while
    EXPECT_NE(cpp.find("while ((c_z < 10)) {\n    std::any c_z = (c_z + 1);\n}"), std::string::npos) << cpp;
    // do-while
    EXPECT_NE(cpp.find("} while ((c_z > 0));"), std::string::npos) << cpp;

    auto* reader = m_ctx.source().toReader();
    ASSERT_NE(reader, nullptr);

    // Range mapping: ищем forward-маппинг оператора if — from (исходник) содержит условие
    // "x > 0", to (сгенерированный C++) содержит "if ((".
    const SourceMapReader::RangeMap* ifMap = nullptr;
    for (const auto& [key, rm] : reader->getForwardMappings()) {
        (void)key;
        if (reader->getText(rm.to).find("if ((") != std::string::npos && reader->getText(rm.from).find("x > 0") != std::string::npos) {
            ifMap = &rm;
            break;
        }
    }
    ASSERT_NE(ifMap, nullptr);

    // Range mapping: оператор do-while — from содержит тело "z := z - 1", to содержит "do {".
    const SourceMapReader::RangeMap* doMap = nullptr;
    for (const auto& [key, rm] : reader->getForwardMappings()) {
        (void)key;
        if (reader->getText(rm.to).find("do {") != std::string::npos && reader->getText(rm.from).find("z := z - 1") != std::string::npos) {
            doMap = &rm;
            break;
        }
    }
    ASSERT_NE(doMap, nullptr);
}

/// Test: while-else — кодогенерация C++.
TEST_F(TranspilerTest, GenerateWhileElse_Codegen) {
    const std::string name = "we.src";
    const std::string src = "z := 0;\n"
                            "[z < 10] <-> { z := z + 1; }, [...] --> { z := -1; };";

    MapperFile input = m_ctx.source().add_source(name, src, true);
    MapperFile out = m_ctx.source().add_output(name + ".cpp", true);
    ASSERT_FALSE(out.isInvalid());

    PipelineOpts opts;
    opts.no_dsl = true;
    Pipeline pipeline(m_ctx, opts);
    std::vector<CppTranspiler::ExportEntry> exports;
    auto res = pipeline.runPipeline(PipelineSteps::ParseAST | PipelineSteps::Semantic | PipelineSteps::Transpile, input, out, &exports);
    ASSERT_TRUE(res.isValid());

    const std::string cpp = m_ctx.source().output_result(out);
    // В C++ нет 'while...else' — else эмулируется флагом «вошёл ли цикл хотя бы раз».
    EXPECT_NE(cpp.find("bool _we1 = false;\nwhile ((c_z < 10)) {\n    _we1 = true;\n    std::any c_z = (c_z + 1);\n}\nif (!_we1) {\n    std::any c_z = -1;\n}"),
              std::string::npos)
        << cpp;
}

/// Test: break (++) и continue (-+): безымянные → break;/continue; (без goto).
TEST_F(TranspilerTest, GenerateBreakContinue_Codegen) {
    const std::string name = "bc.src";
    const std::string src = "z := 0;\n"
                            "[z < 10] <-> { ++; -+; };\n"
                            "{ ++; } <-> [z > 0];";

    MapperFile input = m_ctx.source().add_source(name, src, true);
    MapperFile out = m_ctx.source().add_output(name + ".cpp", true);
    ASSERT_FALSE(out.isInvalid());

    PipelineOpts opts;
    opts.no_dsl = true;
    Pipeline pipeline(m_ctx, opts);
    std::vector<CppTranspiler::ExportEntry> exports;
    auto res = pipeline.runPipeline(PipelineSteps::ParseAST | PipelineSteps::Semantic | PipelineSteps::Transpile, input, out, &exports);
    ASSERT_TRUE(res.isValid());

    const std::string cpp = m_ctx.source().output_result(out);
    // while: безымянные break/continue → break;/continue;
    EXPECT_NE(cpp.find("while ((c_z < 10)) {\n    break;\n    continue;\n}"), std::string::npos) << cpp;
    // do-while: безымянный break → break;
    EXPECT_NE(cpp.find("do {\n    break;\n} while ((c_z > 0));"), std::string::npos) << cpp;
}

/// Test: return со значением (++ N ++) и void (++ _ ++) — кодогенерация.
TEST_F(TranspilerTest, GenerateReturn_Codegen) {
    const std::string name = "ret.src";
    const std::string src = "%f():Int32 := { ++ 42 ++; };\n"
                            "%g():Void := { ++ _ ++; };";

    MapperFile input = m_ctx.source().add_source(name, src, true);
    MapperFile out = m_ctx.source().add_output(name + ".cpp", true);
    ASSERT_FALSE(out.isInvalid());

    PipelineOpts opts;
    opts.no_dsl = true;
    Pipeline pipeline(m_ctx, opts);
    std::vector<CppTranspiler::ExportEntry> exports;
    auto res = pipeline.runPipeline(PipelineSteps::ParseAST | PipelineSteps::Semantic | PipelineSteps::Transpile, input, out, &exports);
    ASSERT_TRUE(res.isValid());

    const std::string cpp = m_ctx.source().output_result(out);
    EXPECT_NE(cpp.find("return 42;"), std::string::npos) << cpp;
    EXPECT_NE(cpp.find("return;"), std::string::npos) << cpp;
}

/// Test: match — временная переменная + if/else-if/else.
TEST_F(TranspilerTest, GenerateMatch_Codegen) {
    const std::string name = "match.src";
    const std::string src = "x:Int32 := 5;\n"
                            "[x] ==> { [1] --> { y := 1; }; [2, 3] --> { y := 2; }; [...] --> { y := 0; }; };";

    MapperFile input = m_ctx.source().add_source(name, src, true);
    MapperFile out = m_ctx.source().add_output(name + ".cpp", true);
    ASSERT_FALSE(out.isInvalid());

    PipelineOpts opts;
    opts.no_dsl = true;
    Pipeline pipeline(m_ctx, opts);
    std::vector<CppTranspiler::ExportEntry> exports;
    auto res = pipeline.runPipeline(PipelineSteps::ParseAST | PipelineSteps::Semantic | PipelineSteps::Transpile, input, out, &exports);
    ASSERT_TRUE(res.isValid());

    const std::string cpp = m_ctx.source().output_result(out);
    EXPECT_NE(cpp.find("auto _match1 = c_x;\nif (_match1 == 1) {\n    bool c_y = 1;\n} else if (_match1 == 2 || _match1 == 3) {\n    int8_t c_y = 2;\n} else "
                       "{\n    bool c_y = 0;\n}"),
              std::string::npos)
        << cpp;
}

/// Test: именованные блоки — метки <имя>_continue/_break для именованных break/continue (только внутри функций).
TEST_F(TranspilerTest, GenerateNamedBlockLabels_Codegen) {
    const std::string name = "nb.src";
    const std::string src = "%f():Void := { outer { z:Int32 := 0; [z < 3] <-> { outer ++; outer -+; }; }; };";

    MapperFile input = m_ctx.source().add_source(name, src, true);
    MapperFile out = m_ctx.source().add_output(name + ".cpp", true);
    ASSERT_FALSE(out.isInvalid());

    PipelineOpts opts;
    opts.no_dsl = true;
    Pipeline pipeline(m_ctx, opts);
    std::vector<CppTranspiler::ExportEntry> exports;
    auto res = pipeline.runPipeline(PipelineSteps::ParseAST | PipelineSteps::Semantic | PipelineSteps::Transpile, input, out, &exports);
    ASSERT_TRUE(res.isValid());

    const std::string cpp = m_ctx.source().output_result(out);
    // continue-метка именованного блока стоит ПЕРЕД циклом (после инициализации z := 0),
    // break-метка — после блока; goto именованных break/continue внутри функции.
    // Сам именованный блок (внутри функции) обёрнут в compound statement { }.
    // Метки — отдельные узлы AST (LabelStmt), поэтому выводятся на своих строках.
    EXPECT_NE(cpp.find("void f() {\n    {\n        int32_t c_z = 0;\n        outer_continue:;\n        while ((c_z < 3)) "
                       "{\n            goto outer_break;\n            goto outer_continue;\n        }\n        outer_break:;\n    }\n}"),
              std::string::npos)
        << cpp;
}

/// Test: топ-левел именованный блок метки НЕ выводит (метки C++ недопустимы на namespace-scope).
TEST_F(TranspilerTest, NamedBlock_NoLabelsAtTopLevel) {
    const std::string name = "tl.src";
    const std::string src = "myblock:: { x := 1; };";

    MapperFile input = m_ctx.source().add_source(name, src, true);
    MapperFile out = m_ctx.source().add_output(name + ".cpp", true);
    ASSERT_FALSE(out.isInvalid());

    PipelineOpts opts;
    opts.no_dsl = true;
    Pipeline pipeline(m_ctx, opts);
    std::vector<CppTranspiler::ExportEntry> exports;
    auto res = pipeline.runPipeline(PipelineSteps::ParseAST | PipelineSteps::Semantic | PipelineSteps::Transpile, input, out, &exports);
    ASSERT_TRUE(res.isValid());

    const std::string cpp = m_ctx.source().output_result(out);
    EXPECT_EQ(cpp.find("myblock_break"), std::string::npos) << cpp;
    EXPECT_EQ(cpp.find("myblock_continue"), std::string::npos) << cpp;
}

/// Test: функция — top-level именованный блок. Именованный break на имя функции (func:: ++) = return (void),
/// именованный return (func:: ++ value ++) = return value.
TEST_F(TranspilerTest, GenerateFuncLabelBreak_ReturnsVoid) {
    const std::string name = "fr.src";
    const std::string src = "%f():Int32 := { f:: ++ 42 ++; };\n"
                            "%g():Void := { g:: ++; };";

    MapperFile input = m_ctx.source().add_source(name, src, true);
    MapperFile out = m_ctx.source().add_output(name + ".cpp", true);
    ASSERT_FALSE(out.isInvalid());

    PipelineOpts opts;
    opts.no_dsl = true;
    Pipeline pipeline(m_ctx, opts);
    std::vector<CppTranspiler::ExportEntry> exports;
    auto res = pipeline.runPipeline(PipelineSteps::ParseAST | PipelineSteps::Semantic | PipelineSteps::Transpile, input, out, &exports);
    ASSERT_TRUE(res.isValid());

    const std::string cpp = m_ctx.source().output_result(out);
    EXPECT_NE(cpp.find("int32_t f() {\n    return 42;\n}"), std::string::npos) << cpp;
    EXPECT_NE(cpp.find("void g() {\n    return;\n}"), std::string::npos) << cpp;
}

/// Test: алиас-цепочка и переменные с пользовательскими алиасами.
/// MyInt ::= :Int32; x:MyInt := 10; Big ::= MyInt; y:Big := 20;
/// Ожидается: using c_MyInt = int32_t; c_MyInt c_x = 10; using c_Big = c_MyInt; c_Big c_y = 20;
TEST_F(TranspilerTest, GenerateToFileAliasChainAndVar) {
    const std::string name = "alias.src";
    const std::string src = "MyInt ::= :Int32;\n"
                            "x:MyInt := 10;\n"
                            "Big ::= MyInt;\n"
                            "y:Big := 20;\n";

    MapperFile input = m_ctx.source().add_source(name, src, true);
    MapperFile out = m_ctx.source().add_output(name + ".cpp", true);
    ASSERT_FALSE(out.isInvalid());

    PipelineOpts opts;
    opts.no_dsl = true;
    Pipeline pipeline(m_ctx, opts);
    std::vector<CppTranspiler::ExportEntry> exports;
    auto res = pipeline.runPipeline(PipelineSteps::ParseAST | PipelineSteps::Semantic | PipelineSteps::Transpile, input, out, &exports);
    ASSERT_TRUE(res.isValid());
    EXPECT_EQ(m_ctx.diag().errorCount(), 0);

    const std::string cpp = m_ctx.source().output_result(out);
    EXPECT_NE(cpp.find("using c_MyInt = int32_t;"), std::string::npos) << cpp;
    EXPECT_NE(cpp.find("c_MyInt c_x = 10;"), std::string::npos) << cpp;
    EXPECT_NE(cpp.find("using c_Big = c_MyInt;"), std::string::npos) << cpp;
    EXPECT_NE(cpp.find("c_Big c_y = 20;"), std::string::npos) << cpp;
}

/// Test: тип без C++-имени (Void) — транспайлер сообщает об ошибке, запрещён fallback "auto".
TEST_F(TranspilerTest, GenerateToFileUnknownTypeReportsError) {
    const std::string name = "errtype.src";
    const std::string src = "v:Void := 5;\n";

    MapperFile input = m_ctx.source().add_source(name, src, true);
    MapperFile out = m_ctx.source().add_output(name + ".cpp", true);
    ASSERT_FALSE(out.isInvalid());

    PipelineOpts opts;
    opts.no_dsl = true;
    Pipeline pipeline(m_ctx, opts);
    std::vector<CppTranspiler::ExportEntry> exports;
    auto res = pipeline.runPipeline(PipelineSteps::ParseAST | PipelineSteps::Semantic | PipelineSteps::Transpile, input, out, &exports);

    // Явная ошибка из транспайлера (Void не имеет C++-имени), а не молчаливый fallback.
    EXPECT_GT(m_ctx.diag().errorCount(), 0);
    const std::string cpp = m_ctx.source().output_result(out);
    EXPECT_EQ(cpp.find("auto v"), std::string::npos) << cpp;
}
/// Test: область имён на верхнем уровне эмитится как `namespace ns { ... }` (область видимости
/// сохраняется — переменная не «протекает» в глобальный скоуп), имя внутри маппится.
TEST_F(TranspilerTest, GenerateTopLevelNamespaceScope) {
    const std::string name = "tlblock.src";
    const std::string src = "ns:: { y:Int32 := 20; };";
    MapperFile input = m_ctx.source().add_source(name, src, true);
    MapperFile out = m_ctx.source().add_output(name + ".cpp", true);
    ASSERT_FALSE(out.isInvalid());

    PipelineOpts opts;
    opts.no_dsl = true;
    Pipeline pipeline(m_ctx, opts);
    std::vector<CppTranspiler::ExportEntry> exports;
    auto res = pipeline.runPipeline(PipelineSteps::ParseAST | PipelineSteps::Semantic | PipelineSteps::Transpile, input, out, &exports);
    ASSERT_TRUE(res.isValid());
    EXPECT_EQ(m_ctx.diag().errorCount(), 0);

    const std::string cpp = m_ctx.source().output_result(out);
    EXPECT_NE(cpp.find("namespace c_ns {\n    int32_t c_y = 20;\n}"), std::string::npos) << cpp;

    // Имя переменной внутри области имён должно маппиться (addNameMapping) и указывать на 'y'.
    auto* reader = m_ctx.source().toReader();
    ASSERT_NE(reader, nullptr);
    // 'y' в "ns:: { y:Int32 := 20; }" — 1-based позиция 8.
    auto cppName = reader->getCppName(static_cast<ReaderLocation>(m_ctx.source().makeLoc(input, 8)), "y");
    ASSERT_TRUE(cppName.has_value());
    EXPECT_EQ(cppName->toName, "c_y");
    EXPECT_EQ(reader->getText(cppName->rangeMap.to), "c_y");
}

/// Test: вложенные области имён верхнего уровня → вложенные namespace.
TEST_F(TranspilerTest, GenerateNestedTopLevelNamespaces) {
    const std::string name = "nested.src";
    const std::string src = "ns:: { a:Int32 := 2; sub:: { c:Int32 := 3; }; };";
    MapperFile input = m_ctx.source().add_source(name, src, true);
    MapperFile out = m_ctx.source().add_output(name + ".cpp", true);
    ASSERT_FALSE(out.isInvalid());

    PipelineOpts opts;
    opts.no_dsl = true;
    Pipeline pipeline(m_ctx, opts);
    std::vector<CppTranspiler::ExportEntry> exports;
    auto res = pipeline.runPipeline(PipelineSteps::ParseAST | PipelineSteps::Semantic | PipelineSteps::Transpile, input, out, &exports);
    ASSERT_TRUE(res.isValid());

    const std::string cpp = m_ctx.source().output_result(out);
    EXPECT_NE(cpp.find("namespace c_ns {\n    int32_t c_a = 2;\n    namespace c_sub {\n        int32_t c_c = 3;\n    }\n}"), std::string::npos) << cpp;
}

/// Test: пользовательский безымянный блок внутри функции эмитится как compound statement { }.
TEST_F(TranspilerTest, GenerateBlockInFunction_Compound) {
    const std::string name = "fnblock.src";
    const std::string src = "%f():Void := { a:Int32 := 1; { b:Int32 := 2; }; c:Int32 := 3; };";
    MapperFile input = m_ctx.source().add_source(name, src, true);
    MapperFile out = m_ctx.source().add_output(name + ".cpp", true);
    ASSERT_FALSE(out.isInvalid());

    PipelineOpts opts;
    opts.no_dsl = true;
    Pipeline pipeline(m_ctx, opts);
    std::vector<CppTranspiler::ExportEntry> exports;
    auto res = pipeline.runPipeline(PipelineSteps::ParseAST | PipelineSteps::Semantic | PipelineSteps::Transpile, input, out, &exports);
    ASSERT_TRUE(res.isValid());
    EXPECT_EQ(m_ctx.diag().errorCount(), 0);

    const std::string cpp = m_ctx.source().output_result(out);
    EXPECT_NE(cpp.find("void f() {\n    int32_t c_a = 1;\n    {\n        int32_t c_b = 2;\n    }\n    int32_t c_c = 3;\n}"), std::string::npos) << cpp;
}

/// Test: объявление типа в области имён — тип маппится и не «протекает».
TEST_F(TranspilerTest, GenerateNamespaceTypeDecl_NameMapping) {
    const std::string name = "blktype.src";
    const std::string src = "ns:: { MyInt ::= :Int32; y:MyInt := 20; };";
    MapperFile input = m_ctx.source().add_source(name, src, true);
    MapperFile out = m_ctx.source().add_output(name + ".cpp", true);
    ASSERT_FALSE(out.isInvalid());

    PipelineOpts opts;
    opts.no_dsl = true;
    Pipeline pipeline(m_ctx, opts);
    std::vector<CppTranspiler::ExportEntry> exports;
    auto res = pipeline.runPipeline(PipelineSteps::ParseAST | PipelineSteps::Semantic | PipelineSteps::Transpile, input, out, &exports);
    ASSERT_TRUE(res.isValid());
    EXPECT_EQ(m_ctx.diag().errorCount(), 0);

    const std::string cpp = m_ctx.source().output_result(out);
    // Объявление типа и переменная — внутри namespace ns (не в глобальном скоупе).
    EXPECT_NE(cpp.find("namespace c_ns {\n    using c_MyInt = int32_t;\n    c_MyInt c_y = 20;\n}"), std::string::npos) << cpp;

    auto* reader = m_ctx.source().toReader();
    ASSERT_NE(reader, nullptr);
    // Имя типа MyInt в области имён должно маппиться; 'M' в "ns:: { MyInt ::= ..." — 1-based позиция 8.
    auto cppName = reader->getCppName(static_cast<ReaderLocation>(m_ctx.source().makeLoc(input, 8)), "MyInt");
    ASSERT_TRUE(cppName.has_value());
    EXPECT_EQ(cppName->toName, "c_MyInt");
    EXPECT_EQ(reader->getText(cppName->rangeMap.to), "c_MyInt");
}

/// Test: attr::ReadOnly на переменной → префикс `const` в C++.
TEST_F(TranspilerTest, GenerateVarDeclReadOnlyConst) {
    MapperFile input_file = m_ctx.source().add_source("test.src", "x:Int32 := 42;", true);
    MapperRange range(m_ctx.source().makeLoc(input_file, 1), m_ctx.source().makeLoc(input_file, 14));

    auto term = Term::Create(TermID::NAME, "x", range, parser::token_type::NAME);
    auto var = std::make_shared<VarDecl>(std::move(term), std::make_shared<IdentType>("Int32"), std::make_shared<Literal>(ParserToken::Kind::IntLiteral, "42"));
    var->add_attr(m_ctx.attrs().lookup(attr::ReadOnly).value());
    std::vector<AstNodePtr> seq;
    seq.push_back(std::move(var));

    SemanticPassRunner runner(m_ctx);
    ASSERT_TRUE(runner.run(seq));

    MapperFile out_idx = m_ctx.source().add_output("test_output.cpp", true);
    ASSERT_FALSE(out_idx.isInvalid());
    CppTranspiler gen(m_ctx);
    gen.generateToFile(seq, out_idx);

    std::string result = m_ctx.source().output_result(out_idx);
    EXPECT_TRUE(result.find("const int32_t c_x = 42;") != std::string::npos) << result;
}

/// Test: attr::ThreadLocal на переменной → префикс `thread_local` в C++.
TEST_F(TranspilerTest, GenerateVarDeclThreadLocal) {
    MapperFile input_file = m_ctx.source().add_source("test.src", "x:Int32 := 42;", true);
    MapperRange range(m_ctx.source().makeLoc(input_file, 1), m_ctx.source().makeLoc(input_file, 14));

    auto term = Term::Create(TermID::NAME, "x", range, parser::token_type::NAME);
    auto var = std::make_shared<VarDecl>(std::move(term), std::make_shared<IdentType>("Int32"), std::make_shared<Literal>(ParserToken::Kind::IntLiteral, "42"));
    var->add_attr(m_ctx.attrs().lookup(attr::ThreadLocal).value());
    std::vector<AstNodePtr> seq;
    seq.push_back(std::move(var));

    SemanticPassRunner runner(m_ctx);
    ASSERT_TRUE(runner.run(seq));

    MapperFile out_idx = m_ctx.source().add_output("test_output.cpp", true);
    ASSERT_FALSE(out_idx.isInvalid());
    CppTranspiler gen(m_ctx);
    gen.generateToFile(seq, out_idx);

    std::string result = m_ctx.source().output_result(out_idx);
    EXPECT_TRUE(result.find("thread_local int32_t c_x = 42;") != std::string::npos) << result;
}

/// Test: attr::ReadOnly + attr::ThreadLocal на переменной → `const thread_local` в C++.
TEST_F(TranspilerTest, GenerateVarDeclReadOnlyThreadLocal) {
    MapperFile input_file = m_ctx.source().add_source("test.src", "x:Int32 := 42;", true);
    MapperRange range(m_ctx.source().makeLoc(input_file, 1), m_ctx.source().makeLoc(input_file, 14));

    auto term = Term::Create(TermID::NAME, "x", range, parser::token_type::NAME);
    auto var = std::make_shared<VarDecl>(std::move(term), std::make_shared<IdentType>("Int32"), std::make_shared<Literal>(ParserToken::Kind::IntLiteral, "42"));
    var->add_attr(m_ctx.attrs().lookup(attr::ReadOnly).value());
    var->add_attr(m_ctx.attrs().lookup(attr::ThreadLocal).value());
    std::vector<AstNodePtr> seq;
    seq.push_back(std::move(var));

    SemanticPassRunner runner(m_ctx);
    ASSERT_TRUE(runner.run(seq));

    MapperFile out_idx = m_ctx.source().add_output("test_output.cpp", true);
    ASSERT_FALSE(out_idx.isInvalid());
    CppTranspiler gen(m_ctx);
    gen.generateToFile(seq, out_idx);

    std::string result = m_ctx.source().output_result(out_idx);
    EXPECT_TRUE(result.find("const thread_local int32_t c_x = 42;") != std::string::npos) << result;
}

} // namespace

/// Test: атрибуты функции → лидирующие/завершающие квалификаторы C++.
/// FuncConst -> __attribute__((const)), FuncPure -> __attribute__((pure)),
/// FuncConstexpr -> constexpr (лидирующие), NoExcept -> noexcept (завершающий).
TEST_F(TranspilerTest, GenerateFuncDeclAttributeQualifiers) {
    MapperFile input_file = m_ctx.source().add_source("test.src", "%f():Void := { };", true);

    // Каждая функция — синтетическая, но с УНИКАЛЬНЫМ source-range (иначе коллизия mapStop).
    auto makeFunc = [&](std::string name, MapperRange r, std::initializer_list<AttrId> attrs) {
        auto term = Term::Create(TermID::NAME, name, r, parser::token_type::NAME);
        auto func = std::make_shared<FuncDecl>(std::move(term));
        func->m_params = std::vector<AstNodePtr>{};
        func->m_type = std::make_shared<IdentType>("None");
        func->m_body = std::vector<AstNodePtr>{};
        for (AttrId a : attrs) {
            func->add_attr(a);
        }
        return func;
    };

    auto lookup = [&](std::string_view name) -> AttrId {
        auto id = m_ctx.attrs().lookup(name);
        EXPECT_TRUE(id.has_value()) << "attr '" << name << "' must be registered";
        return id.value();
    };

    // Уникальные диапазоны для каждой функции.
    auto rangeAt = [&](int begin, int end) { return MapperRange(m_ctx.source().makeLoc(input_file, begin), m_ctx.source().makeLoc(input_file, end)); };

    std::vector<AstNodePtr> seq;
    seq.push_back(makeFunc("%fc", rangeAt(1, 5), {lookup(attr::FuncConst)}));
    seq.push_back(makeFunc("%fp", rangeAt(6, 10), {lookup(attr::FuncPure)}));
    seq.push_back(makeFunc("%fx", rangeAt(11, 15), {lookup(attr::FuncConstexpr)}));
    seq.push_back(makeFunc("%fn", rangeAt(16, 20), {lookup(attr::NoExcept)}));
    seq.push_back(makeFunc("%fcpn", rangeAt(21, 25), {lookup(attr::FuncConst), lookup(attr::FuncPure), lookup(attr::NoExcept)}));

    SemanticPassRunner runner(m_ctx);
    ASSERT_TRUE(runner.run(seq));

    MapperFile out_idx = m_ctx.source().add_output("test_func.cpp", true);
    ASSERT_FALSE(out_idx.isInvalid());
    CppTranspiler gen(m_ctx);
    gen.generateToFile(seq, out_idx);

    std::string result = m_ctx.source().output_result(out_idx);
    EXPECT_TRUE(result.find("__attribute__((const)) void fc()") != std::string::npos) << result;
    EXPECT_TRUE(result.find("__attribute__((pure)) void fp()") != std::string::npos) << result;
    EXPECT_TRUE(result.find("constexpr void fx()") != std::string::npos) << result;
    EXPECT_TRUE(result.find("void fn() noexcept") != std::string::npos) << result;
    // Комбинация лидирующих + завершающего квалификатора.
    EXPECT_TRUE(result.find("__attribute__((const)) __attribute__((pure)) void fcpn() noexcept") != std::string::npos) << result;
}

/// Test: нативная функция с `@[link("имя")]` → флаг линковки `-l<имя>` в linkLibs().
TEST_F(TranspilerTest, GenerateNativeFuncCollectsLinkLib) {
    MapperFile input_file = m_ctx.source().add_source("test.src", "%sqrt(x:Float64):Float64 := ;", true);
    MapperRange range(m_ctx.source().makeLoc(input_file, 1), m_ctx.source().makeLoc(input_file, 20));

    auto term = Term::Create(TermID::NAME, "%sqrt", range, parser::token_type::NAME);
    auto func = std::make_shared<FuncDecl>(std::move(term));
    func->m_params = std::vector<AstNodePtr>{};
    func->m_type = std::make_shared<IdentType>("Float64");
    // Без тела → forward-декларация нативной функции.

    auto link_id = m_ctx.attrs().lookup(attr::Link);
    ASSERT_TRUE(link_id.has_value());
    func->add_attr(*link_id);
    func->set_attr_args(*link_id, {"m"});

    std::vector<AstNodePtr> seq;
    seq.push_back(std::move(func));

    SemanticPassRunner runner(m_ctx);
    ASSERT_TRUE(runner.run(seq));

    MapperFile out_idx = m_ctx.source().add_output("test_link.cpp", true);
    ASSERT_FALSE(out_idx.isInvalid());
    CppTranspiler gen(m_ctx);
    gen.generateToFile(seq, out_idx);

    const auto& libs = gen.linkLibs();
    EXPECT_NE(libs.find("-lm"), libs.end()) << "expected -lm in linkLibs";

    // Имя нативной функции манглируется: %sqrt → sqrt (без префикса c_).
    // Forward-декларация без '::' линкуется как C-символ → префикс extern "C".
    std::string result = m_ctx.source().output_result(out_idx);
    EXPECT_TRUE(result.find("sqrt(") != std::string::npos) << result;
    EXPECT_TRUE(result.find("extern \"C\" double sqrt()") != std::string::npos) << result;
}

/// Test: импорт нативной функции `fabs(x:Int32):Int32 := %abs...;` — АЛИАС:
/// C++-функция `fabs` НЕ эмитится (в отличие от forward-декларации/определения).
TEST_F(TranspilerTest, GenerateNativeImportAlias) {
    MapperFile input_file = m_ctx.source().add_source("test.src", "fabs(x:Int32):Int32 := %abs...;", true);
    MapperRange range(m_ctx.source().makeLoc(input_file, 1), m_ctx.source().makeLoc(input_file, 20));

    auto term = Term::Create(TermID::NAME, "fabs", range, parser::token_type::NAME);
    auto func = std::make_shared<FuncDecl>(std::move(term));
    func->m_params = std::vector<AstNodePtr>{};
    func->m_type = std::make_shared<IdentType>("Int32");
    func->m_isNativeImport = true;
    func->m_nativeName = "abs";

    std::vector<AstNodePtr> seq;
    seq.push_back(std::move(func));

    SemanticPassRunner runner(m_ctx);
    ASSERT_TRUE(runner.run(seq));

    MapperFile out_idx = m_ctx.source().add_output("test_import.cpp", true);
    ASSERT_FALSE(out_idx.isInvalid());
    CppTranspiler gen(m_ctx);
    gen.generateToFile(seq, out_idx);

    std::string result = m_ctx.source().output_result(out_idx);
    // Импортируемая функция не эмитится как C++-функция (алиас на нативную abs).
    EXPECT_EQ(result.find("c_fabs("), std::string::npos) << result;
    EXPECT_EQ(result.find("c_fabs"), std::string::npos) << result;
}

/// Test: правило линковки нативной декларации — без '::' линкуется как C-символ (extern "C",
/// напр. libc/libm `sqrt`), с '::' — C++-линковка (extern "C" НЕ добавляется, `std::sqrt`).
TEST_F(TranspilerTest, GenerateNativeDeclLinkage) {
    auto run = [&](const char* nativeName, const char* outName) -> std::string {
        MapperFile input_file = m_ctx.source().add_source(outName, "%native(x:Float64):Float64 := ...;", true);
        MapperRange range(m_ctx.source().makeLoc(input_file, 1), m_ctx.source().makeLoc(input_file, 20));
        auto term = Term::Create(TermID::NAME, nativeName, range, parser::token_type::NAME);
        auto func = std::make_shared<FuncDecl>(std::move(term));
        func->m_params = std::vector<AstNodePtr>{};
        func->m_type = std::make_shared<IdentType>("Float64");
        std::vector<AstNodePtr> seq;
        seq.push_back(std::move(func));
        SemanticPassRunner runner(m_ctx);
        if (!runner.run(seq)) {
            return {};
        }
        MapperFile out_idx = m_ctx.source().add_output(std::string(outName) + ".cpp", true);
        CppTranspiler gen(m_ctx);
        gen.generateToFile(seq, out_idx);
        return m_ctx.source().output_result(out_idx);
    };

    // Без '::' (%sqrt) → extern "C" (C-символ).
    {
        const std::string r = run("%sqrt", "native_c");
        EXPECT_NE(r.find("extern \"C\" double sqrt()"), std::string::npos) << r;
    }
    // С '::' (%std::sqrt) → C++-линковка: extern "C" не добавляется.
    {
        const std::string r = run("%std::sqrt", "native_cpp");
        EXPECT_EQ(r.find("extern \"C\""), std::string::npos) << r;
        EXPECT_NE(r.find("std::sqrt()"), std::string::npos) << r;
    }
}

} // namespace trust