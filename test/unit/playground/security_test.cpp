// -----------------------------------------------------------------------
// Unit tests for trust-playground security/robustness features:
//   SHA-256, доменная привязка (Origin/Host), кеш /run,
//   rate-limit по forwarded-IP, PoW (challenge + verify).
// -----------------------------------------------------------------------

#include "playground/http.h"
#include "playground/server.h"
#include "playground/config.h"

#include <gtest/gtest.h>
#include <nlohmann/json.hpp>

#include <chrono>
#include <string>
#include <thread>

namespace {

using trust::playground::HttpRequest;
using trust::playground::HttpResponse;
using trust::playground::PlaygroundConfig;
using trust::playground::PlaygroundServer;

constexpr const char* kToken = "aaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaa";

// Без воркеров в реестре: /run упирается в «no workers» (мгновенный 503).
PlaygroundConfig makeCfg() {
    PlaygroundConfig cfg;
    cfg.listen = "127.0.0.1";
    cfg.port = 1;
    cfg.jobTimeoutSec = 1;
    cfg.pollTimeoutSec = 1;
    cfg.rateLimitPerIp = 1000;
    cfg.bodyLimitKb = 256;
    return cfg;
}

HttpRequest makeReq(const std::string& m, const std::string& t, const std::string& b) {
    HttpRequest r;
    r.method = m;
    r.target = t;
    r.body = b;
    return r;
}

// Ищет solution такой, что sha256(nonce+solution) начинается с n нулевых бит.
std::string solvePow(const std::string& nonce, int difficulty) {
    static const int kLz[16] = {4, 3, 2, 2, 1, 1, 1, 1, 0, 0, 0, 0, 0, 0, 0, 0};
    for (long long i = 0; i < 20000000; ++i) {
        const std::string sol = std::to_string(i);
        const std::string hash = trust::playground::sha256Hex(nonce + sol);
        int zeros = 0;
        for (const char c : hash) {
            const int v = (c >= '0' && c <= '9') ? (c - '0') : (c - 'a' + 10);
            const int n = kLz[v & 0xf];
            if (zeros + n >= difficulty) {
                return sol;
            }
            if (n == 0) {
                break;
            }
            zeros += n;
        }
    }
    return std::string();
}

TEST(HashTest, Sha256KnownVectors) {
    EXPECT_EQ(trust::playground::sha256Hex(""), "e3b0c44298fc1c149afbf4c8996fb92427ae41e4649b934ca495991b7852b855");
    EXPECT_EQ(trust::playground::sha256Hex("abc"), "ba7816bf8f01cfea414140de5dae2223b00361a396177a9cb410ff61f20015ad");
    EXPECT_EQ(trust::playground::sha256Hex("The quick brown fox jumps over the lazy dog"), "d7a8fbb307d7809469ca9abcb0082e4f8d5651e46d3cdb762d02d0bf37c9e592");
}

TEST(HashTest, ConstantTimeEqual) {
    EXPECT_TRUE(trust::playground::constantTimeEqual("abc", "abc"));
    EXPECT_FALSE(trust::playground::constantTimeEqual("abc", "abd"));
    EXPECT_FALSE(trust::playground::constantTimeEqual("abc", "abcd"));
    EXPECT_TRUE(trust::playground::constantTimeEqual("", ""));
}

TEST(AccessTest, OriginAndHostBinding) {
    PlaygroundConfig cfg = makeCfg();
    cfg.allowedOrigins = {"https://trust-lang.net"};
    cfg.allowedHosts = {"playground.trust-lang.net"};
    PlaygroundServer server(cfg);

    // Чужой Origin (Host верный) -> 403.
    {
        HttpRequest rq = makeReq("POST", "/run", "print 1");
        rq.host = "playground.trust-lang.net";
        rq.origin = "https://evil.example.com";
        EXPECT_EQ(server.handle(rq, "10.0.0.1").status, 403);
    }
    // Верный Origin, чужой Host -> 403.
    {
        HttpRequest rq = makeReq("POST", "/run", "print 1");
        rq.host = "evil.example.com";
        rq.origin = "https://trust-lang.net";
        EXPECT_EQ(server.handle(rq, "10.0.0.1").status, 403);
    }
    // Верный Origin + Host -> проходит (нет воркеров -> 503, но НЕ 403).
    {
        HttpRequest rq = makeReq("POST", "/run", "print 1");
        rq.host = "playground.trust-lang.net";
        rq.origin = "https://trust-lang.net";
        EXPECT_EQ(server.handle(rq, "10.0.0.1").status, 503);
    }
}

// public_token удалён: он был публичным (лежит в странице) и не давал защиты.
// «Работает только с конкретной песочницей» обеспечивают Origin/Host-привязка + fail-closed
// CORS + PoW.

TEST(CacheTest, RunCacheByExampleName) {
    PlaygroundConfig cfg = makeCfg();
    cfg.workers.push_back({"test-worker", kToken});
    PlaygroundServer server(cfg);
    const std::string result_json = "{\"ok\":true,\"source\":\"codeA\",\"cpp\":\"int x;\"}";

    // Воркер забирает задачу и возвращает результат -> он же пишет в кеш по имени примера.
    std::thread worker([&] {
        const std::string poll_body = std::string("{\"token\":\"") + kToken + "\",\"capacity\":1}";
        const HttpResponse poll = server.handle(makeReq("POST", "/poll", poll_body), "10.0.0.4");
        ASSERT_EQ(poll.status, 200);
        const auto job = nlohmann::json::parse(poll.body);
        const auto rb = nlohmann::json{{"token", kToken}, {"jobId", job["jobId"]}, {"result", result_json}};
        server.handle(makeReq("POST", "/result", rb.dump()), "10.0.0.4");
    });
    std::this_thread::sleep_for(std::chrono::milliseconds(50));
    HttpRequest r1r = makeReq("POST", "/run", "codeA");
    r1r.exampleName = "hello.src";
    const HttpResponse r1 = server.handle(r1r, "10.0.0.5");
    worker.join();
    ASSERT_EQ(r1.status, 200);
    EXPECT_EQ(r1.body, result_json);

    // Повторный /run того же примера -> из кеша, без воркера (которого больше нет).
    HttpRequest r2r = makeReq("POST", "/run", "codeA");
    r2r.exampleName = "hello.src";
    const HttpResponse r2 = server.handle(r2r, "10.0.0.5");
    ASSERT_EQ(r2.status, 200);
    EXPECT_EQ(r2.body, result_json);

    // БЕЗ имени примера (произвольный код) -> сразу на выполнение, кеша нет -> 503.
    EXPECT_EQ(server.handle(makeReq("POST", "/run", "codeB"), "10.0.0.5").status, 503);
    // Другое имя примера -> кеш-промах, воркеров нет -> 503.
    HttpRequest r3r = makeReq("POST", "/run", "codeA");
    r3r.exampleName = "other.src";
    EXPECT_EQ(server.handle(r3r, "10.0.0.5").status, 503);
    // ОТРАВЛЕНИЕ кеша: то же имя, но ДРУГОЙ код -> НЕ из кеша (503, кеш-промах).
    HttpRequest r4r = makeReq("POST", "/run", "codeX");
    r4r.exampleName = "hello.src";
    EXPECT_EQ(server.handle(r4r, "10.0.0.5").status, 503);
}

TEST(RateLimitTest, ForwardedIpPerClient) {
    PlaygroundConfig cfg = makeCfg();
    cfg.rateLimitPerIp = 2;
    PlaygroundServer server(cfg);

    const auto mk = [&](const std::string& xff) {
        HttpRequest rq = makeReq("POST", "/run", "print 1");
        rq.xForwardedFor = xff;
        return server.handle(rq, "127.0.0.1"); // за nginx peer - loopback
    };
    EXPECT_EQ(mk("1.2.3.4").status, 503);
    EXPECT_EQ(mk("1.2.3.4").status, 503);
    EXPECT_EQ(mk("1.2.3.4").status, 429); // третий с того же IP
    EXPECT_EQ(mk("5.6.7.8").status, 503); // другой IP не лимитирован
}

TEST(PowTest, ChallengeRequiredAndVerified) {
    PlaygroundConfig cfg = makeCfg();
    cfg.powMinDifficulty = 8;
    cfg.powMaxDifficulty = 24;
    PlaygroundServer server(cfg);

    const HttpResponse ch = server.handle(makeReq("GET", "/challenge", ""), "10.0.0.1");
    ASSERT_EQ(ch.status, 200);
    const auto cj = nlohmann::json::parse(ch.body);
    ASSERT_EQ(cj["required"], true);
    const std::string nonce = cj["nonce"];
    const int difficulty = cj["difficulty"];
    ASSERT_GE(difficulty, 8);

    // Без X-PoW -> 402.
    EXPECT_EQ(server.handle(makeReq("POST", "/run", "print 1"), "10.0.0.1").status, 402);

    // С верным решением -> PoW проходит (нет воркеров -> 503).
    const std::string solution = solvePow(nonce, difficulty);
    ASSERT_FALSE(solution.empty());
    HttpRequest ok = makeReq("POST", "/run", "print 1");
    ok.xPow = nonce + ":" + solution;
    EXPECT_EQ(server.handle(ok, "10.0.0.1").status, 503);

    // С неверным решением -> 402.
    // Ищем решение, чей sha256(nonce+sol) имеет НОЛЬ ведущих нулевых бит: первый
    // hex-ниббл >= 8 (старший бит = 1). Это гарантированно не проходит требуемую
    // сложность (>=8) - слепой фикс-соль (например, "zzz") на низкой сложности с
    // вероятностью ~2^-8 случайно проходил бы проверку, делая тест флаки.
    std::string bad_sol;
    for (long long i = 0; i < 2000000 && bad_sol.empty(); ++i) {
        const std::string sol = std::to_string(i);
        const std::string h = trust::playground::sha256Hex(nonce + sol);
        if (!h.empty()) {
            const int v0 = (h[0] >= '0' && h[0] <= '9') ? (h[0] - '0') : (h[0] - 'a' + 10);
            if (v0 >= 8) {
                bad_sol = sol;
            }
        }
    }
    ASSERT_FALSE(bad_sol.empty());
    HttpRequest bad = makeReq("POST", "/run", "print 1");
    bad.xPow = nonce + ":" + bad_sol;
    EXPECT_EQ(server.handle(bad, "10.0.0.1").status, 402);
}

// PoW выключен (powMinDifficulty=0) - /run НЕ требует X-PoW и не отдаёт 402.
TEST(PowTest, DisabledWhenOff) {
    PlaygroundConfig cfg = makeCfg(); // powMinDifficulty=0
    PlaygroundServer server(cfg);
    EXPECT_NE(server.handle(makeReq("POST", "/run", "print 1"), "10.0.0.1").status, 402);
}

// Незнакомый/протухший nonce в X-PoW отклоняется (402).
TEST(PowTest, RejectUnknownOrExpiredNonce) {
    PlaygroundConfig cfg = makeCfg();
    cfg.powMinDifficulty = 8;
    PlaygroundServer server(cfg);
    HttpRequest rq = makeReq("POST", "/run", "print 1");
    rq.xPow = "ffffffffffffffffffffffffffffffffffffffffffffffffffffffffffffffff:1";
    EXPECT_EQ(server.handle(rq, "10.0.0.1").status, 402);
}

// Лимит использований одного решённого челленджа: после N использований - 402.
TEST(PowTest, NonceUseLimit) {
    PlaygroundConfig cfg = makeCfg();
    cfg.powMinDifficulty = 8;
    cfg.powMaxUsesPerNonce = 2;
    PlaygroundServer server(cfg);

    const HttpResponse ch = server.handle(makeReq("GET", "/challenge", ""), "10.0.0.1");
    ASSERT_EQ(ch.status, 200);
    const auto cj = nlohmann::json::parse(ch.body);
    const std::string nonce = cj["nonce"];
    const int difficulty = cj["difficulty"];
    const std::string solution = solvePow(nonce, difficulty);
    ASSERT_FALSE(solution.empty());
    const std::string pow = nonce + ":" + solution;

    for (int i = 0; i < 2; ++i) {
        HttpRequest ok = makeReq("POST", "/run", "print 1");
        ok.xPow = pow;
        EXPECT_EQ(server.handle(ok, "10.0.0.1").status, 503); // PoW прошёл
    }
    // Третий раз - лимит использований исчерпан.
    HttpRequest bad = makeReq("POST", "/run", "print 1");
    bad.xPow = pow;
    EXPECT_EQ(server.handle(bad, "10.0.0.1").status, 402);
}

// Решение, НЕ удовлетворяющее требуемой сложности, отклоняется.
TEST(PowTest, SolutionMustMeetDifficulty) {
    PlaygroundConfig cfg = makeCfg();
    cfg.powMinDifficulty = 12;
    PlaygroundServer server(cfg);

    const HttpResponse ch = server.handle(makeReq("GET", "/challenge", ""), "10.0.0.1");
    ASSERT_EQ(ch.status, 200);
    const auto cj = nlohmann::json::parse(ch.body);
    const std::string nonce = cj["nonce"];
    const int difficulty = cj["difficulty"];
    ASSERT_GE(difficulty, 12);

    // Находим решение с ЗАВЕДОМО НЕДОСТАТОЧНОЙ сложностью: первый hex-символ != '0'
    // (т.е. 0 ведущих нулевых бит), что гарантированно меньше difficulty >= 12.
    std::string bad_sol;
    for (long long i = 0; i < 2000000 && bad_sol.empty(); ++i) {
        const std::string sol = std::to_string(i);
        const std::string hash = trust::playground::sha256Hex(nonce + sol);
        if (hash.empty() || hash[0] != '0') {
            bad_sol = sol;
        }
    }
    ASSERT_FALSE(bad_sol.empty());

    HttpRequest rq = makeReq("POST", "/run", "print 1");
    rq.xPow = nonce + ":" + bad_sol;
    EXPECT_EQ(server.handle(rq, "10.0.0.1").status, 402);
}

// X-Forwarded-For доверяется ТОЛЬКО когда peer - loopback (за nginx). Для не-loopback peer
// XFF игнорируется (иначе его можно подделать снаружи и обойти rate-limit): разные XFF с
// одного peer учитываются как ОДИН IP.
TEST(RateLimitTest, ForwardedIpIgnoredForNonLoopbackPeer) {
    PlaygroundConfig cfg = makeCfg();
    cfg.rateLimitPerIp = 2;
    PlaygroundServer server(cfg);
    const auto mk = [&](const std::string& xff) {
        HttpRequest rq = makeReq("POST", "/run", "print 1");
        rq.xForwardedFor = xff;
        return server.handle(rq, "10.9.9.9"); // НЕ loopback
    };
    // Разные XFF, но один peer: XFF игнорируется -> лимит считается на peer.
    EXPECT_EQ(mk("1.2.3.4").status, 503);
    EXPECT_EQ(mk("5.6.7.8").status, 503); // 2-й hit того же peer
    EXPECT_EQ(mk("9.9.9.9").status, 429); // 3-й -> лимит
}

} // namespace
