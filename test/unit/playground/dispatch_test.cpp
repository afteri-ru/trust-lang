// -----------------------------------------------------------------------
// Unit tests for trust-playground balancer dispatch (playground/server.h).
// -----------------------------------------------------------------------

#include "playground/server.h"
#include "playground/config.h"
#include "trust/version.h"

#include <gtest/gtest.h>

#include <nlohmann/json.hpp>

#include <atomic>
#include <chrono>
#include <string>
#include <thread>

namespace {

using trust::playground::HttpRequest;
using trust::playground::HttpResponse;
using trust::playground::PlaygroundConfig;
using trust::playground::PlaygroundServer;

constexpr const char* kToken = "aaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaa";

PlaygroundConfig makeConfig(int job_timeout, int poll_timeout) {
    PlaygroundConfig cfg;
    cfg.listen = "127.0.0.1";
    cfg.port = 1;
    cfg.jobTimeoutSec = job_timeout;
    cfg.pollTimeoutSec = poll_timeout;
    cfg.rateLimitPerIp = 1000;
    cfg.bodyLimitKb = 256;
    cfg.workers.push_back({"test-worker", kToken});
    return cfg;
}

HttpRequest makeReq(const std::string& method, const std::string& target, const std::string& body) {
    HttpRequest req;
    req.method = method;
    req.target = target;
    req.body = body;
    return req;
}

TEST(ServerTest, RunWithNoWorkersReturnsUnavailable) {
    PlaygroundConfig cfg = makeConfig(2, 2);
    cfg.workers.clear(); // пустой реестр — воркеров нет
    PlaygroundServer server(cfg);
    const HttpResponse resp = server.handle(makeReq("POST", "/run", "print 1"), "10.0.0.1");
    EXPECT_EQ(resp.status, 503);
    const auto j = nlohmann::json::parse(resp.body);
    EXPECT_EQ(j["unavailable"], true);
    EXPECT_EQ(j["ok"], false);
    EXPECT_FALSE(j["instructionsUrl"].get<std::string>().empty());
}

TEST(ServerTest, PollBadTokenForbidden) {
    PlaygroundServer server(makeConfig(2, 2));
    const std::string body = "{\"token\":\"bad\",\"capacity\":1}";
    const HttpResponse resp = server.handle(makeReq("POST", "/poll", body), "10.0.0.2");
    EXPECT_EQ(resp.status, 403);
}

TEST(ServerTest, HealthReturnsOk) {
    PlaygroundServer server(makeConfig(2, 2));
    const HttpResponse resp = server.handle(makeReq("GET", "/health", ""), "10.0.0.3");
    EXPECT_EQ(resp.status, 200);
    const auto j = nlohmann::json::parse(resp.body);
    EXPECT_EQ(j["status"], "ok");
    // Публичный пинг готовности: число «живых» воркеров (здесь воркеров нет → 0).
    EXPECT_EQ(j["workers_connected"], 0);
}

// Полный цикл: воркер регистрируется через /poll, забирает задачу из /run,
// возвращает результат через /result; /run отдаёт его сайту.
TEST(ServerTest, FullDispatchPollRunResult) {
    PlaygroundServer server(makeConfig(5, 5));
    const std::string result_json = "{\"ok\":true,\"source\":\"print 42\",\"cpp\":\"int x;\"}";

    std::thread worker([&] {
        const std::string poll_body = std::string("{\"token\":\"") + kToken + "\",\"capacity\":1}";
        const HttpResponse poll_resp = server.handle(makeReq("POST", "/poll", poll_body), "10.0.0.4");
        EXPECT_EQ(poll_resp.status, 200);
        const auto job = nlohmann::json::parse(poll_resp.body);
        EXPECT_EQ(job["code"], "print 42");
        const auto res_body = nlohmann::json{{"token", kToken}, {"jobId", job["jobId"]}, {"result", result_json}};
        server.handle(makeReq("POST", "/result", res_body.dump()), "10.0.0.4");
    });

    // Даём воркеру зарегистрироваться в реестре.
    std::this_thread::sleep_for(std::chrono::milliseconds(50));

    const HttpResponse run_resp = server.handle(makeReq("POST", "/run", "print 42"), "10.0.0.5");
    worker.join();

    EXPECT_EQ(run_resp.status, 200);
    // handleRun пере-сериализует результат (порядок ключей может отличаться);
    // сравниваем семантически по полям.
    const auto run_j = nlohmann::json::parse(run_resp.body);
    EXPECT_EQ(run_j["ok"], true);
    EXPECT_EQ(run_j["source"], "print 42");
    EXPECT_EQ(run_j["cpp"], "int x;");
}

// Ленивое скачивание build-архива: POST /download — отдельный запрос, заново
// обрабатывает файл и сразу отдаёт gzip-архив (без кеша на балансировщике).
// Воркер получает задачу с buildArchive=true и возвращает архив через /result.
TEST(ServerTest, LazyDownloadBuildsArchive) {
    PlaygroundServer server(makeConfig(5, 5));
    // Имя файла формируется из актуальной версии (TRUST_VERSION), не хардкодится.
    const std::string expected_name = std::string("trust-lang-") + TRUST_VERSION_FULL + "-generated.tar.gz";
    const std::string archive_json = "{\"ok\":true,\"archive\":\"aGVsbG8=\",\"archiveName\":\"" + expected_name + "\"}"; // archive = "hello"

    std::thread worker([&] {
        const std::string poll_body = std::string("{\"token\":\"") + kToken + "\",\"capacity\":1}";
        const HttpResponse poll_resp = server.handle(makeReq("POST", "/poll", poll_body), "10.0.0.4");
        EXPECT_EQ(poll_resp.status, 200);
        const auto job = nlohmann::json::parse(poll_resp.body);
        EXPECT_EQ(job["code"], "print 42");
        EXPECT_EQ(job.value("buildArchive", false), true);
        const auto res_body = nlohmann::json{{"token", kToken}, {"jobId", job["jobId"]}, {"result", archive_json}};
        server.handle(makeReq("POST", "/result", res_body.dump()), "10.0.0.4");
    });

    std::this_thread::sleep_for(std::chrono::milliseconds(50));
    const HttpResponse dl = server.handle(makeReq("POST", "/download", "print 42"), "10.0.0.5");
    worker.join();

    ASSERT_EQ(dl.status, 200);
    EXPECT_EQ(dl.content_type, "application/gzip");
    EXPECT_EQ(dl.content_disposition, expected_name);
    EXPECT_EQ(dl.body, "hello"); // декодированный base64
}

// /run НЕ строит build-архив и не возвращает buildArchiveUrl/archive — он собирается
// только по отдельному POST /download (лениво).
TEST(ServerTest, RunDoesNotBuildArchive) {
    PlaygroundServer server(makeConfig(5, 5));
    const std::string result_json = "{\"ok\":true,\"source\":\"print 42\",\"cpp\":\"int x;\"}";

    std::thread worker([&] {
        const std::string poll_body = std::string("{\"token\":\"") + kToken + "\",\"capacity\":1}";
        const HttpResponse poll_resp = server.handle(makeReq("POST", "/poll", poll_body), "10.0.0.4");
        EXPECT_EQ(poll_resp.status, 200);
        const auto job = nlohmann::json::parse(poll_resp.body);
        EXPECT_EQ(job.value("buildArchive", false), false);
        const auto res_body = nlohmann::json{{"token", kToken}, {"jobId", job["jobId"]}, {"result", result_json}};
        server.handle(makeReq("POST", "/result", res_body.dump()), "10.0.0.4");
    });

    std::this_thread::sleep_for(std::chrono::milliseconds(50));
    const HttpResponse run_resp = server.handle(makeReq("POST", "/run", "print 42"), "10.0.0.5");
    worker.join();

    ASSERT_EQ(run_resp.status, 200);
    const auto j = nlohmann::json::parse(run_resp.body);
    EXPECT_FALSE(j.contains("buildArchiveUrl"));
    EXPECT_FALSE(j.contains("archive"));
}

} // namespace

// CORS включается ТОЛЬКО на публичных браузерных эндпоинтах; токен-защищённые и
// воркерские ответы НЕ шлют `Access-Control-Allow-Origin: *` (чтобы не разрешать
// чтение с любого origin).
TEST(ServerTest, CursorOptInOnlyOnBrowserEndpoints) {
    PlaygroundConfig cfg = makeConfig(5, 5);
    cfg.statsToken = "stok";
    PlaygroundServer server(cfg);

    // /health — браузерный (мониторинг со страницы) → CORS есть.
    {
        const HttpResponse r = server.handle(makeReq("GET", "/health", ""), "10.0.0.1");
        EXPECT_NE(trust::playground::serializeHttpResponse(r).find("Access-Control-Allow-Origin: *"), std::string::npos);
    }
    // /run без воркеров — браузерный (503 читается glue-JS) → CORS есть.
    {
        const HttpResponse r = server.handle(makeReq("POST", "/run", "print 1"), "10.0.0.1");
        EXPECT_NE(trust::playground::serializeHttpResponse(r).find("Access-Control-Allow-Origin: *"), std::string::npos);
    }
    // /download (пустое тело → 400) — браузерный → CORS есть.
    {
        const HttpResponse r = server.handle(makeReq("POST", "/download", ""), "10.0.0.1");
        EXPECT_NE(trust::playground::serializeHttpResponse(r).find("Access-Control-Allow-Origin: *"), std::string::npos);
    }
    // /stats (токен-защищённый) — НЕ должен слать CORS: *.
    {
        const HttpResponse r = server.handle(makeReq("GET", "/stats?token=stok", ""), "10.0.0.1");
        EXPECT_EQ(trust::playground::serializeHttpResponse(r).find("Access-Control-Allow-Origin"), std::string::npos);
    }
    // /poll (воркер, неверный токен → 403) — НЕ должен слать CORS: *.
    {
        const HttpResponse r = server.handle(makeReq("POST", "/poll", "{\"token\":\"bad\",\"capacity\":1}"), "10.0.0.2");
        EXPECT_EQ(trust::playground::serializeHttpResponse(r).find("Access-Control-Allow-Origin"), std::string::npos);
    }
}

// /stats отдаёт JSON; /stats?format=html — HTML-страницу той же статистики (по токену).
TEST(ServerTest, StatsJsonAndHtmlByToken) {
    PlaygroundConfig cfg = makeConfig(5, 5);
    cfg.statsToken = "stok";
    PlaygroundServer server(cfg);

    // Без токена — 403.
    EXPECT_EQ(server.handle(makeReq("GET", "/stats", ""), "10.0.0.1").status, 403);

    // JSON.
    const HttpResponse jr = server.handle(makeReq("GET", "/stats?token=stok", ""), "10.0.0.1");
    EXPECT_EQ(jr.status, 200);
    EXPECT_NE(jr.content_type.find("application/json"), std::string::npos);
    EXPECT_NE(jr.body.find("\"balancer\""), std::string::npos);

    // HTML.
    const HttpResponse hr = server.handle(makeReq("GET", "/stats?token=stok&format=html", ""), "10.0.0.1");
    EXPECT_EQ(hr.status, 200);
    EXPECT_NE(hr.content_type.find("text/html"), std::string::npos);
    EXPECT_NE(hr.body.find("trust-playground statistics"), std::string::npos);
    EXPECT_NE(hr.body.find("<html"), std::string::npos);
    EXPECT_NE(hr.body.find("Workers"), std::string::npos);
}
