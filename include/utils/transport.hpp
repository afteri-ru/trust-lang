#ifndef TRUST_TRANSPORT_HPP
#define TRUST_TRANSPORT_HPP

#include <cstring>
#include <iostream>
#include <sstream>
#include <string>

#include <arpa/inet.h>
#include <netinet/in.h>
#include <sys/socket.h>
#include <unistd.h>

namespace trust {
namespace transport {

// ── Базовый абстрактный интерфейс транспорта (общий для DAP и LSP) ──
class Transport {
  public:
    virtual ~Transport() = default;
    virtual std::string readPacket() = 0;
    virtual void send(const std::string& payload) = 0;
};

// ── Content-Length чтение (общий для LSP и DAP) ──

// Проверяет, является ли строка заголовком Content-Length (case-insensitive)
inline bool isContentLength(const std::string& line) {
    if (line.size() < 14)
        return false;
    char buf[15];
    for (int i = 0; i < 14; ++i) {
        buf[i] = static_cast<char>(std::tolower(static_cast<unsigned char>(line[i])));
    }
    buf[14] = '\0';
    return std::strcmp(buf, "content-length") == 0;
}

// Разбирает заголовок Content-Length из строки, возвращает длину или 0
inline int parseContentLength(const std::string& line) {
    size_t pos = line.find(':');
    if (pos == std::string::npos)
        return 0;
    std::string val = line.substr(pos + 1);
    val.erase(0, val.find_first_not_of(" \t"));
    return std::stoi(val);
}

// ── StdioTransport (stdin/stdout) ──
class StdioTransport : public Transport {
  public:
    StdioTransport() = default;

    std::string readPacket() {
        int contentLength = 0;
        std::string line;
        while (std::getline(std::cin, line)) {
            if (!line.empty() && line.back() == '\r') {
                line.pop_back();
            }
            if (line.empty()) {
                break;
            }
            if (isContentLength(line)) {
                contentLength = parseContentLength(line);
            }
        }

        if (contentLength <= 0) {
            return {};
        }

        std::string body(contentLength, '\0');
        std::cin.read(&body[0], contentLength);
        if (std::cin.gcount() != contentLength) {
            return {};
        }

        return body;
    }

    void send(const std::string& payload) { std::cout << "Content-Length: " << payload.size() << "\r\n\r\n" << payload << std::flush; }
};

// ── TcpTransport ──
class TcpTransport : public Transport {
  public:
    explicit TcpTransport(int fd)
    : fd_(fd) {}

    ~TcpTransport() {
        if (fd_ >= 0) {
            ::close(fd_);
        }
    }

    // non-copyable
    TcpTransport(const TcpTransport&) = delete;
    TcpTransport& operator=(const TcpTransport&) = delete;

    // move
    TcpTransport(TcpTransport&& other) noexcept
    : fd_(other.fd_) {
        other.fd_ = -1;
    }
    TcpTransport& operator=(TcpTransport&& other) noexcept {
        if (this != &other) {
            if (fd_ >= 0)
                ::close(fd_);
            fd_ = other.fd_;
            other.fd_ = -1;
        }
        return *this;
    }

    std::string readPacket() {
        int contentLength = 0;
        std::string line;

        while (true) {
            char c;
            ssize_t n = ::read(fd_, &c, 1);
            if (n <= 0)
                return {};

            if (c == '\n') {
                if (!line.empty() && line.back() == '\r') {
                    line.pop_back();
                }
                if (line.empty()) {
                    break;
                }
                if (isContentLength(line)) {
                    contentLength = parseContentLength(line);
                }
                line.clear();
            } else {
                line += c;
            }
        }

        if (contentLength <= 0) {
            return {};
        }

        std::string body(contentLength, '\0');
        ssize_t total = 0;
        while (total < contentLength) {
            ssize_t n = ::read(fd_, &body[0] + total, contentLength - total);
            if (n <= 0)
                return {};
            total += n;
        }

        return body;
    }

    void send(const std::string& payload) {
        std::string header = "Content-Length: " + std::to_string(payload.size()) + "\r\n\r\n";
        std::string full = header + payload;
        ::write(fd_, full.data(), full.size());
    }

    int fd() const { return fd_; }

  private:
    int fd_ = -1;
};

// ── TCP server helpers ──

inline int createTcpServer(int port) {
    int fd = ::socket(AF_INET, SOCK_STREAM, 0);
    if (fd < 0) {
        std::cerr << "Error: cannot create socket: " << std::strerror(errno) << "\n";
        return -1;
    }

    int opt = 1;
    ::setsockopt(fd, SOL_SOCKET, SO_REUSEADDR, &opt, sizeof(opt));

    struct sockaddr_in addr{};
    addr.sin_family = AF_INET;
    addr.sin_addr.s_addr = INADDR_ANY;
    addr.sin_port = htons(static_cast<uint16_t>(port));

    if (::bind(fd, reinterpret_cast<struct sockaddr*>(&addr), sizeof(addr)) < 0) {
        std::cerr << "Error: cannot bind to port " << port << ": " << std::strerror(errno) << "\n";
        ::close(fd);
        return -1;
    }

    if (::listen(fd, 1) < 0) {
        std::cerr << "Error: cannot listen on port " << port << ": " << std::strerror(errno) << "\n";
        ::close(fd);
        return -1;
    }

    return fd;
}

inline int acceptConnection(int serverFd) {
    struct sockaddr_in clientAddr{};
    socklen_t clientLen = sizeof(clientAddr);
    int clientFd = ::accept(serverFd, reinterpret_cast<struct sockaddr*>(&clientAddr), &clientLen);
    if (clientFd < 0) {
        std::cerr << "Error: accept failed: " << std::strerror(errno) << "\n";
        return -1;
    }

    char clientIP[INET_ADDRSTRLEN];
    ::inet_ntop(AF_INET, &clientAddr.sin_addr, clientIP, sizeof(clientIP));
    clientIP[sizeof(clientIP) - 1] = '\0';
    (void)clientIP; // unused, but useful for logging

    return clientFd;
}

} // namespace transport
} // namespace trust

#endif // TRUST_TRANSPORT_HPP