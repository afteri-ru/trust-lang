// -----------------------------------------------------------------------
// Unit tests for GDB/MI line parsing (debug/mi/mi_parse.hpp)
// -----------------------------------------------------------------------

#include "debug/mi/mi_parse.hpp"

#include <gtest/gtest.h>

#include <string_view>
#include <vector>

namespace {

using trust::debug::detail::mapStopReason;
using trust::debug::detail::miConsoleText;
using trust::debug::detail::parseMiResponseLine;
using trust::debug::detail::parseStackFrames;

TEST(MiParseTest, ParsesDone) {
    auto resp = parseMiResponseLine("^done");
    ASSERT_TRUE(resp.has_value());
    EXPECT_FALSE(resp->isAsync);
    EXPECT_EQ(resp->klass, "done");
    EXPECT_TRUE(resp->reason.empty());
}

TEST(MiParseTest, ParsesTokenPrefixedLine) {
    auto resp = parseMiResponseLine("12^running");
    ASSERT_TRUE(resp.has_value());
    EXPECT_EQ(resp->klass, "running");
    EXPECT_FALSE(resp->isAsync);
}

TEST(MiParseTest, ParsesErrorClass) {
    auto resp = parseMiResponseLine("3^error,msg=\"No symbol table\"");
    ASSERT_TRUE(resp.has_value());
    EXPECT_EQ(resp->klass, "error");
}

TEST(MiParseTest, ParsesAsyncStoppedWithReason) {
    auto resp = parseMiResponseLine("*stopped,reason=\"breakpoint-hit\",thread-id=\"1\"");
    ASSERT_TRUE(resp.has_value());
    EXPECT_TRUE(resp->isAsync);
    EXPECT_EQ(resp->klass, "stopped");
    EXPECT_EQ(resp->reason, "breakpoint-hit");
}

TEST(MiParseTest, ConsoleAndStreamRecordsAreNotResponses) {
    EXPECT_FALSE(parseMiResponseLine("~\"hello\"").has_value());
    EXPECT_FALSE(parseMiResponseLine("=thread-created,id=\"1\"").has_value());
    EXPECT_FALSE(parseMiResponseLine("&\"log\"").has_value());
    EXPECT_FALSE(parseMiResponseLine("@\"target\"").has_value());
    EXPECT_FALSE(parseMiResponseLine("12").has_value());
    EXPECT_FALSE(parseMiResponseLine("garbage").has_value());
    EXPECT_FALSE(parseMiResponseLine("").has_value());
}

TEST(MiParseTest, ConsoleTextStripsQuotes) {
    auto text = miConsoleText("~\"breakpoint set\\n\"");
    ASSERT_TRUE(text.has_value());
    EXPECT_EQ(*text, "breakpoint set\\n");
    EXPECT_FALSE(miConsoleText("^done").has_value());
    EXPECT_FALSE(miConsoleText("~no-quotes").has_value());
}

TEST(MiParseTest, ParsesStackFrames) {
    constexpr std::string_view kStackDump = "^done,stack=[frame={level=\"0\",addr=\"0x1\",func=\"main\",file=\"a.cppt\",fullname=\"/tmp/a.cppt\",line=\"42\"},"
                                            "frame={level=\"1\",addr=\"0x2\",func=\"helper\",file=\"b.cppt\",fullname=\"/tmp/b.cppt\",line=\"7\"}]";
    auto frames = parseStackFrames(kStackDump);
    ASSERT_EQ(frames.size(), 2u);
    EXPECT_EQ(frames[0].m_func, "main");
    EXPECT_EQ(frames[0].m_file, "a.cppt");
    EXPECT_EQ(frames[0].m_line, 42);
    EXPECT_EQ(frames[1].m_func, "helper");
    EXPECT_EQ(frames[1].m_file, "b.cppt");
    EXPECT_EQ(frames[1].m_line, 7);
}

TEST(MiParseTest, ParsesEmptyOrMissingStack) {
    EXPECT_TRUE(parseStackFrames("^done,stack=[]").empty());
    EXPECT_TRUE(parseStackFrames("^done").empty());
    EXPECT_TRUE(parseStackFrames("").empty());
}

TEST(MiParseTest, MapsStopReasons) {
    EXPECT_EQ(mapStopReason("breakpoint-hit"), "breakpoint");
    EXPECT_EQ(mapStopReason("watchpoint-trigger"), "breakpoint");
    EXPECT_EQ(mapStopReason("function-finished"), "breakpoint");
    EXPECT_EQ(mapStopReason("end-stepping-range"), "step");
    EXPECT_EQ(mapStopReason("step-end"), "step");
    EXPECT_EQ(mapStopReason("signal-received"), "exception");
    EXPECT_EQ(mapStopReason("exited"), "exit");
    EXPECT_EQ(mapStopReason("exited-normally"), "exit");
    EXPECT_EQ(mapStopReason("exited-signalled"), "exit");
    EXPECT_EQ(mapStopReason("unknown-reason"), "breakpoint");
}

} // namespace
