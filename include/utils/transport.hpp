#ifndef TRUST_TRANSPORT_HPP
#define TRUST_TRANSPORT_HPP

#include "utils/io.hpp"

#include <cerrno>
#include <cstring>
#include <iostream>
#include <limits>
#include <string>

#include <arpa/inet.h>
#include <netinet/in.h>
#include <poll.h>
#include <sys/socket.h>
#include <unistd.h>

namespace trust {
namespace transport {

// -- Базовый абстрактный интерфейс транспорта (общий для DAP и LSP) --
class Transport {
  public:
    virtual ~Transport() = default;
    virtual std::string readPacket() = 0;
    virtual void send(const std::string& payload) = 0;

    // Возвращает fd, доступный для poll(), или -1, если транспорт не поллится.
    // Используется главным циклом LSP для неблокирующего ожидания ввода.
    virtual int pollFd() const { return -1; }

    // Ожидание входных данных с таймаутом (мс).
    // Возвращает 1 - данные готовы, 0 - таймаут, -1 - ошибка.
    virtual int waitInput(int timeoutMs) const {
        int fd = pollFd();
        if (fd < 0) {
            return 1; // транспорт не поллится - считаем данные всегда готовыми
        }
        struct pollfd p{fd, POLLIN, 0};
        for (;;) {
            int r = ::poll(&p, 1, timeoutMs);
            if (r < 0 && errno == EINTR) {
                continue; // прерывание сигналом - повторяем ожидание
            }
            return r;
        }
    }
};

// -- Content-Length чтение (общий для LSP и DAP) --
//
// Максимальный размер одного LSP/DAP-пакета (байт). Защита от атаки
// «Content-Length: <огромное>», которая иначе приводит к аллокации всего
// объявленного объёма прямо из заголовка (OOM / memory DoS).
inline constexpr int kMaxPacketBytes = 64 * 1024 * 1024; // 64 МБ

// Проверяет, является ли строка заголовком Content-Length (case-insensitive)
inline bool isContentLength(const std::string& line) {
    if (line.size() < 14) {
        return false;
    }
    char buf[15];
    for (int i = 0; i < 14; ++i) {
        buf[i] = static_cast<char>(std::tolower(static_cast<unsigned char>(line[i])));
    }
    buf[14] = '\0';
    return std::strcmp(buf, "content-length") == 0;
}

// Разбирает заголовок Content-Length из строки, возвращает длину или 0
// (0 также возвращается при отсутствии/невалидном/переполнившемся значении - без исключений).
inline int parseContentLength(const std::string& line) {
    size_t pos = line.find(':');
    if (pos == std::string::npos) {
        return 0;
    }
    long long result = 0;
    for (size_t i = pos + 1; i < line.size(); ++i) {
        char c = line[i];
        if (c == ' ' || c == '\t' || c == '\r') {
            continue;
        }
        if (c < '0' || c > '9') {
            return 0; // нецифровой символ - невалидная длина
        }
        // Защита от переполнения int при накоплении.
        if (result > (std::numeric_limits<int>::max() - (c - '0')) / 10) {
            return 0;
        }
        result = result * 10 + (c - '0');
    }
    return static_cast<int>(result);
}

// -- StdioTransport (stdin/stdout) --
// Читает пакеты НАПРЯМУЮ из fd 0 (без std::cin), чтобы poll на fd 0 был надёжным -
// смешивание iostreams-буфера std::cin и poll(fd0) приводило к пропуску пакетов.
class StdioTransport : public Transport {
  public:
    StdioTransport() = default;

    std::string readPacket() override {
        int contentLength = 0;
        std::string line;
        char c;
        // Читаем заголовки построчно из fd 0
        while (::read(0, &c, 1) == 1) {
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

        if (contentLength <= 0 || contentLength > kMaxPacketBytes) {
            return {};
        }

        std::string body(contentLength, '\0');
        ssize_t total = 0;
        while (total < contentLength) {
            ssize_t n = ::read(0, &body[0] + total, static_cast<size_t>(contentLength - total));
            if (n <= 0) {
                return {};
            }
            total += n;
        }

        return body;
    }

    void send(const std::string& payload) override { trust::outs() << "Content-Length: " << payload.size() << "\r\n\r\n" << payload << std::flush; }

    int pollFd() const override { return 0; }

    int waitInput(int timeoutMs) const override {
        struct pollfd p{0, POLLIN, 0};
        for (;;) {
            int r = ::poll(&p, 1, timeoutMs);
            if (r < 0 && errno == EINTR) {
                continue;
            }
            return r;
        }
    }
};

// -- TcpTransport --
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
            if (fd_ >= 0) {
                ::close(fd_);
            }
            fd_ = other.fd_;
            other.fd_ = -1;
        }
        return *this;
    }

    std::string readPacket() override {
        int contentLength = 0;
        std::string line;

        while (true) {
            char c;
            ssize_t n = ::read(fd_, &c, 1);
            if (n <= 0) {
                return {};
            }

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

        if (contentLength <= 0 || contentLength > kMaxPacketBytes) {
            return {};
        }

        std::string body(contentLength, '\0');
        ssize_t total = 0;
        while (total < contentLength) {
            ssize_t n = ::read(fd_, &body[0] + total, contentLength - total);
            if (n <= 0) {
                return {};
            }
            total += n;
        }

        return body;
    }

    void send(const std::string& payload) override {
        std::string header = "Content-Length: " + std::to_string(payload.size()) + "\r\n\r\n";
        std::string full = header + payload;
        ::write(fd_, full.data(), full.size());
    }

    int fd() const { return fd_; }

    int pollFd() const override { return fd_; }

  private:
    int fd_ = -1;
};

// -- TCP server helpers --

inline int createTcpServer(int port) {
    int fd = ::socket(AF_INET, SOCK_STREAM, 0);
    if (fd < 0) {
        trust::errs() << "Error: cannot create socket: " << std::strerror(errno) << "\n";
        return -1;
    }

    int opt = 1;
    ::setsockopt(fd, SOL_SOCKET, SO_REUSEADDR, &opt, sizeof(opt));

    struct sockaddr_in addr{};
    addr.sin_family = AF_INET;
    addr.sin_addr.s_addr = INADDR_ANY;
    addr.sin_port = htons(static_cast<uint16_t>(port));

    if (::bind(fd, reinterpret_cast<struct sockaddr*>(&addr), sizeof(addr)) < 0) {
        trust::errs() << "Error: cannot bind to port " << port << ": " << std::strerror(errno) << "\n";
        ::close(fd);
        return -1;
    }

    if (::listen(fd, 1) < 0) {
        trust::errs() << "Error: cannot listen on port " << port << ": " << std::strerror(errno) << "\n";
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
        trust::errs() << "Error: accept failed: " << std::strerror(errno) << "\n";
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