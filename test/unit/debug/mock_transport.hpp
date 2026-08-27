#pragma once

#include "utils/transport.hpp"

#include <nlohmann/json.hpp>

using json = nlohmann::json;

// -- MockTransport: эмулирует клиента для тестов --
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
            size_t eol = mockInput.find('\n', pos);
            if (eol == std::string::npos) {
                break;
            }

            std::string line = mockInput.substr(pos, eol - pos);
            if (!line.empty() && line.back() == '\r') {
                line.pop_back();
            }
            pos = eol + 1;

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