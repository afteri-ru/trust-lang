// -----------------------------------------------------------------------
// Unit tests for trust-playground worker helpers (playground/worker.h):
// redactToken (маскировка токена в публичном ответе) и validateWorkerSettings
// (fail-fast проверка настроек воркера при запуске).
// -----------------------------------------------------------------------

#include "playground/worker.h"

#include <gtest/gtest.h>

#include <filesystem>
#include <fstream>
#include <string>

namespace {

using trust::playground::PlaygroundConfig;
using trust::playground::PlaygroundWorker;
using trust::playground::redactToken;
using trust::playground::validateWorkerSettings;

constexpr const char* kToken = "aaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaa";

// Пишет временный файл в TEST_DATA_DIR и делает его исполняемым (для access(X_OK)).
std::string writeExecutable(const std::string& name) {
    const std::string path = std::string(TEST_DATA_DIR) + "/" + name;
    {
        std::ofstream f(path);
        f << "#!/bin/sh\nexit 0\n";
    }
    std::filesystem::permissions(path, std::filesystem::perms::owner_exec, std::filesystem::perm_options::add);
    return path;
}

TEST(WorkerRedactToken, MasksExactTokenOccurrences) {
    const std::string text = std::string("cannot exec trust-lsp '") + kToken + "' : unknown option '" + kToken + "'";
    const std::string out = redactToken(text, kToken);
    EXPECT_EQ(out.find(kToken), std::string::npos);
    EXPECT_NE(out.find("<redacted>"), std::string::npos);
    // Оба вхождения замаскированы.
    EXPECT_EQ(out, std::string("cannot exec trust-lsp '<redacted>' : unknown option '<redacted>'"));
}

TEST(WorkerRedactToken, LeavesOtherHexStrings) {
    // 64-hex строка, НЕ равная токену воркера, не маскируется.
    const std::string other = std::string(64, 'b');
    const std::string out = redactToken("value=" + other, kToken);
    EXPECT_EQ(out, "value=" + other);
}

TEST(WorkerRedactToken, EmptyTokenIsNoOp) {
    const std::string text = "plain text " + std::string(kToken);
    EXPECT_EQ(redactToken(text, ""), text);
    EXPECT_EQ(redactToken(text, "ab"), text); // слишком короткий токен
}

TEST(WorkerValidateSettings, AcceptsValidConfig) {
    PlaygroundConfig cfg;
    cfg.maxParallel = 4;
    cfg.workerJobTimeoutSec = 30;
    cfg.maxMemoryMb = 512;
    cfg.maxOutputKb = 2048;
    cfg.lspBin = writeExecutable("worker_test_lsp.sh");
    cfg.projectDir = ".";
    cfg.lspOpts = {"-Wsigil=ignore"};
    EXPECT_TRUE(validateWorkerSettings(cfg).empty());
}

TEST(WorkerValidateSettings, RejectsNonexistentExecutable) {
    PlaygroundConfig cfg;
    cfg.maxParallel = 4;
    cfg.workerJobTimeoutSec = 30;
    cfg.maxMemoryMb = 512;
    cfg.maxOutputKb = 2048;
    cfg.lspBin = "/nonexistent/trust-lsp";
    const std::string err = validateWorkerSettings(cfg);
    EXPECT_FALSE(err.empty());
    EXPECT_NE(err.find("lsp_bin"), std::string::npos);
}

TEST(WorkerValidateSettings, RejectsMissingProjectDir) {
    PlaygroundConfig cfg;
    cfg.maxParallel = 4;
    cfg.workerJobTimeoutSec = 30;
    cfg.maxMemoryMb = 512;
    cfg.maxOutputKb = 2048;
    cfg.lspBin = writeExecutable("worker_test_lsp2.sh");
    cfg.projectDir = "/nonexistent/worker-project-dir";
    const std::string err = validateWorkerSettings(cfg);
    EXPECT_FALSE(err.empty());
    EXPECT_NE(err.find("project_dir"), std::string::npos);
}

TEST(WorkerValidateSettings, RejectsTokenInLspOpts) {
    PlaygroundConfig cfg;
    cfg.maxParallel = 4;
    cfg.workerJobTimeoutSec = 30;
    cfg.maxMemoryMb = 512;
    cfg.maxOutputKb = 2048;
    cfg.lspBin = writeExecutable("worker_test_lsp3.sh");
    // Случайный токен/позиционный аргумент в lsp_opts - иначе trust-lsp дал бы
    // "unknown option '<токен>'" и токен утёк бы на сайт.
    cfg.lspOpts = {kToken};
    const std::string err = validateWorkerSettings(cfg);
    EXPECT_FALSE(err.empty());
    EXPECT_NE(err.find("lsp_opts"), std::string::npos);
}

TEST(WorkerValidateSettings, AcceptsEmptyLspOpts) {
    // worker.lsp_opts может не быть настроен (пустой список / пустые элементы) -
    // проверка не должна падать: опции trust-lsp не обязательны.
    PlaygroundConfig cfg;
    cfg.maxParallel = 4;
    cfg.workerJobTimeoutSec = 30;
    cfg.maxMemoryMb = 512;
    cfg.maxOutputKb = 2048;
    cfg.lspBin = writeExecutable("worker_test_lsp_empty.sh");
    cfg.projectDir = ".";
    cfg.lspOpts = {}; // отсутствие опций
    EXPECT_TRUE(validateWorkerSettings(cfg).empty());
    cfg.lspOpts = {""}; // пустой элемент списка
    EXPECT_TRUE(validateWorkerSettings(cfg).empty());
}

TEST(WorkerValidateSettings, RejectsUnsoundLimits) {
    PlaygroundConfig cfg;
    cfg.maxParallel = 0;
    cfg.workerJobTimeoutSec = 30;
    cfg.maxMemoryMb = 512;
    cfg.maxOutputKb = 2048;
    EXPECT_NE(validateWorkerSettings(cfg).find("max_parallel"), std::string::npos);

    PlaygroundConfig cfg2;
    cfg2.maxParallel = 4;
    cfg2.workerJobTimeoutSec = 30;
    cfg2.maxMemoryMb = 0;
    cfg2.maxOutputKb = 2048;
    EXPECT_NE(validateWorkerSettings(cfg2).find("max_memory_mb"), std::string::npos);
}

// Самопроверка при запуске: при нерабочем trust-lsp run() завершается с кодом 1
// (fail-fast ДО подключения к балансировщику), а не регистрируется и не ждёт задач.
TEST(WorkerStartupSelfCheck, FailsFastWhenLspCannotTranspile) {
    PlaygroundConfig cfg;
    cfg.maxParallel = 2;
    cfg.workerJobTimeoutSec = 5;
    cfg.maxMemoryMb = 128;
    cfg.maxOutputKb = 512;
    cfg.lspBin = writeExecutable("worker_test_lsp_selftest.sh"); // exit 0, пустой stdout
    cfg.projectDir = ".";
    cfg.lspOpts = {};
    cfg.token = kToken;
    cfg.playgroundUrl = "http://127.0.0.1:9"; // loopback http: валиден, до подключения не доходит
    PlaygroundWorker worker(cfg);
    // Самопроверка (транспиляция + архив) не может пройти: trust-lsp ничего не выводит.
    EXPECT_EQ(worker.run(), 1);
}

} // namespace
