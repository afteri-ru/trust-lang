#ifndef TRUST_LSP_PROTOCOL_H
#define TRUST_LSP_PROTOCOL_H

#include <cstring>
#include <iostream>
#include <sstream>
#include <string>

#include <arpa/inet.h>
#include <netinet/in.h>
#include <sys/socket.h>
#include <unistd.h>

#include <nlohmann/json.hpp>

// ── Опции командной строки ──
struct LspOptions {
    int port = -1;              // -1 = interactive (stdin/stdout), >0 = TCP server
    std::string projectDir;     // рабочая директория проекта
    bool trace = false;         // трассировка LSP
    bool help = false;
};

static constexpr int LSP_DEFAULT_PORT = 4712;

// ── LSP Transport abstraction ──
class LspTransport {
  public:
    virtual ~LspTransport() = default;
    virtual std::string readPacket() = 0;
    virtual void send(const std::string &payload) = 0;
};

class StdioLspTransport : public LspTransport {
  public:
    std::string readPacket() override;
    void send(const std::string &payload) override;
};

class TcpLspTransport : public LspTransport {
  public:
    explicit TcpLspTransport(int fd) : fd_(fd) {}

    ~TcpLspTransport() override {
        if (fd_ >= 0) {
            ::close(fd_);
        }
    }

    std::string readPacket() override;
    void send(const std::string &payload) override;

    int fd() const { return fd_; }

  private:
    int fd_ = -1;
};

// ── LSP Protocol helpers ──
nlohmann::json readLspPacket(LspTransport &transport);
void sendLspResponse(LspTransport &transport, const nlohmann::json &id, const nlohmann::json &result);
void sendLspError(LspTransport &transport, const nlohmann::json &id, int code, const std::string &message);
void sendLspNotification(LspTransport &transport, const std::string &method, const nlohmann::json &params);
void sendLspRequest(LspTransport &transport, const std::string &method, const nlohmann::json &params);

// ── TCP server helpers ──
int createTcpLspServer(int port);
int acceptLspConnection(int serverFd);

// ── CLI parsing ──
LspOptions parseLspOptions(int argc, const char *argv[]);
void printLspUsage(const char *prog);

#endif // TRUST_LSP_PROTOCOL_H