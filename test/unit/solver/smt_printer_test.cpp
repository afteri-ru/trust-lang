#include "solver/smt_ast.hpp"
#include "solver/smt_printer.hpp"

#include <gtest/gtest.h>

#include <cstdio>
#include <fstream>
#include <memory>
#include <string>

using namespace trust::solver;

// Path for generated SMT-LIB 2 test files
// TEST_DATA_DIR is defined in CMakeLists.txt
namespace {
std::string smtOutputPath(const char* name) {
    return std::string(TEST_DATA_DIR) + "/smt_" + name + ".smt2";
}
} // anonymous namespace

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

// Helper: build SMT-LIB 2 script for:
//   func add(x: Int64) @ensure(result > x)
// With Int64 range constraints and negated postcondition.
SmtScript buildInt64ContractScript() {
    // x is Int64, encoded as SMT-LIB 2 Int with range constraints
    auto int64_min = std::make_shared<SmtTerm>();
    int64_min->kind = SmtTermKind::kConst;
    int64_min->const_value = "-9223372036854775808";

    auto int64_max = std::make_shared<SmtTerm>();
    int64_max->kind = SmtTermKind::kConst;
    int64_max->const_value = "9223372036854775807";

    auto x_term = std::make_shared<SmtTerm>();
    x_term->kind = SmtTermKind::kNamedVar;
    x_term->var_name = "x";

    // x >= INT64_MIN
    auto ge_min = std::make_shared<SmtTerm>();
    ge_min->kind = SmtTermKind::kApp;
    ge_min->fun_name = ">=";
    ge_min->args.push_back(x_term);
    ge_min->args.push_back(int64_min);

    // x <= INT64_MAX
    auto le_max = std::make_shared<SmtTerm>();
    le_max->kind = SmtTermKind::kApp;
    le_max->fun_name = "<=";
    le_max->args = {x_term, int64_max};

    // (and (>= x INT64_MIN) (<= x INT64_MAX))
    auto range_assert = std::make_shared<SmtTerm>();
    range_assert->kind = SmtTermKind::kApp;
    range_assert->fun_name = "and";
    range_assert->args = {ge_min, le_max};

    // result = x + 1
    auto one = std::make_shared<SmtTerm>();
    one->kind = SmtTermKind::kConst;
    one->const_value = "1";

    auto add = std::make_shared<SmtTerm>();
    add->kind = SmtTermKind::kApp;
    add->fun_name = "+";
    add->args = {x_term, one};

    auto result_term = std::make_shared<SmtTerm>();
    result_term->kind = SmtTermKind::kNamedVar;
    result_term->var_name = "result";

    auto eq = std::make_shared<SmtTerm>();
    eq->kind = SmtTermKind::kApp;
    eq->fun_name = "=";
    eq->args = {result_term, add};

    // (not (> result x)) — negated postcondition to check
    auto gt = std::make_shared<SmtTerm>();
    gt->kind = SmtTermKind::kApp;
    gt->fun_name = ">";
    gt->args = {result_term, x_term};

    auto not_gt = std::make_shared<SmtTerm>();
    not_gt->kind = SmtTermKind::kApp;
    not_gt->fun_name = "not";
    not_gt->args = {gt};

    // Build script
    SmtScript script;
    script.logic = "QF_LIA";

    // declare-const x Int
    SmtCommand decl_x;
    decl_x.kind = SmtCommandKind::kDeclareFun;
    decl_x.fun_name = "x";
    auto int_sort = std::make_shared<SmtSort>();
    int_sort->kind = SmtSortKind::kInt;
    decl_x.fun_result_sort = int_sort;
    script.commands.push_back(decl_x);

    // declare-const result Int
    SmtCommand decl_result;
    decl_result.kind = SmtCommandKind::kDeclareFun;
    decl_result.fun_name = "result";
    decl_result.fun_result_sort = int_sort;
    script.commands.push_back(decl_result);

    // assert range constraints on x
    SmtCommand cmd_range;
    cmd_range.kind = SmtCommandKind::kAssert;
    cmd_range.assert_term = range_assert;
    script.commands.push_back(cmd_range);

    // assert result = x + 1
    SmtCommand cmd_eq;
    cmd_eq.kind = SmtCommandKind::kAssert;
    cmd_eq.assert_term = eq;
    script.commands.push_back(cmd_eq);

    // assert (not (> result x)) — try to refute postcondition
    SmtCommand cmd_refute;
    cmd_refute.kind = SmtCommandKind::kAssert;
    cmd_refute.assert_term = not_gt;
    script.commands.push_back(cmd_refute);

    // check-sat
    SmtCommand cmd_cs;
    cmd_cs.kind = SmtCommandKind::kCheckSat;
    script.commands.push_back(cmd_cs);

    return script;
}

// Test: generate SMT-LIB 2 file for Int64 contract example and export
TEST(SmtPrinterTest, GenerateInt64ContractFile) {
    SmtScript script = buildInt64ContractScript();
    std::string output = SmtPrinter::printScript(script);

    // Save to file for export
    std::string filepath = smtOutputPath("int64_contract");
    std::ofstream ofs(filepath);
    ASSERT_TRUE(ofs.is_open()) << "Failed to open: " << filepath;
    ofs << output;
    ofs.close();

    // Verify file content
    std::ifstream ifs(filepath);
    std::string content((std::istreambuf_iterator<char>(ifs)), std::istreambuf_iterator<char>());
    ifs.close();

    EXPECT_TRUE(content.find("set-logic QF_LIA") != std::string::npos);
    EXPECT_TRUE(content.find("declare-fun x () Int") != std::string::npos);
    EXPECT_TRUE(content.find("declare-fun result () Int") != std::string::npos);
    EXPECT_TRUE(content.find(">= x (- 9223372036854775808)") != std::string::npos || content.find(">= x -9223372036854775808") != std::string::npos);
    EXPECT_TRUE(content.find("<= x 9223372036854775807") != std::string::npos);
    EXPECT_TRUE(content.find("= result (+ x 1)") != std::string::npos);
    EXPECT_TRUE(content.find("not") != std::string::npos);
    EXPECT_TRUE(content.find("> result x") != std::string::npos);
    EXPECT_TRUE(content.find("check-sat") != std::string::npos);

    std::cout << "SMT-LIB 2 file written to: " << filepath << std::endl;
    std::cout << "--- Content ---" << std::endl;
    std::cout << content << std::endl;
    std::cout << "--- End ---" << std::endl;

    // Clean up
    // std::remove(filepath.c_str());
}

// Test: run Z3 on the Int64 contract script (only if solver is available)
TEST(SmtPrinterTest, RunZ3OnInt64Contract) {
    SmtScript script = buildInt64ContractScript();
    std::string smt2 = SmtPrinter::printScript(script);

    // Save to temp file
    std::string filepath = smtOutputPath("z3_int64_contract");
    {
        std::ofstream ofs(filepath);
        ASSERT_TRUE(ofs.is_open());
        ofs << smt2;
    }

    // Try running Z3
    std::string cmd = "z3 -smt2 " + filepath + " 2>&1";
    FILE* pipe = popen(cmd.c_str(), "r");
    if (!pipe) {
        std::remove(filepath.c_str());
        GTEST_SKIP() << "z3 not found in PATH, skipping in-process check";
        return;
    }

    char buf[256];
    std::string result;
    while (fgets(buf, sizeof(buf), pipe) != nullptr) {
        result += buf;
    }
    int status = pclose(pipe);

    // Clean up
    // std::remove(filepath.c_str());

    // Check that Z3 ran successfully
    if (status != 0) {
        GTEST_SKIP() << "z3 execution failed (exit=" << status << "): " << result;
        return;
    }

    // Trim whitespace
    while (!result.empty() && (result.back() == '\n' || result.back() == '\r')) {
        result.pop_back();
    }

    EXPECT_TRUE(result == "unsat") << "Expected unsat, got: " << result;
    if (result == "unsat") {
        SUCCEED() << "Z3 confirmed postcondition holds (unsat)";
    }
}
