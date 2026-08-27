#include "transpiler/transpiler_test_fixture.hpp"

namespace trust {
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

    // Литерал словаря: контракт - все элементы ArgNode (имя в text(), значение в m_value).
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

/// Test: составной литерал (ArrayInit) как значение члена Variant - анализ/регистрация работают,
/// кодогенерация выдаёт «не реализовано» и НЕ эмитит сломанный C++ (memberValueCpp - скалярные).
TEST_F(TranspilerTest, VariantCompositeMemberValueNotImplemented) {
    auto dict = std::make_shared<DictLiteralNode>(ParserToken::Kind::DictLiteral, std::string(""));
    dict->m_type = std::make_shared<IdentType>(std::string("Variant"));
    dict->m_body.push_back(
        std::make_shared<ArgNode>(std::string("a"), nullptr, std::make_shared<DictLiteralNode>(ParserToken::Kind::ArrayInit, std::string(""))));
    auto variantDecl = std::make_shared<Binary>(ParserToken::Kind::TypeDecl, std::make_shared<IdentName>(std::string("Value")), std::move(dict));
    std::vector<AstNodePtr> seq;
    seq.push_back(std::move(variantDecl));

    SemanticPassRunner runner(m_ctx);
    ASSERT_TRUE(runner.run(seq)); // анализ не блокирует - ошибка на кодогенерации

    MapperFile out_idx = m_ctx.source().add_output("value_out.cpp", true);
    ASSERT_FALSE(out_idx.isInvalid());

    CppTranspiler gen(m_ctx);
    gen.generateToFile(seq, out_idx);

    std::string result = m_ctx.source().output_result(out_idx);
    EXPECT_GT(m_ctx.diag().errorCount(), 0);
    EXPECT_EQ(result.find("std::variant"), std::string::npos); // сломанный код не эмитится
}

/// Test: составной литерал (ArrayInit) как значение члена Enum - кодогенерация «не реализовано».
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

    // $r.size^() - const-вызов: attr::ReadOnly на ВЫЗОВЕ (CallExpr; в реальном потоке ставит
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

/// Test: generateToFile для Document-узла - комментарий эмитится сырым текстом с маркерами.
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
    m_ctx.opts().set_enabled(transpiler::FlagKind::Comments, false);

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

/// Test: Trust-доки `##`/`##<` невалидны в C++ - в выводе нормализуются в `///`/`///<`.
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

} // namespace trust
