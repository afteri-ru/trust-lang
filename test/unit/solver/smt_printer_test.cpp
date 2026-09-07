#include "solver/smt_ast.hpp"
#include "solver/smt_printer.hpp"

#include <gtest/gtest.h>

#include <memory>
#include <string>

using namespace trust::solver;

// Printer tests for SMT-LIB 2 (sorts, terms, commands, scripts).
// Z3-dependent contract/file tests live in smt_printer_z3_test.cpp.

// Test: print bool sort
TEST(SmtPrinterTest, PrintBoolSort) {
    SmtSort s;
    s.kind = SmtSortKind::kBool;
    EXPECT_EQ(SmtPrinter::printSort(s), "Bool");
}

// Test: print int sort
TEST(SmtPrinterTest, PrintIntSort) {
    SmtSort s;
    s.kind = SmtSortKind::kInt;
    EXPECT_EQ(SmtPrinter::printSort(s), "Int");
}

// Test: print bitvec sort
TEST(SmtPrinterTest, PrintBitVecSort) {
    SmtSort s;
    s.kind = SmtSortKind::kBitVec;
    s.bv_width = 8;
    EXPECT_EQ(SmtPrinter::printSort(s), "(_ BitVec 8)");
}

// Test: print array sort
TEST(SmtPrinterTest, PrintArraySort) {
    auto domain = std::make_shared<SmtSort>();
    domain->kind = SmtSortKind::kInt;
    auto range = std::make_shared<SmtSort>();
    range->kind = SmtSortKind::kBool;
    SmtSort s;
    s.kind = SmtSortKind::kArray;
    s.domain = domain;
    s.range = range;
    EXPECT_EQ(SmtPrinter::printSort(s), "(Array Int Bool)");
}

// Test: print constant term
TEST(SmtPrinterTest, PrintConstTerm) {
    SmtTerm t;
    t.kind = SmtTermKind::kConst;
    t.const_value = "42";
    EXPECT_EQ(SmtPrinter::printTerm(t), "42");
}

// Test: print named variable
TEST(SmtPrinterTest, PrintNamedVar) {
    SmtTerm t;
    t.kind = SmtTermKind::kNamedVar;
    t.var_name = "x";
    EXPECT_EQ(SmtPrinter::printTerm(t), "x");
}

// Test: print function application
TEST(SmtPrinterTest, PrintAppTerm) {
    SmtTerm t;
    t.kind = SmtTermKind::kApp;
    t.fun_name = "+";
    auto a1 = std::make_shared<SmtTerm>();
    a1->kind = SmtTermKind::kConst;
    a1->const_value = "1";
    auto a2 = std::make_shared<SmtTerm>();
    a2->kind = SmtTermKind::kConst;
    a2->const_value = "2";
    t.args.push_back(a1);
    t.args.push_back(a2);
    EXPECT_EQ(SmtPrinter::printTerm(t), "(+ 1 2)");
}

// Встроенный оператор (op установлен) печатается из fun_name (принтер не зависит от op).
TEST(SmtPrinterTest, PrintAppWithOp) {
    SmtTerm t;
    t.kind = SmtTermKind::kApp;
    t.op = SmtOp::BvAdd;
    t.fun_name = "bvadd";
    auto a1 = std::make_shared<SmtTerm>();
    a1->kind = SmtTermKind::kNamedVar;
    a1->var_name = "x";
    auto a2 = std::make_shared<SmtTerm>();
    a2->kind = SmtTermKind::kConst;
    a2->const_value = "1";
    t.args.push_back(a1);
    t.args.push_back(a2);
    EXPECT_EQ(SmtPrinter::printTerm(t), "(bvadd x 1)");
    EXPECT_EQ(t.op.value(), SmtOp::BvAdd);
}

// Test: print assert command
TEST(SmtPrinterTest, PrintAssertCommand) {
    SmtCommand cmd;
    cmd.kind = SmtCommandKind::kAssert;
    cmd.assert_term = std::make_shared<SmtTerm>();
    cmd.assert_term->kind = SmtTermKind::kConst;
    cmd.assert_term->const_value = "true";
    EXPECT_EQ(SmtPrinter::printCommand(cmd), "(assert true)");
}

// Test: print check-sat
TEST(SmtPrinterTest, PrintCheckSat) {
    SmtCommand cmd;
    cmd.kind = SmtCommandKind::kCheckSat;
    EXPECT_EQ(SmtPrinter::printCommand(cmd), "(check-sat)");
}

// Test: print set-logic
TEST(SmtPrinterTest, PrintSetLogic) {
    SmtCommand cmd;
    cmd.kind = SmtCommandKind::kSetLogic;
    cmd.logic_name = "QF_LIA";
    EXPECT_EQ(SmtPrinter::printCommand(cmd), "(set-logic QF_LIA)");
}

// Test: print declare-fun
TEST(SmtPrinterTest, PrintDeclareFun) {
    SmtCommand cmd;
    cmd.kind = SmtCommandKind::kDeclareFun;
    cmd.fun_name = "f";
    SmtSort arg_sort;
    arg_sort.kind = SmtSortKind::kInt;
    cmd.fun_arg_sorts.push_back(arg_sort);
    auto result_sort = std::make_shared<SmtSort>();
    result_sort->kind = SmtSortKind::kBool;
    cmd.fun_result_sort = result_sort;
    std::string output = SmtPrinter::printCommand(cmd);
    EXPECT_TRUE(output.find("declare-fun") != std::string::npos);
    EXPECT_TRUE(output.find("f") != std::string::npos);
    EXPECT_TRUE(output.find("Int") != std::string::npos);
    EXPECT_TRUE(output.find("Bool") != std::string::npos);
}

// Test: print script
TEST(SmtPrinterTest, PrintScript) {
    SmtScript script;
    script.logic = "QF_LIA";
    SmtCommand cmd;
    cmd.kind = SmtCommandKind::kCheckSat;
    script.commands.push_back(cmd);
    std::string output = SmtPrinter::printScript(script);
    EXPECT_TRUE(output.find("set-logic") != std::string::npos);
    EXPECT_TRUE(output.find("check-sat") != std::string::npos);
}

// Test: escape symbol with special chars
TEST(SmtPrinterTest, EscapeSymbol) {
    EXPECT_EQ(SmtPrinter::escapeSymbol("hello_world"), "hello_world");
    // + is a valid simple symbol char in SMT-LIB 2, no escaping needed
    EXPECT_EQ(SmtPrinter::escapeSymbol("a+b"), "a+b");
    // spaces are invalid, must be escaped
    EXPECT_EQ(SmtPrinter::escapeSymbol("a b"), "|a b|");
    EXPECT_EQ(SmtPrinter::escapeSymbol("simple"), "simple");
}

// Test: print forall term
TEST(SmtPrinterTest, PrintForallTerm) {
    SmtTerm t;
    t.kind = SmtTermKind::kForall;
    t.quant_vars.push_back("x");
    t.quant_body = std::make_shared<SmtTerm>();
    t.quant_body->kind = SmtTermKind::kConst;
    t.quant_body->const_value = "true";
    std::string output = SmtPrinter::printTerm(t);
    EXPECT_TRUE(output.find("forall") != std::string::npos);
    EXPECT_TRUE(output.find("x") != std::string::npos);
}

// Test: push/pop commands
TEST(SmtPrinterTest, PrintPushPop) {
    SmtCommand push_cmd;
    push_cmd.kind = SmtCommandKind::kPush;
    push_cmd.stack_depth = 1;
    EXPECT_EQ(SmtPrinter::printCommand(push_cmd), "(push 1)");

    SmtCommand pop_cmd;
    pop_cmd.kind = SmtCommandKind::kPop;
    pop_cmd.stack_depth = 2;
    EXPECT_EQ(SmtPrinter::printCommand(pop_cmd), "(pop 2)");
}
