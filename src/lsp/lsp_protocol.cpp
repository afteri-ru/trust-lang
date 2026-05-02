#include "lsp/lsp_protocol.h"
#include <algorithm>
#include <cstring>
#include <iomanip>
#include <iostream>

// ── Case-insensitive Content-Length проверка ──
static bool isContentLength(const std::string &line) {
    if (line.size() < 14) return false;
    char buf[15];
    for (int i = 0; i < 14; ++i) {
        buf[i] = static_cast<char>(std::tolower(static_cast<unsigned char>(line[i])));
    }
    buf[14] = '\0';
    return std::strcmp(buf, "content-length") == 0;
}

// ── StdioLspTransport ──

std::string StdioLspTransport::readPacket() {
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
            size_t pos = line.find(':');
            if (pos != std::string::npos) {
                std::string val = line.substr(pos + 1);
                val.erase(0, val.find_first_not_of(" \t"));
                contentLength = std::stoi(val);
            }
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

void StdioLspTransport::send(const std::string &payload) {
    std::cout << "Content-Length: " << payload.size() << "\r\n\r\n" << payload << std::flush;
}

// ── TcpLspTransport ──

std::string TcpLspTransport::readPacket() {
    int contentLength = 0;
    std::string line;

    while (true) {
        char c;
        ssize_t n = ::read(fd_, &c, 1);
        if (n <= 0) return {};

        if (c == '\n') {
            if (line.empty()) {
                continue;
            }
            if (!line.empty() && line.back() == '\r') {
                line.pop_back();
            }
            if (line.empty()) {
                break;
            }
            if (isContentLength(line)) {
                size_t pos = line.find(':');
                if (pos != std::string::npos) {
                    std::string val = line.substr(pos + 1);
                    val.erase(0, val.find_first_not_of(" \t"));
                    contentLength = std::stoi(val);
                }
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
        if (n <= 0) return {};
        total += n;
    }

    return body;
}

void TcpLspTransport::send(const std::string &payload) {
    std::string header = "Content-Length: " + std::to_string(payload.size()) + "\r\n\r\n";
    std::string full = header + payload;
    ::write(fd_, full.data(), full.size());
}

// ── LSP Protocol helpers ──

using json = nlohmann::json;

json readLspPacket(LspTransport &transport) {
    std::string body = transport.readPacket();
    if (body.empty()) {
        return json();
    }

    try {
        return json::parse(body);
    } catch (const json::parse_error &e) {
        std::cerr << "LSP JSON parse error: " << e.what() << "\n"
                  << "Raw input: " << body << "\n";
        return json();
    }
}

void sendLspResponse(LspTransport &transport, const json &id, const json &result) {
    json resp = {
        {"jsonrpc", "2.0"},
        {"id", id},
        {"result", result}
    };
    std::string payload = resp.dump();
    transport.send(payload);
}

void sendLspError(LspTransport &transport, const json &id, int code, const std::string &message) {
    json resp = {
        {"jsonrpc", "2.0"},
        {"id", id},
        {"error", {{"code", code}, {"message", message}}}
    };
    std::string payload = resp.dump();
    transport.send(payload);
}

void sendLspNotification(LspTransport &transport, const std::string &method, const json &params) {
    json notif = {
        {"jsonrpc", "2.0"},
        {"method", method},
        {"params", params}
    };
    std::string payload = notif.dump();
    transport.send(payload);
}

void sendLspRequest(LspTransport &transport, const std::string &method, const json &params) {
    static int requestId = 0;
    json req = {
        {"jsonrpc", "2.0"},
        {"id", ++requestId},
        {"method", method},
        {"params", params}
    };
    std::string payload = req.dump();
    transport.send(payload);
}

// ── TCP server helpers ──

int createTcpLspServer(int port) {
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

    if (::bind(fd, reinterpret_cast<struct sockaddr *>(&addr), sizeof(addr)) < 0) {
        std::cerr << "Error: cannot bind to port " << port << ": " << std::strerror(errno) << "\n";
        ::close(fd);
        return -1;
    }

    if (::listen(fd, 1) < 0) {
        std::cerr << "Error: cannot listen on port " << port << ": " << std::strerror(errno) << "\n";
        ::close(fd);
        return -1;
    }

    std::cerr << "trust-lsp: waiting for connection on port " << port << "\n";
    return fd;
}

int acceptLspConnection(int serverFd) {
    struct sockaddr_in clientAddr{};
    socklen_t clientLen = sizeof(clientAddr);
    int clientFd = ::accept(serverFd, reinterpret_cast<struct sockaddr *>(&clientAddr), &clientLen);
    if (clientFd < 0) {
        std::cerr << "Error: accept failed: " << std::strerror(errno) << "\n";
        return -1;
    }

    char clientIP[INET_ADDRSTRLEN];
    ::inet_ntop(AF_INET, &clientAddr.sin_addr, clientIP, sizeof(clientIP));
    std::cerr << "trust-lsp: connection from " << clientIP << ":" << ntohs(clientAddr.sin_port) << "\n";

    return clientFd;
}

// ── CLI parsing ──

void printLspUsage(const char *prog) {
    std::cerr << "Usage: " << prog << " [options]\n"
              << "\n"
              << "LSP server for Trust language source mapping.\n"
              << "By default runs in interactive mode (stdin/stdout).\n"
              << "\n"
              << "Options:\n"
              << "  --help                  Show this help\n"
              << "  server[=<port>]         TCP server mode on given port (default: " << LSP_DEFAULT_PORT << ")\n"
              << "  --project-dir <path>    Project working directory (default: cwd)\n"
              << "  --trace                 Enable LSP protocol tracing\n";
}

LspOptions parseLspOptions(int argc, const char *argv[]) {
    LspOptions opts;

    for (int i = 1; i < argc; ++i) {
        if (std::strcmp(argv[i], "--help") == 0) {
            opts.help = true;
            return opts;
        }

        // server[=port] — TCP server mode
        if (std::strncmp(argv[i], "server", 6) == 0) {
            const char *eq = std::strchr(argv[i], '=');
            if (eq != nullptr) {
                opts.port = std::stoi(eq + 1);
            } else {
                opts.port = LSP_DEFAULT_PORT;
            }
            continue;
        }

        auto nextArg = [&]() -> std::string {
            if (++i >= argc) {
                std::cerr << "Error: " << argv[i - 1] << " requires an argument\n";
                std::exit(1);
            }
            return argv[i];
        };

        if (std::strcmp(argv[i], "--project-dir") == 0) {
            opts.projectDir = nextArg();
        } else if (std::strcmp(argv[i], "--trace") == 0) {
            opts.trace = true;
        } else {
            std::cerr << "Error: unknown option '" << argv[i] << "'\n";
            std::exit(1);
        }
    }

    return opts;
}