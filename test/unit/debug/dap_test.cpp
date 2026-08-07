// ── Юнит-тесты для DAP транспортного уровня ──
//
// Проверяет парсинг DAP-пакетов (Content-Length), работу заглушек
// для StdioTransport / TcpTransport, парсинг опций CLI.
//
// Без LLDB: тестируется только транспортный протокол и обработка DAP.

#include "debug/dap_transport.h"
#include "utils/transport.hpp"

#include <gmock/gmock.h>
#include <gtest/gtest.h>

#include <nlohmann/json.hpp>

using json = nlohmann::json;

// ── MockTransport: эмулирует клиента для тестов ──
class MockTransport : public trust::transport::Transport {
  public:
    std::string mockInput;      // сырые данные (заголовки + тело)
    std::string capturedOutput; // что было отправлено через send
    size_t consumed = 0;        // сколько байт уже прочитано

    void setInput(const std::string& header, const std::string& body) {
        mockInput = header + "\r\n" + body;
        consumed = 0;
    }

    // Имитирует реальный транспорт: парсит Content-Length и возвращает тело
    std::string readPacket() override {
        if (consumed >= mockInput.size()) {
            return {};
        }

        // Парсим заголовки из mockInput, начиная с consumed
        size_t pos = consumed;
        int contentLength = 0;

        while (pos < mockInput.size()) {
            // Ищем конец строки
            size_t eol = mockInput.find('\n', pos);
            if (eol == std::string::npos) {
                break;
            }

            std::string line = mockInput.substr(pos, eol - pos);
            // Убираем \r в конце
            if (!line.empty() && line.back() == '\r') {
                line.pop_back();
            }
            pos = eol + 1;

            // Пустая строка — конец заголовков
            if (line.empty()) {
                break;
            }

            if (line.rfind("Content-Length:", 0) == 0) {
                size_t colon = line.find(':');
                if (colon != std::string::npos) {
                    std::string val = line.substr(colon + 1);
                    val.erase(0, val.find_first_not_of(" \t"));
                    contentLength = std::stoi(val);
                }
            }
        }

        // После пустой строки — тело
        if (contentLength <= 0 || pos + contentLength > mockInput.size()) {
            consumed = mockInput.size();
            return {};
        }

        std::string body = mockInput.substr(pos, contentLength);
        consumed = pos + contentLength;
        return body;
    }

    void send(const std::string& payload) override { capturedOutput = payload; }
};

// ═══════════════════════════════════════════════════
// StdioTransport: парсинг Content-Length
// ═══════════════════════════════════════════════════

TEST(DapTransportTest, StdioReadPacket_Basic) {
    // Тест напрямую не тестирует std::cin — только логику парсинга
    // Используем MockTransport для проверки протокола
    MockTransport mock;
    std::string body = R"({"type":"request","command":"initialize","seq":1})";
    mock.setInput("Content-Length: " + std::to_string(body.size()), body);

    // readDapPacket использует transport.readPacket() + JSON парсинг
    json req = readDapPacket(mock);
    ASSERT_FALSE(req.is_null());
    EXPECT_EQ(req["type"], "request");
    EXPECT_EQ(req["command"], "initialize");
    EXPECT_EQ(req["seq"], 1);
}

TEST(DapTransportTest, StdioReadPacket_EmptyInput) {
    MockTransport mock;
    mock.mockInput.clear();
    json req = readDapPacket(mock);
    EXPECT_TRUE(req.is_null());
}

// ═══════════════════════════════════════════════════
// sendDapResponse: проверка формата ответа
// ═══════════════════════════════════════════════════

TEST(DapTransportTest, SendDapResponse_Success) {
    MockTransport mock;
    json body = {{"body", {{"supportsConfigurationDoneRequest", true}}}, {"command", "initialize"}};
    sendDapResponse(mock, 1, body, true);

    ASSERT_FALSE(mock.capturedOutput.empty());
    json resp = json::parse(mock.capturedOutput);

    EXPECT_EQ(resp["type"], "response");
    EXPECT_EQ(resp["request_seq"], 1);
    EXPECT_EQ(resp["command"], "initialize");
    EXPECT_TRUE(resp["success"].get<bool>());
    EXPECT_TRUE(resp["body"]["supportsConfigurationDoneRequest"].get<bool>());
}

TEST(DapTransportTest, SendDapResponse_Failure) {
    MockTransport mock;
    json body = {{"command", "launch"}, {"message", "target not found"}};
    sendDapResponse(mock, 42, body, false);

    json resp = json::parse(mock.capturedOutput);
    EXPECT_FALSE(resp["success"].get<bool>());
    EXPECT_EQ(resp["request_seq"], 42);
    EXPECT_EQ(resp["message"], "target not found");
}

// ═══════════════════════════════════════════════════
// sendDapEvent: проверка формата события
// ═══════════════════════════════════════════════════

TEST(DapTransportTest, SendDapEvent_Stopped) {
    MockTransport mock;
    sendDapEvent(mock, "stopped", {{"reason", "breakpoint"}, {"threadId", 1}});

    json evt = json::parse(mock.capturedOutput);
    EXPECT_EQ(evt["type"], "event");
    EXPECT_EQ(evt["event"], "stopped");
    EXPECT_EQ(evt["body"]["reason"], "breakpoint");
    EXPECT_EQ(evt["body"]["threadId"], 1);
}

TEST(DapTransportTest, SendDapEvent_EmptyBody) {
    MockTransport mock;
    sendDapEvent(mock, "terminated", json::object());

    json evt = json::parse(mock.capturedOutput);
    EXPECT_EQ(evt["type"], "event");
    EXPECT_EQ(evt["event"], "terminated");
    // body может отсутствовать, т.к. мы передали пустой объект
    EXPECT_TRUE(evt.contains("body"));
}

// ═══════════════════════════════════════════════════
// sendDapOutput: проверка output-события
// ═══════════════════════════════════════════════════

TEST(DapTransportTest, SendDapOutput) {
    MockTransport mock;
    sendDapOutput(mock, "stdout", "hello world\n");

    json evt = json::parse(mock.capturedOutput);
    EXPECT_EQ(evt["type"], "event");
    EXPECT_EQ(evt["event"], "output");
    EXPECT_EQ(evt["body"]["category"], "stdout");
    EXPECT_EQ(evt["body"]["output"], "hello world\n");
}

// ═══════════════════════════════════════════════════
// sendBreakpointEvent: проверка breakpoint-события
// ═══════════════════════════════════════════════════

TEST(DapTransportTest, SendBreakpointEvent_Verified) {
    MockTransport mock;
    sendBreakpointEvent(mock, "/test/main.src", 10, 1, true);

    json evt = json::parse(mock.capturedOutput);
    EXPECT_EQ(evt["event"], "breakpoint");
    EXPECT_EQ(evt["body"]["reason"], "changed");
    EXPECT_TRUE(evt["body"]["breakpoint"]["verified"].get<bool>());
    EXPECT_EQ(evt["body"]["breakpoint"]["id"], 1);
    EXPECT_EQ(evt["body"]["breakpoint"]["line"], 10);
    EXPECT_EQ(evt["body"]["breakpoint"]["source"]["path"], "/test/main.src");
}

TEST(DapTransportTest, SendBreakpointEvent_Unverified) {
    MockTransport mock;
    sendBreakpointEvent(mock, "/test/main.src", 15, 2, false);

    json evt = json::parse(mock.capturedOutput);
    EXPECT_EQ(evt["body"]["reason"], "new");
    EXPECT_FALSE(evt["body"]["breakpoint"]["verified"].get<bool>());
    EXPECT_EQ(evt["body"]["breakpoint"]["id"], 2);
}

// ═══════════════════════════════════════════════════
// parseDapOptions: проверка парсинга CLI-аргументов
// ═══════════════════════════════════════════════════

TEST(DapOptionsTest, DefaultInteractiveMode) {
    const char* argv[] = {"trust-dap", nullptr};
    auto opts = parseDapOptions(1, argv);
    EXPECT_EQ(opts.port, -1); // interactive
    EXPECT_FALSE(opts.help);
    EXPECT_TRUE(opts.projectDir.empty());
}

TEST(DapOptionsTest, ServerModeDefaultPort) {
    const char* argv[] = {"trust-dap", "server", nullptr};
    auto opts = parseDapOptions(2, argv);
    EXPECT_EQ(opts.port, DAP_DEFAULT_PORT);
}

TEST(DapOptionsTest, ServerModeCustomPort) {
    const char* argv[] = {"trust-dap", "server=9999", nullptr};
    auto opts = parseDapOptions(2, argv);
    EXPECT_EQ(opts.port, 9999);
}

TEST(DapOptionsTest, Help) {
    const char* argv[] = {"trust-dap", "--help", nullptr};
    auto opts = parseDapOptions(2, argv);
    EXPECT_TRUE(opts.help);
    EXPECT_EQ(opts.port, -1);
}

TEST(DapOptionsTest, ProjectDir) {
    const char* argv[] = {"trust-dap", "--project-dir", "/my/project", nullptr};
    auto opts = parseDapOptions(3, argv);
    EXPECT_EQ(opts.projectDir, "/my/project");
}

TEST(DapOptionsTest, GdbPath) {
    const char* argv[] = {"trust-dap", "--gdb", "/custom/path/to/gdb", nullptr};
    auto opts = parseDapOptions(3, argv);
    EXPECT_EQ(opts.gdbPath, "/custom/path/to/gdb");
}

TEST(DapOptionsTest, ServerAndProjectDir) {
    const char* argv[] = {"trust-dap", "server=7777", "--project-dir", "/app", nullptr};
    auto opts = parseDapOptions(4, argv);
    EXPECT_EQ(opts.port, 7777);
    EXPECT_EQ(opts.projectDir, "/app");
}

// ═══════════════════════════════════════════════════
// nextDapSeq: проверка инкремента seq
// ═══════════════════════════════════════════════════

TEST(DapTransportTest, SeqIncrement) {
    // Сбросим seq до 0 через вызов (внутренний счётчик статический,
    // поэтому в одном тестовом запуске seq будет расти последовательно)
    int s1 = nextDapSeq();
    int s2 = nextDapSeq();
    EXPECT_EQ(s2, s1 + 1);
    int s3 = nextDapSeq();
    EXPECT_EQ(s3, s2 + 1);
}

// ═══════════════════════════════════════════════════
// TCP transport: базовый тест создания/отправки (без сокета)
// ═══════════════════════════════════════════════════

TEST(DapTransportTest, CreateTcpServer_InvalidPort) {
    // port 0 специально выбираем, чтобы bind мог не сработать
    // Но этот тест просто проверяет, что функция не падает
    // и не содержит UB
    int fd = trust::transport::createTcpServer(0);
    // С port=0 bind может сработать или нет в зависимости от ОС
    // просто проверяем, что функция отработала
    if (fd >= 0) {
        ::close(fd);
    }
}