#include "debug/dap_transport.h"

// ── StdioTransport ──

std::string StdioTransport::readPacket() {
    int contentLength = 0;
    std::string line;
    while (std::getline(std::cin, line)) {
        if (!line.empty() && line.back() == '\r') {
            line.pop_back();
        }
        if (line.empty()) {
            break;
        }
        if (line.rfind("Content-Length:", 0) == 0) {
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

void StdioTransport::send(const std::string &payload) {
    std::cout << "Content-Length: " << payload.size() << "\r\n\r\n" << payload << std::flush;
}

// ── TcpTransport ──

std::string TcpTransport::readPacket() {
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
            if (line.rfind("Content-Length:", 0) == 0) {
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

void TcpTransport::send(const std::string &payload) {
    std::string header = "Content-Length: " + std::to_string(payload.size()) + "\r\n\r\n";
    std::string full = header + payload;
    ::write(fd_, full.data(), full.size());
}

// ── DAP Protocol helpers ──

static int dapSeq = 0;

int nextDapSeq() {
    return ++dapSeq;
}

using json = nlohmann::json;

json readDapPacket(DapTransport &transport) {
    std::string body = transport.readPacket();
    if (body.empty()) {
        return json();
    }

    try {
        return json::parse(body);
    } catch (const json::parse_error &e) {
        std::cerr << "JSON parse error: " << e.what() << "\n"
                  << "Raw input: " << body << "\n";
        return json();
    }
}

void sendDapResponse(DapTransport &transport, int requestSeq, const json &body, bool success) {
    json resp = {
        {"type", "response"},
        {"seq", nextDapSeq()},
        {"request_seq", requestSeq},
        {"command", body.value("command", "")},
        {"success", success}
    };
    if (body.contains("body")) {
        resp["body"] = body["body"];
    }
    if (!success) {
        json msg = body.contains("message") ? body["message"] : json("request failed");
        resp["message"] = msg;
    }
    std::string payload = resp.dump();
    transport.send(payload);
}

void sendDapEvent(DapTransport &transport, const std::string &eventName, const json &body) {
    json evt = {{"type", "event"}, {"event", eventName}, {"seq", nextDapSeq()}};
    // Всегда добавляем body, даже пустой — DAP spec этого не запрещает,
    // а VSCode ожидает наличие поля
    evt["body"] = body;
    std::string payload = evt.dump();
    transport.send(payload);
}

void sendDapOutput(DapTransport &transport, const std::string &category, const std::string &output) {
    sendDapEvent(transport, "output", {{"category", category}, {"output", output}});
}

void sendBreakpointEvent(DapTransport &transport, const std::string &srcPath, int line, int bpId, bool verified) {
    sendDapEvent(transport, "breakpoint", {{"reason", verified ? "changed" : "new"},
                                           {"breakpoint", {{"id", bpId}, {"verified", verified}, {"source", {{"path", srcPath}}}, {"line", line}}}});
}

// ── TCP server helpers ──

int createTcpServer(int port) {
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

    std::cerr << "trust-dap: waiting for connection on port " << port << "\n";
    return fd;
}

int acceptConnection(int serverFd) {
    struct sockaddr_in clientAddr{};
    socklen_t clientLen = sizeof(clientAddr);
    int clientFd = ::accept(serverFd, reinterpret_cast<struct sockaddr *>(&clientAddr), &clientLen);
    if (clientFd < 0) {
        std::cerr << "Error: accept failed: " << std::strerror(errno) << "\n";
        return -1;
    }

    char clientIP[INET_ADDRSTRLEN];
    ::inet_ntop(AF_INET, &clientAddr.sin_addr, clientIP, sizeof(clientIP));
    std::cerr << "trust-dap: connection from " << clientIP << ":" << ntohs(clientAddr.sin_port) << "\n";

    return clientFd;
}

// ── CLI parsing ──

void printUsage(const char *prog) {
    std::cerr << "Usage: " << prog << " [options]\n"
              << "\n"
              << "DAP server for debugging Trust language programs.\n"
              << "By default runs in interactive mode (stdin/stdout).\n"
              << "\n"
              << "Options:\n"
              << "  --help                  Show this help\n"
              << "  server[=<port>]         TCP server mode on given port (default: " << DAP_DEFAULT_PORT << ")\n"
              << "  --project-dir <path>    Project working directory (default: cwd)\n"
              << "  --lldb-server <path>    Path to lldb-server (optional)\n";
}

DapOptions parseDapOptions(int argc, const char *argv[]) {
    DapOptions opts;

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
                opts.port = DAP_DEFAULT_PORT;
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
        } else if (std::strcmp(argv[i], "--lldb-server") == 0) {
            opts.lldbServerPath = nextArg();
        } else {
            std::cerr << "Error: unknown option '" << argv[i] << "'\n";
            std::exit(1);
        }
    }

    return opts;
}