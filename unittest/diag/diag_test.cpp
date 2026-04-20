#include "diag/context.hpp"
#include "diag/diag.hpp"

#include <gtest/gtest.h>
#include <sstream>
#include <string>

using namespace trust;

static std::string generate_source(int num_lines) {
    std::string s;
    for (int i = 1; i <= num_lines; ++i) {
        s += "line" + std::to_string(i) + "\n";
    }
    return s;
}

class DiagFixture : public ::testing::Test {
protected:
    DiagFixture() : m_source(generate_source(50)), m_src(m_ctx.add_source("test.cpp", m_source)) {}

    void SetUp() override {
        m_stream.str("");
        m_ctx.diag().clear();
        m_ctx.diag().setMinSeverity(Severity::Remark);
        m_ctx.diag().setOutput(&m_stream);
    }

    void TearDown() override {
        m_ctx.diag().setOutput(nullptr);
    }

    static SourceRange range(SourceLoc begin, SourceLoc end) {
        return {begin, end};
    }

    std::stringstream m_stream;
    std::string m_source;
    Context m_ctx;
    SourceIdx m_src{};

    std::string output() {
        return m_stream.str();
    }
};

TEST_F(DiagFixture, ErrorWithLocation) {
    auto loc = m_ctx.loc_from_line(m_src, 10);
    m_ctx.diag().report(loc, Severity::Error, "unexpected token '{}'", "foo");

    std::string out = output();
    EXPECT_NE(out.find("test.cpp:10:1: error: unexpected token 'foo'"), std::string::npos) << "Output: " << out;
    EXPECT_EQ(m_ctx.diag().errorCount(), 1);
}

TEST_F(DiagFixture, WarningWithLocation) {
    auto loc = m_ctx.loc_from_line(m_src, 42);
    m_ctx.diag().report(loc, Severity::Warning, "deprecated function '{}'", "old_func");

    std::string out = output();
    EXPECT_NE(out.find("test.cpp:42:1: warning: deprecated function 'old_func'"), std::string::npos)
        << "Output: " << out;
    EXPECT_EQ(m_ctx.diag().warningCount(), 1);
}

TEST_F(DiagFixture, NoteWithLocation) {
    auto loc = m_ctx.loc_from_line(m_src, 5);
    m_ctx.diag().report(loc, Severity::Note, "did you mean '{}'?", "bar");

    std::string out = output();
    EXPECT_NE(out.find("test.cpp:5:1: note: did you mean 'bar'?"), std::string::npos)
        << "Output: " << out;
}

TEST_F(DiagFixture, ErrorWithoutLocation) {
    auto loc = SourceLoc::invalid();
    m_ctx.diag().report(loc, Severity::Error, "internal error");

    std::string out = output();
    EXPECT_NE(out.find("error: internal error"), std::string::npos)
        << "Output: " << out;
    EXPECT_FALSE(loc.isValid());
}

TEST_F(DiagFixture, SeverityFiltering) {
    m_ctx.diag().setMinSeverity(Severity::Warning);
    m_ctx.diag().clear();
    m_stream.str("");

    auto loc = m_ctx.loc_from_line(m_src, 1);

    m_ctx.diag().report(loc, Severity::Remark, "optimization hint");
    m_ctx.diag().report(loc, Severity::Note, "additional info");

    std::string out = output();
    EXPECT_TRUE(out.find("remark") == std::string::npos)
        << "Remark should be filtered";

    m_ctx.diag().report(loc, Severity::Warning, "test warning");
    m_ctx.diag().report(loc, Severity::Error, "test error");

    out = output();
    EXPECT_NE(out.find("test warning"), std::string::npos)
        << "Warning should pass";
    EXPECT_NE(out.find("test error"), std::string::npos)
        << "Error should pass";
}

TEST_F(DiagFixture, ErrorCount) {
    auto loc = m_ctx.loc_from_line(m_src, 1);
    m_ctx.diag().report(loc, Severity::Error, "error 1");
    m_ctx.diag().report(loc, Severity::Error, "error 2");
    m_ctx.diag().report(loc, Severity::Warning, "warning 1");

    EXPECT_EQ(m_ctx.diag().errorCount(), 2);
    EXPECT_EQ(m_ctx.diag().warningCount(), 1);
}

TEST_F(DiagFixture, ClearResetsCounts) {
    auto loc = m_ctx.loc_from_line(m_src, 1);
    m_ctx.diag().report(loc, Severity::Error, "error");
    m_ctx.diag().report(loc, Severity::Warning, "warning");
    EXPECT_EQ(m_ctx.diag().errorCount(), 1);
    EXPECT_EQ(m_ctx.diag().warningCount(), 1);

    m_ctx.diag().clear();
    EXPECT_EQ(m_ctx.diag().errorCount(), 0);
    EXPECT_EQ(m_ctx.diag().warningCount(), 0);
}

TEST_F(DiagFixture, FormatString) {
    auto loc = m_ctx.loc_from_line(m_src, 1);
    m_ctx.diag().report(loc, Severity::Error, "value {}, float {:.2f}, str {}", 42, 3.14159, "hello");

    std::string out = output();
    EXPECT_NE(out.find("value 42, float 3.14, str hello"), std::string::npos)
        << "Output: " << out;
}

TEST_F(DiagFixture, RemarkSeverity) {
    auto loc = m_ctx.loc_from_line(m_src, 1);
    m_ctx.diag().report(loc, Severity::Remark, "remark message");

    std::string out = output();
    EXPECT_NE(out.find("remark: remark message"), std::string::npos)
        << "Output: " << out;
}

TEST_F(DiagFixture, FatalWithLocation) {
    auto loc = m_ctx.loc_from_line(m_src, 1);
    m_ctx.diag().report(loc, Severity::Fatal, "internal compiler error: {}", "stack overflow");

    std::string out = output();
    EXPECT_NE(out.find("test.cpp:1:1: fatal: internal compiler error: stack overflow"), std::string::npos)
        << "Output: " << out;
}

TEST_F(DiagFixture, FatalSeverityFiltering) {
    m_ctx.diag().setMinSeverity(Severity::Error);
    m_ctx.diag().clear();
    m_stream.str("");

    auto loc = m_ctx.loc_from_line(m_src, 1);
    m_ctx.diag().report(loc, Severity::Warning, "should be filtered");
    m_ctx.diag().report(loc, Severity::Fatal, "fatal error");

    std::string out = output();
    EXPECT_TRUE(out.find("warning") == std::string::npos)
        << "Warning should be filtered";
    EXPECT_NE(out.find("fatal: fatal error"), std::string::npos)
        << "Fatal should pass";
}

TEST_F(DiagFixture, MinSeverityGetter) {
    m_ctx.diag().setMinSeverity(Severity::Warning);
    EXPECT_EQ(m_ctx.diag().minSeverity(), Severity::Warning);

    m_ctx.diag().setMinSeverity(Severity::Error);
    EXPECT_EQ(m_ctx.diag().minSeverity(), Severity::Error);

    m_ctx.diag().setMinSeverity(Severity::Fatal);
    EXPECT_EQ(m_ctx.diag().minSeverity(), Severity::Fatal);
}

TEST_F(DiagFixture, CaretWithSingleLocation) {
    std::string source = std::string(
        "int main() {\n"
        "    int x = foo();\n"
        "    return 0;\n"
        "}\n"
    );
    Context ctx;
    SourceIdx src = ctx.add_source("test.cpp", source);
    ctx.diag().setMinSeverity(Severity::Remark);
    ctx.diag().setOutput(&m_stream);

    auto loc = ctx.loc_from_line(src, 2);
    auto new_loc = SourceLoc::make(src, loc.offset() + 12);
    ctx.diag().report(new_loc, Severity::Error, "unknown identifier '{}'", "foo");

    std::string out = output();
    EXPECT_NE(out.find("test.cpp:2:13: error: unknown identifier 'foo'"), std::string::npos)
        << "Output: " << out;
    EXPECT_NE(out.find("    int x = foo();"), std::string::npos)
        << "Source line should be present";
    EXPECT_NE(out.find("            ^"), std::string::npos)
        << "Caret should point to column 13";
}

TEST_F(DiagFixture, CaretWithRange) {
    std::string source = std::string(
        "int x = 42;\n"
        "int y = foobar();\n"
    );
    Context ctx;
    SourceIdx src = ctx.add_source("test.cpp", source);
    ctx.diag().setMinSeverity(Severity::Remark);
    ctx.diag().setOutput(&m_stream);

    auto begin_pos = source.find("foobar", 12);
    auto begin = SourceLoc::make(src, static_cast<int>(begin_pos) + 1);
    auto end = SourceLoc::make(src, static_cast<int>(begin_pos) + 1 + 6);

    ctx.diag().report(range(begin, end), Severity::Error, "unknown identifier '{}'", "foobar");

    std::string out = output();
    EXPECT_NE(out.find("test.cpp:2:9: error: unknown identifier 'foobar'"), std::string::npos)
        << "Output: " << out;
    EXPECT_NE(out.find("int y = foobar();"), std::string::npos)
        << "Source line should be present";
    EXPECT_NE(out.find("        ^~~~~~"), std::string::npos)
        << "Caret underline should start at column 9";
}

TEST_F(DiagFixture, CaretWithoutLocation) {
    m_ctx.diag().report(SourceLoc::invalid(), Severity::Error, "internal error");

    std::string out = output();
    EXPECT_NE(out.find("error: internal error"), std::string::npos)
        << "Output: " << out;
    EXPECT_EQ(out.find("^\n"), std::string::npos)
        << "No caret for invalid location";
}

TEST_F(DiagFixture, CaretAtDifferentColumn) {
    std::string source = std::string(
        "void test() { auto x = 1 + 2 * 3; }\n"
    );
    Context ctx;
    SourceIdx src = ctx.add_source("test.cpp", source);
    ctx.diag().setMinSeverity(Severity::Remark);
    ctx.diag().setOutput(&m_stream);

    auto loc = ctx.loc_from_line(src, 1);
    auto new_loc = SourceLoc::make(src, loc.offset() + 26);

    ctx.diag().report(new_loc, Severity::Error, "magic number '{}'", 3);

    std::string out = output();
    EXPECT_NE(out.find("test.cpp:1:27: error: magic number '3'"), std::string::npos)
        << "Output: " << out;
    EXPECT_NE(out.find("                          ^"), std::string::npos)
        << "Caret at correct column";
}

TEST_F(DiagFixture, RangeSpanningSameLine) {
    std::string source = std::string("const char* name = \"hello world\";\n");
    Context ctx;
    SourceIdx src = ctx.add_source("test.cpp", source);
    ctx.diag().setMinSeverity(Severity::Remark);
    ctx.diag().setOutput(&m_stream);

    auto hello_pos = source.find("hello");
    auto world_end_pos = source.find("world") + 5;
    auto begin = SourceLoc::make(src, static_cast<int>(hello_pos) + 1);
    auto end = SourceLoc::make(src, static_cast<int>(world_end_pos) + 1);

    ctx.diag().report(range(begin, end), Severity::Warning, "string literal used");

    std::string out = output();
    EXPECT_NE(out.find("test.cpp:1:"), std::string::npos)
        << "Output: " << out;
    EXPECT_NE(out.find("hello world"), std::string::npos)
        << "Source line should be present";
}

TEST_F(DiagFixture, FirstLineWithCaret) {
    std::string source = std::string("x = 1;\n");
    Context ctx;
    SourceIdx src = ctx.add_source("test.cpp", source);
    ctx.diag().setMinSeverity(Severity::Remark);
    ctx.diag().setOutput(&m_stream);

    auto loc = ctx.loc_from_line(src, 1);
    ctx.diag().report(loc, Severity::Error, "unexpected 'x'");

    std::string out = output();
    EXPECT_NE(out.find("test.cpp:1:1: error: unexpected 'x'"), std::string::npos)
        << "Output: " << out;
    EXPECT_NE(out.find("x = 1;"), std::string::npos);
    EXPECT_NE(out.find("^\n"), std::string::npos)
        << "Caret at first column";
}

TEST_F(DiagFixture, SourceRange_Point) {
    auto loc = m_ctx.loc_from_line(m_src, 5);
    auto rng = SourceRange::point(loc);
    EXPECT_TRUE(rng.is_point());
    m_ctx.diag().report(loc, Severity::Error, "point error");

    std::string out = output();
    EXPECT_NE(out.find("test.cpp:5:1: error: point error"), std::string::npos)
        << "Output: " << out;
}

TEST_F(DiagFixture, ReportWithRange) {
    std::string source = "int x = foo;\n";
    Context ctx;
    SourceIdx src = ctx.add_source("test.cpp", source);
    ctx.diag().setMinSeverity(Severity::Remark);
    ctx.diag().setOutput(&m_stream);

    auto foo_pos = source.find("foo");
    auto begin = SourceLoc::make(src, static_cast<int>(foo_pos) + 1);
    auto end = SourceLoc::make(src, static_cast<int>(foo_pos) + 1 + 3);
    auto range = SourceRange{begin, end};
    ctx.diag().report(range, Severity::Warning, "unused variable");

    std::string out = output();
    EXPECT_NE(out.find("warning: unused variable"), std::string::npos)
        << "Output: " << out;
    EXPECT_NE(out.find("^~~"), std::string::npos)
        << "Caret underline should show range";
}

TEST_F(DiagFixture, MultiSource) {
    Context ctx;
    SourceIdx src_a = ctx.add_source("a.cpp", "int x = 1;\n");
    SourceIdx src_b = ctx.add_source("b.cpp", "int y = 2;\n");
    ctx.diag().setMinSeverity(Severity::Remark);
    ctx.diag().setOutput(&m_stream);

    auto loc_a = ctx.loc_from_line(src_a, 1);
    auto loc_b = ctx.loc_from_line(src_b, 1);

    ctx.diag().report(loc_a, Severity::Error, "error in a.cpp");
    ctx.diag().report(loc_b, Severity::Error, "error in b.cpp");

    std::string out = m_stream.str();
    EXPECT_NE(out.find("a.cpp:1:1"), std::string::npos) << "Output: " << out;
    EXPECT_NE(out.find("b.cpp:1:1"), std::string::npos) << "Output: " << out;
    EXPECT_EQ(ctx.diag().errorCount(), 2);
}

TEST_F(DiagFixture, CaretAtCorrectColumn) {
    std::string source = "    int x = foo();\n";
    Context ctx;
    SourceIdx src = ctx.add_source("caret.cpp", source);
    ctx.diag().setMinSeverity(Severity::Remark);
    ctx.diag().setOutput(&m_stream);

    auto foo_pos = source.find("foo");
    auto foo_begin = SourceLoc::make(src, static_cast<int>(foo_pos) + 1);
    auto foo_end = SourceLoc::make(src, static_cast<int>(foo_pos) + 4);

    ctx.diag().report({foo_begin, foo_end}, Severity::Warning, "unused variable {}", "foo");

    auto out = m_stream.str();
    EXPECT_NE(out.find("caret.cpp:1:13:"), std::string::npos)
        << "Wrong line/column: " << out;
    EXPECT_NE(out.find("            ^~~"), std::string::npos)
        << "Caret should point at column 13 with 3-char underline: " << out;
}

TEST_F(DiagFixture, CaretAtColumnOne) {
    std::string source = "int x;\n";
    Context ctx;
    SourceIdx src = ctx.add_source("col1.cpp", source);
    ctx.diag().setMinSeverity(Severity::Remark);
    ctx.diag().setOutput(&m_stream);

    auto loc = ctx.loc_from_line(src, 1);
    auto end = SourceLoc::make(src, loc.offset() + 3);

    ctx.diag().report({loc, end}, Severity::Error, "unexpected 'int'");

    auto out = m_stream.str();
    EXPECT_NE(out.find("col1.cpp:1:1:"), std::string::npos)
        << "Wrong line/column: " << out;
    EXPECT_NE(out.find("^~~"), std::string::npos)
        << "Caret at column 1 should have 3-char underline: " << out;
}

TEST_F(DiagFixture, MultiLineRangeCaretAtBegin) {
    std::string source = "int x = 1 +\n"
                         "    2 * 3;\n";
    Context ctx;
    SourceIdx src = ctx.add_source("multi.cpp", source);
    ctx.diag().setMinSeverity(Severity::Remark);
    ctx.diag().setOutput(&m_stream);

    auto loc1 = ctx.loc_from_line(src, 1);
    auto loc2 = ctx.loc_from_line(src, 2);
    auto end = SourceLoc::make(src, loc2.offset() + 7);

    ctx.diag().report({loc1, end}, Severity::Error, "expression too long");

    auto out = m_stream.str();
    EXPECT_NE(out.find("multi.cpp:1:1:"), std::string::npos)
        << "Wrong line/column: " << out;
    EXPECT_NE(out.find("^"), std::string::npos)
        << "Caret should be present: " << out;
}

TEST_F(DiagFixture, SourceIdx_TypeSafety) {
    SourceIdx idx = SourceIdx::make(0);
    EXPECT_TRUE(idx.isValid());

    SourceIdx invalid = SourceIdx::none();
    EXPECT_FALSE(invalid.isValid());
}

TEST_F(DiagFixture, SourceCount) {
    Context ctx;
    EXPECT_EQ(ctx.file_count(), 0);
    ctx.add_source("a.cpp", "");
    EXPECT_EQ(ctx.file_count(), 1);
    ctx.add_source("b.cpp", "");
    EXPECT_EQ(ctx.file_count(), 2);
}