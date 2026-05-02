import trust;

#include "ast_format_parser.hpp"

#include <gtest/gtest.h>
#include <sstream>
#include <string>

using namespace trust;

// ============================================================================
// AST Parsing Tests
// ============================================================================

class AstParsingTest : public ::testing::Test {
protected:
    Context ctx;
};

// Test: Parse a simple VarDecl node
TEST_F(AstParsingTest, ParseVarDecl) {
    std::string input = "VarDecl name=x type=int\n"
                        "  IntLiteral value=42\n";
    auto result = parse_ast_format(input, ctx);
    ASSERT_TRUE(result.has_value());
    ASSERT_EQ(result->size(), 1);
    EXPECT_EQ((*result)[0]->kind, ParserToken::Kind::VarDecl);
    EXPECT_EQ((*result)[0]->children.size(), 1);
    EXPECT_EQ((*result)[0]->children[0]->kind, ParserToken::Kind::IntLiteral);
}

// Test: Parse a simple FuncDecl node
TEST_F(AstParsingTest, ParseFuncDecl) {
    std::string input = "FuncDecl name=main ret=int\n"
                        "  BlockStmt\n"
                        "    ReturnStmt\n"
                        "      IntLiteral value=0\n";
    auto result = parse_ast_format(input, ctx);
    ASSERT_TRUE(result.has_value());
    ASSERT_EQ(result->size(), 1);
    EXPECT_EQ((*result)[0]->kind, ParserToken::Kind::FuncDecl);
    EXPECT_EQ((*result)[0]->children.size(), 1);
    EXPECT_EQ((*result)[0]->children[0]->kind, ParserToken::Kind::BlockStmt);
}

// Test: Parse FuncDecl with parameters
TEST_F(AstParsingTest, ParseFuncDeclWithParams) {
    std::string input = "FuncDecl name=add ret=int\n"
                        "  ParamDecl name=a type=int\n"
                        "  ParamDecl name=b type=int\n"
                        "  BlockStmt\n"
                        "    ReturnStmt\n"
                        "      IntLiteral value=0\n";
    auto result = parse_ast_format(input, ctx);
    ASSERT_TRUE(result.has_value());
    ASSERT_EQ(result->size(), 1);
    EXPECT_EQ((*result)[0]->kind, ParserToken::Kind::FuncDecl);
    size_t param_count = 0;
    for (auto& child : (*result)[0]->children) {
        if (child->kind == ParserToken::Kind::ParamDecl) param_count++;
    }
    EXPECT_EQ(param_count, 2);
}

// Test: Parse multiple root nodes
TEST_F(AstParsingTest, ParseMultipleRoots) {
    std::string input = "VarDecl name=x type=int\n"
                        "  IntLiteral value=1\n"
                        "FuncDecl name=main ret=int\n"
                        "  BlockStmt\n";
    auto result = parse_ast_format(input, ctx);
    ASSERT_TRUE(result.has_value());
    ASSERT_EQ(result->size(), 2);
    EXPECT_EQ((*result)[0]->kind, ParserToken::Kind::VarDecl);
    EXPECT_EQ((*result)[1]->kind, ParserToken::Kind::FuncDecl);
}

// Test: Parse BinaryOp expression
TEST_F(AstParsingTest, ParseBinaryOp) {
    std::string input = "BinaryOp op=+ type=int\n"
                        "  VarRef name=x\n"
                        "  IntLiteral value=10\n";
    auto result = parse_ast_format(input, ctx);
    ASSERT_TRUE(result.has_value());
    ASSERT_EQ(result->size(), 1);
    EXPECT_EQ((*result)[0]->kind, ParserToken::Kind::BinaryOp);
    EXPECT_EQ((*result)[0]->children.size(), 2);
    EXPECT_EQ((*result)[0]->children[0]->kind, ParserToken::Kind::VarRef);
    EXPECT_EQ((*result)[0]->children[1]->kind, ParserToken::Kind::IntLiteral);
}

// Test: Parse IfStmt with condition
TEST_F(AstParsingTest, ParseIfStmt) {
    std::string input = "IfStmt\n"
                        "  VarRef name=x\n"
                        "  ThenBlock\n"
                        "    ReturnStmt\n"
                        "      IntLiteral value=1\n";
    auto result = parse_ast_format(input, ctx);
    ASSERT_TRUE(result.has_value());
    ASSERT_EQ(result->size(), 1);
    EXPECT_EQ((*result)[0]->kind, ParserToken::Kind::IfStmt);
}

// Test: Parse IfStmt with else block
TEST_F(AstParsingTest, ParseIfStmtWithElse) {
    std::string input = "IfStmt\n"
                        "  VarRef name=x\n"
                        "  ThenBlock\n"
                        "    ReturnStmt\n"
                        "      IntLiteral value=1\n"
                        "  ElseBlock\n"
                        "    ReturnStmt\n"
                        "      IntLiteral value=0\n";
    auto result = parse_ast_format(input, ctx);
    ASSERT_TRUE(result.has_value());
    ASSERT_EQ(result->size(), 1);
    EXPECT_EQ((*result)[0]->kind, ParserToken::Kind::IfStmt);
    EXPECT_GE((*result)[0]->children.size(), 3);
}

// Test: Parse StringLiteral
TEST_F(AstParsingTest, ParseStringLiteral) {
    std::string input = "StringLiteral value=\"hello\"\n";
    auto result = parse_ast_format(input, ctx);
    ASSERT_TRUE(result.has_value());
    ASSERT_EQ(result->size(), 1);
    EXPECT_EQ((*result)[0]->kind, ParserToken::Kind::StringLiteral);

    bool found = false;
    for (auto& attr : (*result)[0]->attrs) {
        if (attr.first == "value") {
            EXPECT_EQ(attr.second, "\"hello\"");
            found = true;
        }
    }
    EXPECT_TRUE(found);
}

// Test: Parse CallExpr
TEST_F(AstParsingTest, ParseCallExpr) {
    std::string input = "CallExpr name=print\n"
                        "  StringLiteral value=\"hello\"\n";
    auto result = parse_ast_format(input, ctx);
    ASSERT_TRUE(result.has_value());
    ASSERT_EQ(result->size(), 1);
    EXPECT_EQ((*result)[0]->kind, ParserToken::Kind::CallExpr);
    EXPECT_EQ((*result)[0]->children.size(), 1);
}

// Test: Parse AssignmentStmt
TEST_F(AstParsingTest, ParseAssignmentStmt) {
    std::string input = "AssignmentStmt target=x\n"
                        "  IntLiteral value=42\n";
    auto result = parse_ast_format(input, ctx);
    ASSERT_TRUE(result.has_value());
    ASSERT_EQ(result->size(), 1);
    EXPECT_EQ((*result)[0]->kind, ParserToken::Kind::AssignmentStmt);
}

// Test: Parse ExprStmt
TEST_F(AstParsingTest, ParseExprStmt) {
    std::string input = "ExprStmt\n"
                        "  IntLiteral value=42\n";
    auto result = parse_ast_format(input, ctx);
    ASSERT_TRUE(result.has_value());
    ASSERT_EQ(result->size(), 1);
    EXPECT_EQ((*result)[0]->kind, ParserToken::Kind::ExprStmt);
}

// Test: Parse BlockStmt with multiple statements
TEST_F(AstParsingTest, ParseBlockStmt) {
    std::string input = "BlockStmt\n"
                        "  VarDecl name=x type=int\n"
                        "    IntLiteral value=1\n"
                        "  VarDecl name=y type=int\n"
                        "    IntLiteral value=2\n";
    auto result = parse_ast_format(input, ctx);
    ASSERT_TRUE(result.has_value());
    ASSERT_EQ(result->size(), 1);
    EXPECT_EQ((*result)[0]->kind, ParserToken::Kind::BlockStmt);
    EXPECT_EQ((*result)[0]->children.size(), 2);
}

// Test: Parse with various binary operators
TEST_F(AstParsingTest, ParseAllBinaryOperators) {
    const char* ops[] = {"+", "-", "*", "/", "==", "!=", "<", "<=", ">", ">=", "and", "or"};

    for (const char* op : ops) {
        std::string input = "BinaryOp op=" + std::string(op) + " type=int\n"
                            "  IntLiteral value=1\n"
                            "  IntLiteral value=2\n";
        auto result = parse_ast_format(input, ctx);
        ASSERT_TRUE(result.has_value());
        ASSERT_EQ(result->size(), 1);
        EXPECT_EQ((*result)[0]->kind, ParserToken::Kind::BinaryOp);
    }
}

// Test: Parse with various types
TEST_F(AstParsingTest, ParseAllTypes) {
    const char* types[] = {"int", "string", "void", "bool"};

    for (const char* t : types) {
        std::string input = "VarDecl name=x type=" + std::string(t) + "\n"
                            "  IntLiteral value=0\n";
        auto result = parse_ast_format(input, ctx);
        ASSERT_TRUE(result.has_value());
        ASSERT_EQ(result->size(), 1);
        EXPECT_EQ((*result)[0]->kind, ParserToken::Kind::VarDecl);
    }
}

// Test: Parse complex nested structure (full function)
TEST_F(AstParsingTest, ParseComplexFunction) {
    std::string input = "FuncDecl name=main ret=int\n"
                        "  BlockStmt\n"
                        "    VarDecl name=x type=int\n"
                        "      IntLiteral value=10\n"
                        "    VarDecl name=y type=int\n"
                        "      IntLiteral value=20\n"
                        "    IfStmt\n"
                        "      BinaryOp op=> type=bool\n"
                        "        VarRef name=x\n"
                        "        VarRef name=y\n"
                        "      ThenBlock\n"
                        "        ReturnStmt\n"
                        "          VarRef name=x\n"
                        "      ElseBlock\n"
                        "        ReturnStmt\n"
                        "          VarRef name=y\n";
    auto result = parse_ast_format(input, ctx);
    ASSERT_TRUE(result.has_value());
    ASSERT_EQ(result->size(), 1);
    EXPECT_EQ((*result)[0]->kind, ParserToken::Kind::FuncDecl);
}

// Test: Parse empty input
TEST_F(AstParsingTest, ParseEmptyInput) {
    std::string input = "";
    auto result = parse_ast_format(input, ctx);
    ASSERT_TRUE(result.has_value());
    EXPECT_EQ(result->size(), 0);
}

// Test: Parse input with only whitespace
TEST_F(AstParsingTest, ParseWhitespaceOnly) {
    std::string input = "   \n   \n\n";
    auto result = parse_ast_format(input, ctx);
    ASSERT_TRUE(result.has_value());
    EXPECT_EQ(result->size(), 0);
}

// Test: Parse with comments (# lines)
TEST_F(AstParsingTest, ParseWithComments) {
    std::string input = "# This is a comment\n"
                        "VarDecl name=x type=int\n"
                        "# Another comment\n"
                        "  IntLiteral value=42\n";
    auto result = parse_ast_format(input, ctx);
    ASSERT_TRUE(result.has_value());
    ASSERT_EQ(result->size(), 1);
    EXPECT_EQ((*result)[0]->kind, ParserToken::Kind::VarDecl);
}

// Test: Parse deeply nested structure
TEST_F(AstParsingTest, ParseDeepNesting) {
    std::string input = "FuncDecl name=f ret=int\n"
                        "  BlockStmt\n"
                        "    IfStmt\n"
                        "      VarRef name=x\n"
                        "      ThenBlock\n"
                        "        IfStmt\n"
                        "          VarRef name=y\n"
                        "          ThenBlock\n"
                        "            ReturnStmt\n"
                        "              IntLiteral value=1\n";
    auto result = parse_ast_format(input, ctx);
    ASSERT_TRUE(result.has_value());
    ASSERT_EQ(result->size(), 1);
    EXPECT_EQ((*result)[0]->kind, ParserToken::Kind::FuncDecl);
}

// Test: Parse EnumDecl
TEST_F(AstParsingTest, ParseEnumDecl) {
    std::string input = "EnumDecl name=Color\n"
                        "  EnumMember name=Red value=1\n"
                        "  EnumMember name=Green\n"
                        "  EnumMember name=Blue value=3\n";
    auto result = parse_ast_format(input, ctx);
    ASSERT_TRUE(result.has_value());
    ASSERT_EQ(result->size(), 1);
    EXPECT_EQ((*result)[0]->kind, ParserToken::Kind::EnumDecl);
    EXPECT_EQ((*result)[0]->children.size(), 3);
}

// Test: Parse StructDecl
TEST_F(AstParsingTest, ParseStructDecl) {
    std::string input = "StructDecl name=Point\n"
                        "  StructField name=x type=int\n"
                        "  StructField name=y type=int\n";
    auto result = parse_ast_format(input, ctx);
    ASSERT_TRUE(result.has_value());
    ASSERT_EQ(result->size(), 1);
    EXPECT_EQ((*result)[0]->kind, ParserToken::Kind::StructDecl);
    EXPECT_EQ((*result)[0]->children.size(), 2);
}

// Test: Parse WhileStmt
TEST_F(AstParsingTest, ParseWhileStmt) {
    std::string input = "FuncDecl name=main ret=int\n"
                        "  BlockStmt\n"
                        "    WhileStmt\n"
                        "      VarRef name=x\n"
                        "      ReturnStmt\n"
                        "        IntLiteral value=0\n";
    auto result = parse_ast_format(input, ctx);
    ASSERT_TRUE(result.has_value());
    ASSERT_EQ(result->size(), 1);
    auto* func_node = (*result)[0].get();
    auto* block_node = func_node->children[0].get();
    EXPECT_EQ(block_node->kind, ParserToken::Kind::BlockStmt);
}

// Test: Parse DoWhileStmt
TEST_F(AstParsingTest, ParseDoWhileStmt) {
    std::string input = "FuncDecl name=main ret=int\n"
                        "  BlockStmt\n"
                        "    DoWhileStmt\n"
                        "      ReturnStmt\n"
                        "        IntLiteral value=0\n"
                        "      VarRef name=x\n";
    auto result = parse_ast_format(input, ctx);
    ASSERT_TRUE(result.has_value());
    ASSERT_EQ(result->size(), 1);
}

// Test: Parse TryCatchStmt
TEST_F(AstParsingTest, ParseTryCatchStmt) {
    std::string input = "FuncDecl name=main ret=int\n"
                        "  BlockStmt\n"
                        "    TryCatchStmt\n"
                        "      ThrowStmt\n"
                        "        IntLiteral value=1\n"
                        "      CatchBlock type=int name=e\n"
                        "        ReturnStmt\n"
                        "          IntLiteral value=0\n";
    auto result = parse_ast_format(input, ctx);
    ASSERT_TRUE(result.has_value());
    ASSERT_EQ(result->size(), 1);
}

// Test: Parse MatchingStmt
TEST_F(AstParsingTest, ParseMatchingStmt) {
    std::string input = "FuncDecl name=main ret=int\n"
                        "  BlockStmt\n"
                        "    MatchingStmt\n"
                        "      VarRef name=x\n"
                        "      MatchingCase\n"
                        "        IntLiteral value=1\n"
                        "        ReturnStmt\n"
                        "          IntLiteral value=1\n";
    auto result = parse_ast_format(input, ctx);
    ASSERT_TRUE(result.has_value());
    ASSERT_EQ(result->size(), 1);
}

// Test: Parse CastExpr
TEST_F(AstParsingTest, ParseCastExpr) {
    std::string input = "FuncDecl name=main ret=int\n"
                        "  BlockStmt\n"
                        "    VarDecl name=val type=int\n"
                        "      CastExpr type=int\n"
                        "        IntLiteral value=42\n";
    auto result = parse_ast_format(input, ctx);
    ASSERT_TRUE(result.has_value());
    ASSERT_EQ(result->size(), 1);
}

// Test: Parse ArrayInit
TEST_F(AstParsingTest, ParseArrayInit) {
    std::string input = "FuncDecl name=main ret=int\n"
                        "  BlockStmt\n"
                        "    VarDecl name=arr type=int\n"
                        "      ArrayInit\n"
                        "        IntLiteral value=1\n"
                        "        IntLiteral value=2\n";
    auto result = parse_ast_format(input, ctx);
    ASSERT_TRUE(result.has_value());
    ASSERT_EQ(result->size(), 1);
}

// Test: Parse error on unknown type
TEST_F(AstParsingTest, ParseUnknownType) {
    std::string input = "UnknownType name=x\n";
    auto result = parse_ast_format(input, ctx);
    ASSERT_FALSE(result.has_value());
}

// ============================================================================
// parse_attr Tests — полное покрытие всех вариантов входных данных
// ============================================================================

// --- Стандартные key=value ---

TEST(ParseAttrTest, StandardKeyValue) {
    auto result = parse_attr("name=main");
    EXPECT_EQ(result.first, "name");
    EXPECT_EQ(result.second, "main");
}

TEST(ParseAttrTest, NumericValue) {
    auto result = parse_attr("value=10");
    EXPECT_EQ(result.first, "value");
    EXPECT_EQ(result.second, "10");
}

TEST(ParseAttrTest, SingleCharKeyAndValue) {
    auto result = parse_attr("x=1");
    EXPECT_EQ(result.first, "x");
    EXPECT_EQ(result.second, "1");
}

// --- Ключ с цифрами ---

TEST(ParseAttrTest, KeyWithDigits) {
    auto result = parse_attr("key123=val");
    EXPECT_EQ(result.first, "key123");
    EXPECT_EQ(result.second, "val");
}

TEST(ParseAttrTest, KeyEndingWithDigit) {
    auto result = parse_attr("name1=test");
    EXPECT_EQ(result.first, "name1");
    EXPECT_EQ(result.second, "test");
}

// --- Ключ с подчеркиванием ---

TEST(ParseAttrTest, KeyWithUnderscore) {
    auto result = parse_attr("my_key=value");
    EXPECT_EQ(result.first, "my_key");
    EXPECT_EQ(result.second, "value");
}

TEST(ParseAttrTest, KeyStartingWithUnderscore) {
    auto result = parse_attr("_private=val");
    EXPECT_EQ(result.first, "_private");
    EXPECT_EQ(result.second, "val");
}

// --- Смешанный ключ ---

TEST(ParseAttrTest, MixedKeyAlnumUnderscore) {
    auto result = parse_attr("A1_b=val");
    EXPECT_EQ(result.first, "A1_b");
    EXPECT_EQ(result.second, "val");
}

// --- Одиночная буква ---

TEST(ParseAttrTest, SingleLetterKeyValue) {
    auto result = parse_attr("a=b");
    EXPECT_EQ(result.first, "a");
    EXPECT_EQ(result.second, "b");
}

// --- Comparison операторы (НЕ должны матчиться с ==) ---

TEST(ParseAttrTest, DoubleEqualsOp) {
    // op=== → BinaryOp with == operator, parse_attr should return {"op", "=="}
    auto result = parse_attr("op===");
    EXPECT_EQ(result.first, "op");
    EXPECT_EQ(result.second, "==");
}

TEST(ParseAttrTest, DoubleEqualsOpEmpty) {
    // op== → BinaryOp with == operator, parse_attr should return {"op", "=="}
    auto result = parse_attr("op==");
    EXPECT_EQ(result.first, "op");
    EXPECT_EQ(result.second, "==");
}

TEST(ParseAttrTest, NotEqualOp) {
    // op=!= → != operator
    auto result = parse_attr("op=!=");
    EXPECT_EQ(result.first, "op");
    EXPECT_EQ(result.second, "!=");
}

TEST(ParseAttrTest, LessEqualOp) {
    // op=<== → <= operator
    auto result = parse_attr("op=<=");
    EXPECT_EQ(result.first, "op");
    EXPECT_EQ(result.second, "<=");
}

TEST(ParseAttrTest, GreaterEqualOp) {
    // op=>== → >= operator
    auto result = parse_attr("op=>=");
    EXPECT_EQ(result.first, "op");
    EXPECT_EQ(result.second, ">=");
}

// --- Арифметические операторы ---

TEST(ParseAttrTest, AddOp) {
    auto result = parse_attr("op=+");
    EXPECT_EQ(result.first, "op");
    EXPECT_EQ(result.second, "+");
}

TEST(ParseAttrTest, SubOp) {
    auto result = parse_attr("op=-");
    EXPECT_EQ(result.first, "op");
    EXPECT_EQ(result.second, "-");
}

TEST(ParseAttrTest, MulOp) {
    auto result = parse_attr("op=*");
    EXPECT_EQ(result.first, "op");
    EXPECT_EQ(result.second, "*");
}

TEST(ParseAttrTest, DivOp) {
    auto result = parse_attr("op=/");
    EXPECT_EQ(result.first, "op");
    EXPECT_EQ(result.second, "/");
}

TEST(ParseAttrTest, LessThanOp) {
    auto result = parse_attr("op=<");
    EXPECT_EQ(result.first, "op");
    EXPECT_EQ(result.second, "<");
}

TEST(ParseAttrTest, GreaterThanOp) {
    auto result = parse_attr("op=>");
    EXPECT_EQ(result.first, "op");
    EXPECT_EQ(result.second, ">");
}

// --- Логические операторы ---

TEST(ParseAttrTest, AndOp) {
    auto result = parse_attr("op=and");
    EXPECT_EQ(result.first, "op");
    EXPECT_EQ(result.second, "and");
}

TEST(ParseAttrTest, OrOp) {
    auto result = parse_attr("op=or");
    EXPECT_EQ(result.first, "op");
    EXPECT_EQ(result.second, "or");
}

// --- Значение содержит '=' ---

TEST(ParseAttrTest, ValueContainsEquals) {
    auto result = parse_attr("key=val=ue");
    EXPECT_EQ(result.first, "key");
    EXPECT_EQ(result.second, "val=ue");
}

// --- '=' в начале строки ---

TEST(ParseAttrTest, EqualsAtStart) {
    auto result = parse_attr("=value");
    EXPECT_EQ(result.first, "");
    EXPECT_EQ(result.second, "=value");
}

// --- Пустое значение ---

TEST(ParseAttrTest, EmptyValue) {
    auto result = parse_attr("key=");
    EXPECT_EQ(result.first, "key");
    EXPECT_EQ(result.second, "");
}

// --- Нет '=' (только ключ) ---

TEST(ParseAttrTest, NoEqualsKeyOnly) {
    auto result = parse_attr("BlockStmt");
    EXPECT_EQ(result.first, "");
    EXPECT_EQ(result.second, "BlockStmt");
}

// --- '==' в конце ---

TEST(ParseAttrTest, DoubleEqualsAtEnd) {
    auto result = parse_attr("key==");
    EXPECT_EQ(result.first, "");
    EXPECT_EQ(result.second, "key==");
}

// --- Значение заканчивается на '==' ---

TEST(ParseAttrTest, ValueEndsWithDoubleEquals) {
    auto result = parse_attr("key=val==");
    EXPECT_EQ(result.first, "key");
    EXPECT_EQ(result.second, "val==");
}

// --- Значение в кавычках ---

TEST(ParseAttrTest, QuotedValue) {
    auto result = parse_attr("value=\"hello\"");
    EXPECT_EQ(result.first, "value");
    EXPECT_EQ(result.second, "\"hello\"");
}

TEST(ParseAttrTest, QuotedValueWithSpaces) {
    auto result = parse_attr("value=\"hello world\"");
    EXPECT_EQ(result.first, "value");
    EXPECT_EQ(result.second, "\"hello world\"");
}

// --- Пустая строка ---

TEST(ParseAttrTest, EmptyString) {
    auto result = parse_attr("");
    EXPECT_EQ(result.first, "");
    EXPECT_EQ(result.second, "");
}

// --- Один символ ---

TEST(ParseAttrTest, SingleChar) {
    auto result = parse_attr("x");
    EXPECT_EQ(result.first, "");
    EXPECT_EQ(result.second, "x");
}

TEST(ParseAttrTest, SingleEquals) {
    auto result = parse_attr("=");
    EXPECT_EQ(result.first, "");
    EXPECT_EQ(result.second, "=");
}

// --- Ключ заканчивается на цифру ---

TEST(ParseAttrTest, KeyEndsWithDigit) {
    auto result = parse_attr("v2=1");
    EXPECT_EQ(result.first, "v2");
    EXPECT_EQ(result.second, "1");
}

// --- Несколько '=' подряд (но не в начале) ---
// "key===val" → первый = после "key" за которым следует '=' → skip (как ==)
// Результат: нет валидного разделителя → {"", original}
TEST(ParseAttrTest, MultipleEqualsInValue) {
    auto result = parse_attr("key===val");
    EXPECT_EQ(result.first, "");
    EXPECT_EQ(result.second, "key===val");
}

// --- Значение с escape-последовательностями ---

TEST(ParseAttrTest, ValueWithNewline) {
    auto result = parse_attr("value=\"hello\\n\"");
    EXPECT_EQ(result.first, "value");
    EXPECT_EQ(result.second, "\"hello\\n\"");
}

// --- Значение с обратной косой чертой ---

TEST(ParseAttrTest, ValueWithBackslash) {
    auto result = parse_attr("value=\"path\\\\file\"");
    EXPECT_EQ(result.first, "value");
    EXPECT_EQ(result.second, "\"path\\\\file\"");
}