#include "utils/io.hpp"
#include "diag/context.hpp"
#include "diag/diag.hpp"
#include "diag/protocol.hpp"

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
    DiagFixture()
    : m_source(generate_source(50))
    , m_src(m_ctx.source().add_source("test.cpp", m_source)) {}

    void SetUp() override {
        m_stream.str("");
        m_ctx.diag().clear();
        m_ctx.diag().setMinSeverity(Severity::Remark);
        m_prev_err = setErrs(&m_stream);
    }

    void TearDown() override { setErrs(m_prev_err); }

    std::ostream* m_prev_err = nullptr;
    std::stringstream m_stream;
    std::string m_source;
    Context m_ctx;
    MapperFile m_src{};

    std::string output() { return m_stream.str(); }
};

TEST_F(DiagFixture, ErrorWithLocation) {
    auto loc = m_ctx.source().loc_from_line(m_src, 10);
    m_ctx.diag().report(Severity::Error, loc, "unexpected token '{}'", "foo");

    std::string out = output();
    EXPECT_NE(out.find("test.cpp:10:1: error: unexpected token 'foo'"), std::string::npos) << "Output: " << out;
    EXPECT_EQ(m_ctx.diag().errorCount(), 1);
}

TEST_F(DiagFixture, WarningWithLocation) {
    auto loc = m_ctx.source().loc_from_line(m_src, 42);
    m_ctx.diag().report(Severity::Warning, loc, "deprecated function '{}'", "old_func");

    std::string out = output();
    EXPECT_NE(out.find("test.cpp:42:1: warning: deprecated function 'old_func'"), std::string::npos) << "Output: " << out;
    EXPECT_EQ(m_ctx.diag().warningCount(), 1);
}

TEST_F(DiagFixture, NoteWithLocation) {
    auto loc = m_ctx.source().loc_from_line(m_src, 5);
    m_ctx.diag().report(Severity::Note, loc, "did you mean '{}'?", "bar");

    std::string out = output();
    EXPECT_NE(out.find("test.cpp:5:1: note: did you mean 'bar'?"), std::string::npos) << "Output: " << out;
}

TEST_F(DiagFixture, ErrorWithoutLocation) {
    MapperRange invalidRange;
    m_ctx.diag().report(Severity::Error, invalidRange, "internal error");

    std::string out = output();
    EXPECT_NE(out.find("error: internal error"), std::string::npos) << "Output: " << out;
    EXPECT_TRUE(invalidRange.begin.isInvalid());
}

TEST_F(DiagFixture, SeverityFiltering) {
    m_ctx.diag().setMinSeverity(Severity::Warning);
    m_ctx.diag().clear();
    m_stream.str("");

    auto loc = m_ctx.source().loc_from_line(m_src, 1);

    m_ctx.diag().report(Severity::Remark, loc, "optimization hint");
    m_ctx.diag().report(Severity::Note, loc, "additional info");

    std::string out = output();
    EXPECT_TRUE(out.find("remark") == std::string::npos) << "Remark should be filtered";

    m_ctx.diag().report(Severity::Warning, loc, "test warning");
    m_ctx.diag().report(Severity::Error, loc, "test error");

    out = output();
    EXPECT_NE(out.find("test warning"), std::string::npos) << "Warning should pass";
    EXPECT_NE(out.find("test error"), std::string::npos) << "Error should pass";
}

TEST_F(DiagFixture, ErrorCount) {
    auto loc = m_ctx.source().loc_from_line(m_src, 1);
    m_ctx.diag().report(Severity::Error, loc, "error 1");
    m_ctx.diag().report(Severity::Error, loc, "error 2");
    m_ctx.diag().report(Severity::Warning, loc, "warning 1");

    EXPECT_EQ(m_ctx.diag().errorCount(), 2);
    EXPECT_EQ(m_ctx.diag().warningCount(), 1);
}

TEST_F(DiagFixture, ClearResetsCounts) {
    auto loc = m_ctx.source().loc_from_line(m_src, 1);
    m_ctx.diag().report(Severity::Error, loc, "error");
    m_ctx.diag().report(Severity::Warning, loc, "warning");
    EXPECT_EQ(m_ctx.diag().errorCount(), 1);
    EXPECT_EQ(m_ctx.diag().warningCount(), 1);

    m_ctx.diag().clear();
    EXPECT_EQ(m_ctx.diag().errorCount(), 0);
    EXPECT_EQ(m_ctx.diag().warningCount(), 0);
}

TEST_F(DiagFixture, FormatString) {
    auto loc = m_ctx.source().loc_from_line(m_src, 1);
    m_ctx.diag().report(Severity::Error, loc, "value {}, float {:.2f}, str {}", 42, 3.14159, "hello");

    std::string out = output();
    EXPECT_NE(out.find("value 42, float 3.14, str hello"), std::string::npos) << "Output: " << out;
}

TEST_F(DiagFixture, RemarkSeverity) {
    auto loc = m_ctx.source().loc_from_line(m_src, 1);
    m_ctx.diag().report(Severity::Remark, loc, "remark message");

    std::string out = output();
    EXPECT_NE(out.find("remark: remark message"), std::string::npos) << "Output: " << out;
}

TEST_F(DiagFixture, FatalWithLocation) {
    auto loc = m_ctx.source().loc_from_line(m_src, 1);
    EXPECT_THROW(m_ctx.diag().report(Severity::Fatal, loc, "internal compiler error: {}", "stack overflow"), FatalError);

    std::string out = output();
    EXPECT_NE(out.find("test.cpp:1:1: fatal: internal compiler error: stack overflow"), std::string::npos) << "Output: " << out;
}

TEST_F(DiagFixture, FatalSeverityFiltering) {
    m_ctx.diag().setMinSeverity(Severity::Error);
    m_ctx.diag().clear();
    m_stream.str("");

    auto loc = m_ctx.source().loc_from_line(m_src, 1);
    m_ctx.diag().report(Severity::Warning, loc, "should be filtered");
    EXPECT_THROW(m_ctx.diag().report(Severity::Fatal, loc, "fatal error"), FatalError);

    std::string out = output();
    EXPECT_TRUE(out.find("warning") == std::string::npos) << "Warning should be filtered";
    EXPECT_NE(out.find("fatal: fatal error"), std::string::npos) << "Fatal should pass";
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
    std::string source = std::string("int main() {\n"
                                     "    int x = foo();\n"
                                     "    return 0;\n"
                                     "}\n");
    MapperFile src = m_ctx.source().add_source("test.cpp", source);
    m_ctx.diag().setMinSeverity(Severity::Remark);

    auto loc = m_ctx.source().loc_from_line(src, 2);
    auto new_loc = m_ctx.source().makeLoc(src, loc + 12);
    m_ctx.diag().report(Severity::Error, new_loc, "unknown identifier '{}'", "foo");

    std::string out = output();
    EXPECT_NE(out.find("test.cpp:2:13: error: unknown identifier 'foo'"), std::string::npos) << "Output: " << out;
    EXPECT_NE(out.find("    int x = foo();"), std::string::npos) << "Source line should be present";
    EXPECT_NE(out.find("            ^"), std::string::npos) << "Caret should point to column 13";
}

TEST_F(DiagFixture, CaretWithRange) {
    std::string source = std::string("int x = 42;\n"
                                     "int y = foobar();\n");
    MapperFile src = m_ctx.source().add_source("test.cpp", source);
    m_ctx.diag().setMinSeverity(Severity::Remark);

    auto begin_pos = source.find("foobar", 12);
    auto begin = m_ctx.source().makeLoc(src, static_cast<int>(begin_pos) + 1);
    auto end = m_ctx.source().makeLoc(src, static_cast<int>(begin_pos) + 1 + 6);

    m_ctx.diag().report(Severity::Error, MapperRange{begin, end}, "unknown identifier '{}'", "foobar");

    std::string out = output();
    EXPECT_NE(out.find("test.cpp:2:9: error: unknown identifier 'foobar'"), std::string::npos) << "Output: " << out;
    EXPECT_NE(out.find("int y = foobar();"), std::string::npos) << "Source line should be present";
    EXPECT_NE(out.find("        ^~~~~~"), std::string::npos) << "Caret underline should start at column 9";
}

TEST_F(DiagFixture, CaretWithoutLocation) {
    m_ctx.diag().report(Severity::Error, MapperRange{}, "internal error");

    std::string out = output();
    EXPECT_NE(out.find("error: internal error"), std::string::npos) << "Output: " << out;
    EXPECT_EQ(out.find("^\n"), std::string::npos) << "No caret for invalid location";
}

TEST_F(DiagFixture, CaretAtDifferentColumn) {
    std::string source = std::string("void test() { auto x = 1 + 2 * 3; }\n");
    MapperFile src = m_ctx.source().add_source("test.cpp", source);
    m_ctx.diag().setMinSeverity(Severity::Remark);

    auto loc = m_ctx.source().loc_from_line(src, 1);
    auto new_loc = m_ctx.source().makeLoc(src, loc + 26);

    m_ctx.diag().report(Severity::Error, new_loc, "magic number '{}'", 3);

    std::string out = output();
    EXPECT_NE(out.find("test.cpp:1:27: error: magic number '3'"), std::string::npos) << "Output: " << out;
    EXPECT_NE(out.find("                          ^"), std::string::npos) << "Caret at correct column";
}

TEST_F(DiagFixture, RangeSpanningSameLine) {
    std::string source = std::string("const char* name = \"hello world\";\n");
    MapperFile src = m_ctx.source().add_source("test.cpp", source);
    m_ctx.diag().setMinSeverity(Severity::Remark);

    auto hello_pos = source.find("hello");
    auto world_end_pos = source.find("world") + 5;
    auto begin = m_ctx.source().makeLoc(src, static_cast<int>(hello_pos) + 1);
    auto end = m_ctx.source().makeLoc(src, static_cast<int>(world_end_pos) + 1);

    m_ctx.diag().report(Severity::Warning, MapperRange{begin, end}, "string literal used");

    std::string out = output();
    EXPECT_NE(out.find("test.cpp:1:"), std::string::npos) << "Output: " << out;
    EXPECT_NE(out.find("hello world"), std::string::npos) << "Source line should be present";
}

TEST_F(DiagFixture, FirstLineWithCaret) {
    std::string source = std::string("x = 1;\n");
    MapperFile src = m_ctx.source().add_source("test.cpp", source);
    m_ctx.diag().setMinSeverity(Severity::Remark);

    auto loc = m_ctx.source().loc_from_line(src, 1);
    m_ctx.diag().report(Severity::Error, loc, "unexpected 'x'");

    std::string out = output();
    EXPECT_NE(out.find("test.cpp:1:1: error: unexpected 'x'"), std::string::npos) << "Output: " << out;
    EXPECT_NE(out.find("x = 1;"), std::string::npos);
    EXPECT_NE(out.find("^\n"), std::string::npos) << "Caret at first column";
}

TEST_F(DiagFixture, SourceRange_Point) {
    auto loc = m_ctx.source().loc_from_line(m_src, 5);
    auto rng = MapperRange::point(loc);
    EXPECT_TRUE(rng.is_point());
    m_ctx.diag().report(Severity::Error, loc, "point error");

    std::string out = output();
    EXPECT_NE(out.find("test.cpp:5:1: error: point error"), std::string::npos) << "Output: " << out;
}

TEST_F(DiagFixture, ReportWithRange) {
    std::string source = "int x = foo;\n";
    MapperFile src = m_ctx.source().add_source("test.cpp", source);
    m_ctx.diag().setMinSeverity(Severity::Remark);

    auto foo_pos = source.find("foo");
    auto begin = m_ctx.source().makeLoc(src, static_cast<int>(foo_pos) + 1);
    auto end = m_ctx.source().makeLoc(src, static_cast<int>(foo_pos) + 1 + 3);
    auto range = MapperRange{begin, end};
    m_ctx.diag().report(Severity::Warning, range, "unused variable");

    std::string out = output();
    EXPECT_NE(out.find("warning: unused variable"), std::string::npos) << "Output: " << out;
    EXPECT_NE(out.find("^~~"), std::string::npos) << "Caret underline should show range";
}

TEST_F(DiagFixture, MultiSource) {
    MapperFile src_a = m_ctx.source().add_source("a.cpp", "int x = 1;\n");
    MapperFile src_b = m_ctx.source().add_source("b.cpp", "int y = 2;\n");
    m_ctx.diag().setMinSeverity(Severity::Remark);

    auto loc_a = m_ctx.source().loc_from_line(src_a, 1);
    auto loc_b = m_ctx.source().loc_from_line(src_b, 1);

    m_ctx.diag().report(Severity::Error, loc_a, "error in a.cpp");
    m_ctx.diag().report(Severity::Error, loc_b, "error in b.cpp");

    std::string out = m_stream.str();
    EXPECT_NE(out.find("a.cpp:1:1"), std::string::npos) << "Output: " << out;
    EXPECT_NE(out.find("b.cpp:1:1"), std::string::npos) << "Output: " << out;
    EXPECT_EQ(m_ctx.diag().errorCount(), 2);
}

TEST_F(DiagFixture, CaretAtCorrectColumn) {
    std::string source = "    int x = foo();\n";
    MapperFile src = m_ctx.source().add_source("caret.cpp", source);
    m_ctx.diag().setMinSeverity(Severity::Remark);

    auto foo_pos = source.find("foo");
    auto foo_begin = m_ctx.source().makeLoc(src, static_cast<int>(foo_pos) + 1);
    auto foo_end = m_ctx.source().makeLoc(src, static_cast<int>(foo_pos) + 4);

    m_ctx.diag().report(Severity::Warning, MapperRange{foo_begin, foo_end}, "unused variable {}", "foo");

    auto out = m_stream.str();
    EXPECT_NE(out.find("caret.cpp:1:13:"), std::string::npos) << "Wrong line/column: " << out;
    EXPECT_NE(out.find("            ^~~"), std::string::npos) << "Caret should point at column 13 with 3-char underline: " << out;
}

TEST_F(DiagFixture, CaretAtColumnOne) {
    std::string source = "int x;\n";
    MapperFile src = m_ctx.source().add_source("col1.cpp", source);
    m_ctx.diag().setMinSeverity(Severity::Remark);

    auto loc = m_ctx.source().loc_from_line(src, 1);
    auto end = m_ctx.source().makeLoc(src, loc + 3);

    m_ctx.diag().report(Severity::Error, MapperRange{loc, end}, "unexpected 'int'");

    auto out = m_stream.str();
    EXPECT_NE(out.find("col1.cpp:1:1:"), std::string::npos) << "Wrong line/column: " << out;
    EXPECT_NE(out.find("^~~"), std::string::npos) << "Caret at column 1 should have 3-char underline: " << out;
}

TEST_F(DiagFixture, MultiLineRangeCaretAtBegin) {
    std::string source = "int x = 1 +\n"
                         "    2 * 3;\n";
    MapperFile src = m_ctx.source().add_source("multi.cpp", source);
    m_ctx.diag().setMinSeverity(Severity::Remark);

    auto loc1 = m_ctx.source().loc_from_line(src, 1);
    auto loc2 = m_ctx.source().loc_from_line(src, 2);
    auto end = m_ctx.source().makeLoc(src, loc2 + 7);

    m_ctx.diag().report(Severity::Error, MapperRange{loc1, end}, "expression too long");

    auto out = m_stream.str();
    EXPECT_NE(out.find("multi.cpp:1:1:"), std::string::npos) << "Wrong line/column: " << out;
    EXPECT_NE(out.find("^"), std::string::npos) << "Caret should be present: " << out;
}

// ── diag/protocol.hpp: общие конверсии в протокольные координаты (LSP/DAP) ──

TEST_F(DiagFixture, SeverityToLsp) {
    EXPECT_EQ(trust::severityToLsp(Severity::Fatal), 1);
    EXPECT_EQ(trust::severityToLsp(Severity::Error), 1);
    EXPECT_EQ(trust::severityToLsp(Severity::Warning), 2);
    EXPECT_EQ(trust::severityToLsp(Severity::Note), 3);
    EXPECT_EQ(trust::severityToLsp(Severity::Remark), 4);
}

TEST_F(DiagFixture, MapperRangeToProtocol_InvalidIsZero) {
    const auto pr = trust::mapperRangeToProtocol(m_ctx.source(), MapperRange{});
    EXPECT_EQ(pr.start.line, 0);
    EXPECT_EQ(pr.start.character, 0);
    EXPECT_EQ(pr.end.line, 0);
    EXPECT_EQ(pr.end.character, 0);
}

TEST_F(DiagFixture, MapperRangeToProtocol_Valid) {
    auto loc = m_ctx.source().loc_from_line(m_src, 10); // 1-based → 0-based line 9
    const auto pr = trust::mapperRangeToProtocol(m_ctx.source(), MapperRange{loc, loc});
    EXPECT_EQ(pr.start.line, 9);
    EXPECT_EQ(pr.end.line, 9);
    EXPECT_GE(pr.start.character, 0);
}
