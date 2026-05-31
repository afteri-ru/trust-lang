// -----------------------------------------------------------------------
// Unit tests for DapHandler
// -----------------------------------------------------------------------

#include "debug/dap_handler.hpp"
#include "debug/dap_transport.h"
#include "debug/gdb_debug.h"
#include "diag/mapper.hpp"

#include "mock_transport.hpp"

#include <gmock/gmock.h>
#include <gtest/gtest.h>

#include <nlohmann/json.hpp>

#include <filesystem>
#include <string>

using json = nlohmann::json;

namespace {

TEST(DapHandlerTest, IsTrustFileExt_TrueForSrc) {
    EXPECT_TRUE(trust::SourceMapReader::isTrustFileExt("/path/to/file.src"));
    EXPECT_TRUE(trust::SourceMapReader::isTrustFileExt("file.src"));
}

TEST(DapHandlerTest, IsTrustFileExt_FalseForTrust) {
    // .trust — бинарный скомпилированный модуль, не исходный файл
    EXPECT_FALSE(trust::SourceMapReader::isTrustFileExt("/path/to/file.trust"));
    EXPECT_FALSE(trust::SourceMapReader::isTrustFileExt("file.trust"));
}

TEST(DapHandlerTest, IsTrustFileExt_FalseForCpp) {
    EXPECT_FALSE(trust::SourceMapReader::isTrustFileExt("file.cpp"));
    EXPECT_FALSE(trust::SourceMapReader::isTrustFileExt("file.cppt"));
    EXPECT_FALSE(trust::SourceMapReader::isTrustFileExt("file.hppt"));
    EXPECT_FALSE(trust::SourceMapReader::isTrustFileExt("file.h"));
    EXPECT_FALSE(trust::SourceMapReader::isTrustFileExt(""));
}

TEST(DapHandlerTest, HandleInitialize_ReturnsCapabilities) {
    MockTransport mock;
    DapOptions opts;
    DapHandler handler(mock, opts);

    json req = {{"type", "request"}, {"command", "initialize"}, {"seq", 1}};
    handler.handleInitialize(req);

    ASSERT_FALSE(mock.capturedOutput.empty());
    json resp = json::parse(mock.capturedOutput);

    EXPECT_EQ(resp["type"], "response");
    EXPECT_EQ(resp["request_seq"], 1);
    EXPECT_TRUE(resp["success"].get<bool>());
    EXPECT_TRUE(resp["body"]["supportsConfigurationDoneRequest"].get<bool>());
    EXPECT_TRUE(resp["body"]["supportsFunctionBreakpoints"].get<bool>());
}

TEST(DapHandlerTest, HandleLaunch_NoTargetFile) {
    MockTransport mock;
    DapOptions opts;
    DapHandler handler(mock, opts);

    json req = {{"type", "request"}, {"command", "launch"}, {"seq", 2}, {"arguments", json::object()}};
    handler.handleLaunch(req);

    ASSERT_FALSE(mock.capturedOutput.empty());
    json resp = json::parse(mock.capturedOutput);
    EXPECT_FALSE(resp["success"].get<bool>());
    EXPECT_EQ(resp["message"], "No target file specified");
}

TEST(DapHandlerTest, HandleLaunch_TargetNotFound) {
    MockTransport mock;
    DapOptions opts;
    DapHandler handler(mock, opts);

    json req = {{"type", "request"}, {"command", "launch"}, {"seq", 3}, {"arguments", {{"targetFile", "/nonexistent/binary"}}}};
    handler.handleLaunch(req);

    ASSERT_FALSE(mock.capturedOutput.empty());
    json resp = json::parse(mock.capturedOutput);
    EXPECT_FALSE(resp["success"].get<bool>());
    EXPECT_NE(resp["message"].get<std::string>().find("not found"), std::string::npos);
}

TEST(DapHandlerTest, HandleRequest_UnknownCommand) {
    MockTransport mock;
    DapOptions opts;
    DapHandler handler(mock, opts);

    json req = {{"type", "request"}, {"command", "foobar"}, {"seq", 99}};
    handler.handleRequest(req);

    ASSERT_FALSE(mock.capturedOutput.empty());
    json resp = json::parse(mock.capturedOutput);
    EXPECT_FALSE(resp["success"].get<bool>());
    EXPECT_EQ(resp["message"].get<std::string>().find("unsupported"), 0);
}

TEST(DapHandlerTest, HandleRequest_Continue) {
    MockTransport mock;
    DapOptions opts;
    DapHandler handler(mock, opts);

    json req = {{"type", "request"}, {"command", "continue"}, {"seq", 10}};
    handler.handleRequest(req);

    ASSERT_FALSE(mock.capturedOutput.empty());
    json resp = json::parse(mock.capturedOutput);
    EXPECT_TRUE(resp["success"].get<bool>());
    EXPECT_TRUE(resp["body"]["allThreadsContinued"].get<bool>());
}

TEST(DapHandlerTest, HandleRequest_ConfigurationDone) {
    MockTransport mock;
    DapOptions opts;
    DapHandler handler(mock, opts);

    json req = {{"type", "request"}, {"command", "configurationDone"}, {"seq", 11}};
    handler.handleRequest(req);

    ASSERT_FALSE(mock.capturedOutput.empty());
    json resp = json::parse(mock.capturedOutput);
    EXPECT_TRUE(resp["success"].get<bool>());
}
} // namespace