#ifndef TRUST_LSP_PROTOCOL_H
#define TRUST_LSP_PROTOCOL_H

#include <string>

#include "utils/transport.hpp"
#include <nlohmann/json.hpp>

// ── Опции командной строки ──
struct LspOptions {
    int port = -1;          // -1 = interactive (stdin/stdout), >0 = TCP server
    std::string projectDir; // рабочая директория проекта
    std::string tempDir;    // каталог для временных транспилированных .cpp файлов
    bool trace = false;     // трассировка LSP
    bool help = false;
};

static constexpr int LSP_DEFAULT_PORT = 4712;

// ── LSP Protocol helpers ──
// Принимают trust::transport::Transport& (см. utils/transport.hpp)
nlohmann::json readLspPacket(trust::transport::Transport& transport);
void sendLspResponse(trust::transport::Transport& transport, const nlohmann::json& id, const nlohmann::json& result);
void sendLspError(trust::transport::Transport& transport, const nlohmann::json& id, int code, const std::string& message);
void sendLspNotification(trust::transport::Transport& transport, const std::string& method, const nlohmann::json& params);
void sendLspRequest(trust::transport::Transport& transport, const std::string& method, const nlohmann::json& params);

// ── CLI parsing ──
LspOptions parseLspOptions(int argc, const char* argv[]);
void printLspUsage(const char* prog);

#endif // TRUST_LSP_PROTOCOL_H
