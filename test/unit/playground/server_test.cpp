// -----------------------------------------------------------------------
// Unit tests for trust-playground HTTP server core (playground/http.h).
// -----------------------------------------------------------------------

#include "playground/http.h"

#include <gtest/gtest.h>

#include <nlohmann/json.hpp>

#include <string>

using json = nlohmann::json;

namespace {

class PlaygroundServerTest : public ::testing::Test {
  protected:
    LspOptions opts;
};

constexpr const char* kHelloSrc = "@main():={\n"
                                  "    msg := \"hello playground\";\n"
                                  "    print(msg);\n"
                                  "}\n";

std::string postRunRequest(const std::string& body) {
    return "POST /run HTTP/1.1\r\n"
           "Host: localhost\r\n"
           "Content-Type: text/plain\r\n"
           "Content-Length: " +
           std::to_string(body.size()) +
           "\r\n"
           "\r\n" +
           body;
}

TEST_F(PlaygroundServerTest, ParseHttpRequest_ValidPost) {
    const std::string raw = postRunRequest("abc123");
    trust::playground::HttpRequest req;
    ASSERT_TRUE(trust::playground::parseHttpRequest(raw, req));
    EXPECT_EQ(req.method, "POST");
    EXPECT_EQ(req.target, "/run");
    EXPECT_EQ(req.body, "abc123");
}

TEST_F(PlaygroundServerTest, ParseHttpRequest_EmptyReturnsFalse) {
    trust::playground::HttpRequest req;
    EXPECT_FALSE(trust::playground::parseHttpRequest("", req));
}

TEST_F(PlaygroundServerTest, ParseHttpRequest_BadRequestLineReturnsFalse) {
    trust::playground::HttpRequest req;
    EXPECT_FALSE(trust::playground::parseHttpRequest("POST\r\n\r\n", req));
}

TEST_F(PlaygroundServerTest, HandleHttpRequest_HealthReturnsOk) {
    trust::playground::HttpRequest req;
    req.method = "GET";
    req.target = "/health";
    auto resp = trust::playground::handleHttpRequest(req, opts);
    EXPECT_EQ(resp.status, 200);
    json j = json::parse(resp.body);
    EXPECT_EQ(j["status"], "ok");
}

TEST_F(PlaygroundServerTest, HandleHttpRequest_PostRunReturnsJsonContract) {
    trust::playground::HttpRequest req;
    req.method = "POST";
    req.target = "/run";
    req.body = kHelloSrc;
    auto resp = trust::playground::handleHttpRequest(req, opts);
    EXPECT_EQ(resp.status, 200);
    json j = json::parse(resp.body);
    EXPECT_EQ(j["ok"], true);
    EXPECT_EQ(j["source"], kHelloSrc);
    EXPECT_FALSE(j["cpp"].get<std::string>().empty());
    EXPECT_TRUE(j.contains("trustToCpp"));
    EXPECT_TRUE(j.contains("cppToTrust"));
}

TEST_F(PlaygroundServerTest, HandleHttpRequest_PostEmptyBodyReturns400) {
    trust::playground::HttpRequest req;
    req.method = "POST";
    req.target = "/run";
    req.body = "";
    auto resp = trust::playground::handleHttpRequest(req, opts);
    EXPECT_EQ(resp.status, 400);
}

TEST_F(PlaygroundServerTest, HandleHttpRequest_UnknownTargetReturns404) {
    trust::playground::HttpRequest req;
    req.method = "GET";
    req.target = "/nope";
    auto resp = trust::playground::handleHttpRequest(req, opts);
    EXPECT_EQ(resp.status, 404);
}

TEST_F(PlaygroundServerTest, HandleHttpRequest_OptionsPreflightReturns204) {
    trust::playground::HttpRequest req;
    req.method = "OPTIONS";
    req.target = "/run";
    auto resp = trust::playground::handleHttpRequest(req, opts);
    EXPECT_EQ(resp.status, 204);
}

TEST_F(PlaygroundServerTest, SerializeHttpResponse_CorsOptIn) {
    trust::playground::HttpResponse r;
    r.status = 200;
    r.content_type = "application/json; charset=utf-8";
    r.body = "{\"ok\":true}";
    // По умолчанию CORS выключен (безопасно: не раскрываем ответы чужому origin).
    const std::string out_default = trust::playground::serializeHttpResponse(r);
    EXPECT_NE(out_default.find("HTTP/1.1 200 OK"), std::string::npos);
    EXPECT_EQ(out_default.find("Access-Control-Allow-Origin"), std::string::npos);
    EXPECT_NE(out_default.find("Content-Length: 11"), std::string::npos);
    EXPECT_NE(out_default.find("\r\n\r\n{\"ok\":true}"), std::string::npos);
    // CORS добавляется только при явном r.cors = true.
    r.cors = true;
    // Без конкретного origin (corsOrigin пуст) ACAO НЕ выводим - НИКОГДА не '*'.
    const std::string out_cors = trust::playground::serializeHttpResponse(r);
    EXPECT_EQ(out_cors.find("Access-Control-Allow-Origin"), std::string::npos);
    EXPECT_NE(out_cors.find("Access-Control-Allow-Methods"), std::string::npos);
    // С конкретным origin - выводим именно его (и не '*'; Content-Disposition доступен JS).
    r.corsOrigin = "https://trust-lang.net";
    const std::string out_cors2 = trust::playground::serializeHttpResponse(r);
    EXPECT_NE(out_cors2.find("Access-Control-Allow-Origin: https://trust-lang.net"), std::string::npos);
    EXPECT_EQ(out_cors2.find("Access-Control-Allow-Origin: *"), std::string::npos);
    // Кастомные заголовки фронтенда должны быть разрешены в CORS-preflight, иначе браузер
    // отклонит OPTIONS и /run упадёт («Нет связи»), хотя /health будет «онлайн».
    EXPECT_NE(out_cors2.find("Access-Control-Allow-Headers: Content-Type, X-Example-Name, X-Stats-Token"), std::string::npos);
    EXPECT_NE(out_cors2.find("Access-Control-Expose-Headers: Content-Disposition"), std::string::npos);
}

} // namespace
