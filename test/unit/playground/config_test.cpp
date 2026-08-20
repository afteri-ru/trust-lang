// -----------------------------------------------------------------------
// Unit tests for trust-playground config parser (playground/config.h).
// -----------------------------------------------------------------------

#include "playground/config.h"

#include <gtest/gtest.h>

#include <fstream>
#include <string>

namespace {

using trust::playground::generateToken;
using trust::playground::isLoopbackHost;
using trust::playground::isValidToken;
using trust::playground::loadConfig;
using trust::playground::PlaygroundConfig;
using trust::playground::validateWorkerPlaygroundUrl;
using trust::playground::workerLabelForToken;

// Пишет содержимое во временный файл в TEST_DATA_DIR (в _build) и возвращает его имя.
std::string writeTempConfig(const std::string& content) {
    static int counter = 0;
    const std::string path = std::string(TEST_DATA_DIR) + "/trust_pg_config_" + std::to_string(counter++) + ".conf";
    std::ofstream f(path);
    f << content;
    f.close();
    return path;
}

TEST(ConfigTest, ParsesPrefixesAndRegistry) {
    const std::string path = writeTempConfig("playground.listen=0.0.0.0\n"
                                             "playground.port=9090\n"
                                             "playground.max_queue=42\n"
                                             "worker.playground_url=https://run.example.net\n"
                                             "worker.token=aaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaa\n"
                                             "worker.max_parallel=8\n"
                                             "worker.lsp_bin=/opt/trust/bin/trust-lsp\n"
                                             "alice-home=bbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbb\n"
                                             "bob-office=cccccccccccccccccccccccccccccccccccccccccccccccccccccccccccccccc\n");
    PlaygroundConfig cfg;
    std::string error;
    ASSERT_TRUE(loadConfig(path, cfg, error)) << error;

    EXPECT_EQ(cfg.listen, "0.0.0.0");
    EXPECT_EQ(cfg.port, 9090);
    EXPECT_EQ(cfg.maxQueue, 42);
    EXPECT_EQ(cfg.playgroundUrl, "https://run.example.net");
    EXPECT_EQ(cfg.token, "aaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaa");
    EXPECT_EQ(cfg.maxParallel, 8);
    EXPECT_EQ(cfg.lspBin, "/opt/trust/bin/trust-lsp");

    ASSERT_EQ(cfg.workers.size(), 2u);
    EXPECT_EQ(cfg.workers[0].label, "alice-home");
    EXPECT_EQ(cfg.workers[1].label, "bob-office");
    EXPECT_EQ(workerLabelForToken(cfg, cfg.workers[0].token), "alice-home");
    EXPECT_EQ(workerLabelForToken(cfg, "unknown"), "");
}

TEST(ConfigTest, RejectsInvalidTokenFormat) {
    const std::string path = writeTempConfig("badlabel=not-a-hex-token\n");
    PlaygroundConfig cfg;
    std::string error;
    EXPECT_FALSE(loadConfig(path, cfg, error));
    EXPECT_NE(error.find("invalid worker token"), std::string::npos);
}

TEST(ConfigTest, IsValidTokenChecksLengthAndHex) {
    EXPECT_FALSE(isValidToken(""));
    EXPECT_FALSE(isValidToken("short"));
    EXPECT_FALSE(isValidToken(std::string(64, 'z'))); // 'z' не hex
    EXPECT_TRUE(isValidToken(std::string(64, 'a')));
    EXPECT_TRUE(isValidToken(std::string(64, 'f')));
}

TEST(ConfigTest, DefaultPlaygroundUrl) {
    const PlaygroundConfig cfg;
    EXPECT_EQ(cfg.playgroundUrl, "https://playground.trust-lang.net");
}

TEST(ConfigTest, GenerateTokenProducesValidDistinctTokens) {
    const std::string tok = generateToken();
    EXPECT_EQ(tok.size(), 64u);
    EXPECT_TRUE(isValidToken(tok));
    // Два вызова дают разные токены (случайность из /dev/urandom).
    EXPECT_NE(tok, generateToken());
}

TEST(ConfigTest, IgnoresCommentsAndBlankLines) {
    const std::string path = writeTempConfig("# comment\n"
                                             "\n"
                                             "   \n"
                                             "playground.port=7000\n"
                                             "# another\n");
    PlaygroundConfig cfg;
    std::string error;
    ASSERT_TRUE(loadConfig(path, cfg, error)) << error;
    EXPECT_EQ(cfg.port, 7000);
}

TEST(ConfigTest, LoopbackHostDetection) {
    EXPECT_TRUE(isLoopbackHost("localhost"));
    EXPECT_TRUE(isLoopbackHost("127.0.0.1"));
    EXPECT_TRUE(isLoopbackHost("127.0.0.2"));
    EXPECT_TRUE(isLoopbackHost("::1"));
    EXPECT_FALSE(isLoopbackHost("run.example.net"));
    EXPECT_FALSE(isLoopbackHost("10.0.0.1"));
    EXPECT_FALSE(isLoopbackHost(""));
}

TEST(ConfigTest, WorkerUrlRequiresTlsForNonLoopback) {
    std::string err;
    // https:// допустим везде (в т.ч. loopback).
    EXPECT_TRUE(validateWorkerPlaygroundUrl("https://run.example.net", err));
    EXPECT_TRUE(validateWorkerPlaygroundUrl("https://127.0.0.1:8080", err));
    // http:// разрешён только для loopback-хостов.
    EXPECT_TRUE(validateWorkerPlaygroundUrl("http://127.0.0.1:8080", err));
    EXPECT_TRUE(validateWorkerPlaygroundUrl("http://localhost", err));
    EXPECT_TRUE(validateWorkerPlaygroundUrl("http://[::1]:8080", err));
    EXPECT_TRUE(validateWorkerPlaygroundUrl("http://127.5.6.7", err));
    // http:// для не-loopback — запрещено (утечка токена/полезной нагрузки).
    EXPECT_FALSE(validateWorkerPlaygroundUrl("http://run.example.net", err));
    EXPECT_FALSE(validateWorkerPlaygroundUrl("http://192.168.1.10:8080", err));
    // Некорректные URL.
    EXPECT_FALSE(validateWorkerPlaygroundUrl("run.example.net", err));
    EXPECT_FALSE(validateWorkerPlaygroundUrl("ftp://run.example.net", err));
    EXPECT_FALSE(validateWorkerPlaygroundUrl("http://", err));
}

} // namespace
