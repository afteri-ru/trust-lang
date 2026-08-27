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
#include <cstring>
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
    cfg.workers.clear(); // пустой реестр - воркеров нет
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

// Ленивое скачивание build-архива: POST /download - отдельный запрос, заново
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

// Ленивое скачивание: если воркер не собрал архив (нет поля archive), сервер
// возвращает 502 с реальной причиной из результата воркера (error/log), а не
// немой дефолт "archive not produced".
TEST(ServerTest, DownloadSurfacesWorkerErrorWhenArchiveMissing) {
    PlaygroundServer server(makeConfig(5, 5));
    const std::string worker_error = "archive not produced: trust-lsp --emit-build-dir returned no *.tar.gz (tar failed)";
    const std::string archive_json = "{\"ok\":true,\"error\":\"" + worker_error + "\"}";

    std::thread worker([&] {
        const std::string poll_body = std::string("{\"token\":\"") + kToken + "\",\"capacity\":1}";
        const HttpResponse poll_resp = server.handle(makeReq("POST", "/poll", poll_body), "10.0.0.4");
        EXPECT_EQ(poll_resp.status, 200);
        const auto job = nlohmann::json::parse(poll_resp.body);
        EXPECT_EQ(job.value("buildArchive", false), true);
        const auto res_body = nlohmann::json{{"token", kToken}, {"jobId", job["jobId"]}, {"result", archive_json}};
        server.handle(makeReq("POST", "/result", res_body.dump()), "10.0.0.4");
    });

    std::this_thread::sleep_for(std::chrono::milliseconds(50));
    const HttpResponse dl = server.handle(makeReq("POST", "/download", "print 42"), "10.0.0.5");
    worker.join();

    ASSERT_EQ(dl.status, 502);
    const auto j = nlohmann::json::parse(dl.body);
    ASSERT_TRUE(j.contains("error"));
    const std::string err = j["error"].get<std::string>();
    EXPECT_NE(err.find("archive not produced"), std::string::npos);
    EXPECT_NE(err.find("tar failed"), std::string::npos);
}

// /run НЕ строит build-архив и не возвращает buildArchiveUrl/archive - он собирается
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

// CORS: ACAO выводится ТОЛЬКО на публичных браузерных эндпоинтах и ТОЛЬКО конкретным
// origin (из allowlist или loopback); НИКОГДА не '*'. Без origin (curl/скрипты) - ACAO нет.
// Токен-защищённые/воркерские ответы НЕ шлют Access-Control-Allow-Origin.
TEST(ServerTest, CursorOptInOnlyOnBrowserEndpoints) {
    PlaygroundConfig cfg = makeConfig(5, 5);
    cfg.statsToken = "stok";
    PlaygroundServer server(cfg);

    // /health с loopback-origin - браузерный → ACAO = конкретный origin (не '*').
    {
        HttpRequest rq = makeReq("GET", "/health", "");
        rq.origin = "http://localhost";
        const HttpResponse r = server.handle(rq, "10.0.0.1");
        EXPECT_NE(trust::playground::serializeHttpResponse(r).find("Access-Control-Allow-Origin: http://localhost"), std::string::npos);
    }
    // /health с посторонним origin (allowed_origins не заданы) - 403 (доменная привязка).
    {
        HttpRequest rq = makeReq("GET", "/health", "");
        rq.origin = "https://evil.example.com";
        EXPECT_EQ(server.handle(rq, "10.0.0.1").status, 403);
    }
    // /run без origin (curl/скрипт) - ACAO НЕ выводим (fail-closed, без '*') - 503, но без CORS.
    {
        const HttpResponse r = server.handle(makeReq("POST", "/run", "print 1"), "10.0.0.1");
        EXPECT_EQ(trust::playground::serializeHttpResponse(r).find("Access-Control-Allow-Origin"), std::string::npos);
    }
    // /run с посторонним origin (allowed_origins не заданы) - 403 (fail-closed).
    {
        HttpRequest rq = makeReq("POST", "/run", "print 1");
        rq.origin = "https://evil.example.com";
        EXPECT_EQ(server.handle(rq, "10.0.0.1").status, 403);
    }
    // /download (пустое тело → 400) без origin - ACAO нет.
    {
        const HttpResponse r = server.handle(makeReq("POST", "/download", ""), "10.0.0.1");
        EXPECT_EQ(trust::playground::serializeHttpResponse(r).find("Access-Control-Allow-Origin"), std::string::npos);
    }
    // /stats (токен-защищённый) - НЕ должен слать CORS: *.
    {
        HttpRequest rq = makeReq("GET", "/stats", "");
        rq.statsTokenHdr = "stok";
        const HttpResponse r = server.handle(rq, "10.0.0.1");
        EXPECT_EQ(trust::playground::serializeHttpResponse(r).find("Access-Control-Allow-Origin"), std::string::npos);
    }
    // /poll (воркер, неверный токен → 403) - НЕ должен слать CORS: *.
    {
        const HttpResponse r = server.handle(makeReq("POST", "/poll", "{\"token\":\"bad\",\"capacity\":1}"), "10.0.0.2");
        EXPECT_EQ(trust::playground::serializeHttpResponse(r).find("Access-Control-Allow-Origin"), std::string::npos);
    }
}

// /stats отдаёт JSON/HTML по заголовку X-Stats-Token (query-строка ?token= больше не
// поддерживается - токен не должен светиться в URL/логах).
TEST(ServerTest, StatsJsonAndHtmlByToken) {
    PlaygroundConfig cfg = makeConfig(5, 5);
    cfg.statsToken = "stok";
    PlaygroundServer server(cfg);

    // Без токена и без сессии - 403.
    EXPECT_EQ(server.handle(makeReq("GET", "/stats", ""), "10.0.0.1").status, 403);

    // JSON по заголовку X-Stats-Token.
    HttpRequest jrq = makeReq("GET", "/stats", "");
    jrq.statsTokenHdr = "stok";
    const HttpResponse jr = server.handle(jrq, "10.0.0.1");
    EXPECT_EQ(jr.status, 200);
    EXPECT_NE(jr.content_type.find("application/json"), std::string::npos);
    EXPECT_NE(jr.body.find("\"balancer\""), std::string::npos);
    // Метрики соединений (раздельные пулы): текущее + пик утилизации присутствуют в статистике.
    EXPECT_NE(jr.body.find("\"conns_used\""), std::string::npos);
    EXPECT_NE(jr.body.find("\"client_conns_peak\""), std::string::npos);
    EXPECT_NE(jr.body.find("\"worker_conns_peak_pct\""), std::string::npos);

    // HTML по заголовку X-Stats-Token.
    HttpRequest hrq = makeReq("GET", "/stats?format=html", "");
    hrq.statsTokenHdr = "stok";
    const HttpResponse hr = server.handle(hrq, "10.0.0.1");
    EXPECT_EQ(hr.status, 200);
    EXPECT_NE(hr.content_type.find("text/html"), std::string::npos);
    EXPECT_NE(hr.body.find("trust-playground statistics"), std::string::npos);
    EXPECT_NE(hr.body.find("<html"), std::string::npos);
    EXPECT_NE(hr.body.find("Workers"), std::string::npos);
    EXPECT_NE(hr.body.find("/stats/logout"), std::string::npos);
}

// /stats/login + cookie-сессия: неверный токен -> 403, верный -> 302 + Set-Cookie;
// по cookie (/stats?format=html) доступ открывается; /stats/logout инвалидирует сессию.
TEST(ServerTest, StatsSessionLoginLogoutByCookie) {
    PlaygroundConfig cfg = makeConfig(5, 5);
    cfg.statsToken = "stok";
    PlaygroundServer server(cfg);

    // GET /stats/login - форма.
    {
        const HttpResponse r = server.handle(makeReq("GET", "/stats/login", ""), "10.0.0.1");
        EXPECT_EQ(r.status, 200);
        EXPECT_NE(r.content_type.find("text/html"), std::string::npos);
    }
    // POST /stats/login с неверным токеном -> 403.
    {
        const HttpResponse r = server.handle(makeReq("POST", "/stats/login", "bad"), "10.0.0.1");
        EXPECT_EQ(r.status, 403);
    }
    // POST /stats/login с верным токеном -> 302 + Set-Cookie.
    HttpRequest lrq = makeReq("POST", "/stats/login", "stok");
    const HttpResponse lr = server.handle(lrq, "10.0.0.1");
    EXPECT_EQ(lr.status, 302);
    // Форма (application/x-www-form-urlencoded): тело "token=stok" -> 302 (реальный ввод
    // с формы /stats/login; раньше трактовалось как строка "token=stok" и токен не совпадал).
    {
        HttpRequest frq = makeReq("POST", "/stats/login", "token=stok");
        EXPECT_EQ(server.handle(frq, "10.0.0.1").status, 302);
    }
    EXPECT_FALSE(lr.location.empty()) << "no Location header";
    // Извлекаем session_id из Set-Cookie.
    std::string cookie;
    for (const std::string& h : lr.extraHeaders) {
        if (h.rfind("Set-Cookie: tpg_stats=", 0) == 0) {
            cookie = h.substr(std::strlen("Set-Cookie: tpg_stats="));
            const size_t semi = cookie.find(';');
            if (semi != std::string::npos) {
                cookie = cookie.substr(0, semi);
            }
        }
    }
    EXPECT_FALSE(cookie.empty());

    // Без cookie - 403.
    EXPECT_EQ(server.handle(makeReq("GET", "/stats?format=html", ""), "10.0.0.1").status, 403);
    // С cookie - 200.
    HttpRequest crq = makeReq("GET", "/stats?format=html", "");
    crq.cookie = "tpg_stats=" + cookie;
    EXPECT_EQ(server.handle(crq, "10.0.0.1").status, 200);

    // Логаут инвалидирует сессию: после POST /stats/logout cookie больше не работает.
    HttpRequest outq = makeReq("POST", "/stats/logout", "");
    outq.cookie = "tpg_stats=" + cookie;
    EXPECT_EQ(server.handle(outq, "10.0.0.1").status, 302);
    EXPECT_EQ(server.handle(crq, "10.0.0.1").status, 403);
}
