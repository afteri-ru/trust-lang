#ifndef TRUST_DAP_TRANSPORT_H
#define TRUST_DAP_TRANSPORT_H

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
struct DapOptions {
    int port = -1;              // -1 = interactive, >0 = server mode
    std::string projectDir;     // рабочая директория проекта
    std::string lldbServerPath;
    bool help = false;
};

static constexpr int DAP_DEFAULT_PORT = 4711;

// ── DAP Transport abstraction ──
class DapTransport {
  public:
    virtual ~DapTransport() = default;
    virtual std::string readPacket() = 0;
    virtual void send(const std::string &payload) = 0;
};

class StdioTransport : public DapTransport {
  public:
    std::string readPacket() override;
    void send(const std::string &payload) override;
};

class TcpTransport : public DapTransport {
  public:
    explicit TcpTransport(int fd) : fd_(fd) {}

    ~TcpTransport() override {
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

// ── DAP Protocol helpers ──
int nextDapSeq();
void sendDapResponse(class DapTransport &transport, int requestSeq, const nlohmann::json &body, bool success = true);
void sendDapEvent(class DapTransport &transport, const std::string &eventName, const nlohmann::json &body);
void sendDapOutput(class DapTransport &transport, const std::string &category, const std::string &output);

// ── DAP packet helpers ──
nlohmann::json readDapPacket(class DapTransport &transport);
void sendBreakpointEvent(class DapTransport &transport, const std::string &srcPath, int line, int bpId, bool verified);

// ── TCP server helpers ──
int createTcpServer(int port);
int acceptConnection(int serverFd);

// ── CLI parsing ──
DapOptions parseDapOptions(int argc, const char *argv[]);
void printUsage(const char *prog);

#endif // TRUST_DAP_TRANSPORT_H
