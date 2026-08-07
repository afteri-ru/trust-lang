// src/playground/server.cpp
// trust-playground: HTTP-ядро сервера playground (без сокетов, юнит-тестируемо).
// POST /run (Trust-код) → transpileToResult() → resultToJson() (контракт lsp/HTML.md).
// Также HTTP-клиент (httpPost) для воркера и интеграционных тестов.

#include "playground/http.h"

#include "lsp/html_emit.h"
#include "utils/transport.hpp"

#include <arpa/inet.h>
#include <cerrno>
#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <netinet/in.h>
#include <poll.h>
#include <sys/socket.h>
#include <unistd.h>

#include <chrono>
#include <cctype>
#include <string>
#include <vector>

namespace trust::playground {

using trust::lsp::HtmlResult;
using trust::lsp::resultToJson;
using trust::lsp::transpileToResult;

namespace {

struct UrlParts {
    std::string scheme;
    std::string host;
    int port = 0;
    std::string path;
};

bool parseUrl(const std::string& url, UrlParts& out) {
    const size_t scheme_end = url.find("://");
    if (scheme_end == std::string::npos) {
        return false;
    }
    out.scheme = url.substr(0, scheme_end);
    const std::string rest = url.substr(scheme_end + 3);
    const size_t slash = rest.find('/');
    const std::string hostport = (slash == std::string::npos) ? rest : rest.substr(0, slash);
    out.path = (slash == std::string::npos) ? "/" : rest.substr(slash);

    const size_t colon = hostport.rfind(':');
    if (colon != std::string::npos) {
        out.host = hostport.substr(0, colon);
        out.port = std::stoi(hostport.substr(colon + 1));
    } else {
        out.host = hostport;
        out.port = (out.scheme == "https") ? 443 : 80;
    }
    return !out.host.empty();
}

bool readWithTimeout(int fd, std::string& buf, int timeout_ms) {
    char tmp[8192];
    struct pollfd p{fd, POLLIN, 0};
    const auto deadline = std::chrono::steady_clock::now() + std::chrono::milliseconds(timeout_ms);
    while (true) {
        const auto remain = std::chrono::duration_cast<std::chrono::milliseconds>(deadline - std::chrono::steady_clock::now());
        if (remain.count() <= 0) {
            return true;
        }
        const int pr = ::poll(&p, 1, static_cast<int>(remain.count()));
        if (pr < 0) {
            if (errno == EINTR) {
                continue;
            }
            return false;
        }
        if (pr == 0) {
            return true;
        }
        const ssize_t n = ::read(fd, tmp, sizeof(tmp));
        if (n > 0) {
            buf.append(tmp, static_cast<size_t>(n));
        } else {
            return false;
        }
    }
}

// Пишет весь буфер в fd (петля — защита от частичной записи большого тела).
bool writeAll(int fd, const std::string& data) {
    size_t off = 0;
    while (off < data.size()) {
        const ssize_t n = ::write(fd, data.data() + off, data.size() - off);
        if (n <= 0) {
            if (n < 0 && errno == EINTR) {
                continue;
            }
            return false;
        }
        off += static_cast<size_t>(n);
    }
    return true;
}

} // namespace

bool parseHttpRequest(const std::string& raw, HttpRequest& out) {
    if (raw.empty()) {
        return false;
    }

    // Request line: METHOD TARGET HTTP/1.1
    const size_t line_end = raw.find("\r\n");
    if (line_end == std::string::npos) {
        return false;
    }
    const std::string request_line = raw.substr(0, line_end);
    const size_t sp1 = request_line.find(' ');
    if (sp1 == std::string::npos) {
        return false;
    }
    const size_t sp2 = request_line.find(' ', sp1 + 1);
    if (sp2 == std::string::npos) {
        return false;
    }
    out.method = request_line.substr(0, sp1);
    out.target = request_line.substr(sp1 + 1, sp2 - sp1 - 1);

    // Headers (до \r\n\r\n); для GET без тела — до конца raw.
    const size_t headers_start = line_end + 2;
    size_t headers_end = raw.find("\r\n\r\n", headers_start);
    if (headers_end == std::string::npos) {
        headers_end = raw.size();
    }

    int content_length = 0;
    size_t pos = headers_start;
    while (pos < headers_end) {
        size_t eol = raw.find("\r\n", pos);
        if (eol == std::string::npos || eol > headers_end) {
            eol = headers_end;
        }
        const std::string header = raw.substr(pos, eol - pos);
        if (trust::transport::isContentLength(header)) {
            content_length = trust::transport::parseContentLength(header);
        }
        pos = eol + 2;
    }

    // Body — content_length байт после заголовков.
    const size_t body_start = (headers_end == raw.size()) ? raw.size() : headers_end + 4;
    out.body = raw.substr(body_start, static_cast<size_t>(content_length));
    return true;
}

HttpResponse handleHttpRequest(const HttpRequest& req, const LspOptions& opts) {
    HttpResponse resp;
    resp.cors = true; // все эндпоинты этого сервера (/run, /health, preflight) — браузерные

    if (req.method == "OPTIONS") {
        // CORS preflight.
        resp.status = 204;
        resp.content_type = "";
        resp.body = "";
        return resp;
    }

    if (req.method == "GET" && (req.target == "/health" || req.target == "/health/")) {
        resp.status = 200;
        resp.content_type = "application/json; charset=utf-8";
        resp.body = "{\"status\":\"ok\"}";
        return resp;
    }

    if (req.method == "POST" && req.target == "/run") {
        if (req.body.empty()) {
            resp.status = 400;
            resp.content_type = "application/json; charset=utf-8";
            resp.body = "{\"error\":\"empty request body\"}";
            return resp;
        }
        const HtmlResult result = transpileToResult(req.body, "playground.src", opts);
        resp.status = 200;
        resp.content_type = "application/json; charset=utf-8";
        resp.body = resultToJson(result);
        return resp;
    }

    resp.status = 404;
    resp.content_type = "application/json; charset=utf-8";
    resp.body = "{\"error\":\"not found\"}";
    return resp;
}

static const char* statusText(int status) {
    switch (status) {
    case 200:
        return "OK";
    case 204:
        return "No Content";
    case 400:
        return "Bad Request";
    case 404:
        return "Not Found";
    case 500:
        return "Internal Server Error";
    default:
        return "OK";
    }
}

std::string serializeHttpResponse(const HttpResponse& r) {
    std::string out;
    out += "HTTP/1.1 " + std::to_string(r.status) + " " + statusText(r.status) + "\r\n";
    if (r.cors) {
        out += "Access-Control-Allow-Origin: *\r\n";
        out += "Access-Control-Allow-Methods: GET, POST, OPTIONS\r\n";
        out += "Access-Control-Allow-Headers: Content-Type\r\n";
        // Без Expose-Headers браузер не отдаёт JS не-simple заголовки (Content-Disposition),
        // из-за чего glue-JS не мог бы узнать имя скачиваемого файла.
        out += "Access-Control-Expose-Headers: Content-Disposition\r\n";
        out += "Access-Control-Max-Age: 86400\r\n";
    }
    out += "Content-Type: " + r.content_type + "\r\n";
    if (!r.content_disposition.empty()) {
        out += "Content-Disposition: attachment; filename=\"" + r.content_disposition + "\"\r\n";
    }
    out += "Content-Length: " + std::to_string(r.body.size()) + "\r\n";
    out += "Connection: close\r\n";
    out += "\r\n";
    out += r.body;
    return out;
}

HttpResult rawHttpPost(const UrlParts& url, const std::string& body, int timeout_sec) {
    HttpResult res;
    const int fd = ::socket(AF_INET, SOCK_STREAM, 0);
    if (fd < 0) {
        return res;
    }
    struct sockaddr_in addr{};
    addr.sin_family = AF_INET;
    addr.sin_port = htons(static_cast<uint16_t>(url.port));
    if (::inet_pton(AF_INET, url.host.c_str(), &addr.sin_addr) <= 0) {
        ::close(fd);
        return res;
    }
    if (::connect(fd, reinterpret_cast<struct sockaddr*>(&addr), sizeof(addr)) < 0) {
        ::close(fd);
        return res;
    }

    std::string request = "POST " + url.path + " HTTP/1.1\r\n";
    request += "Host: " + url.host + "\r\n";
    request += "Content-Type: application/json; charset=utf-8\r\n";
    request += "Content-Length: " + std::to_string(body.size()) + "\r\n";
    request += "Connection: close\r\n\r\n";
    request += body;

    if (!writeAll(fd, request)) {
        ::close(fd);
        return res;
    }

    std::string raw;
    readWithTimeout(fd, raw, timeout_sec * 1000);
    ::close(fd);

    const size_t status_pos = raw.find(' ');
    if (raw.rfind("HTTP/1.", 0) != 0 || status_pos == std::string::npos) {
        return res;
    }
    res.status = std::atoi(raw.c_str() + status_pos + 1);
    const size_t hdr_end = raw.find("\r\n\r\n");
    if (hdr_end == std::string::npos) {
        return res;
    }
    res.body = raw.substr(hdr_end + 4);
    return res;
}

// Отправляет POST через curl (для https). Возвращает результат; при неудаче status=0.
HttpResult curlHttpPost(const UrlParts& url, const std::string& body, int timeout_sec) {
    HttpResult res;
    char body_path[] = "/tmp/trust-worker-body-XXXXXX";
    char out_path[] = "/tmp/trust-worker-out-XXXXXX";
    const int bfd = ::mkstemp(body_path);
    const int ofd = ::mkstemp(out_path);
    if (bfd < 0 || ofd < 0) {
        if (bfd >= 0) {
            ::close(bfd);
        }
        if (ofd >= 0) {
            ::close(ofd);
        }
        return res;
    }
    ::write(bfd, body.data(), body.size());
    ::close(bfd);
    ::close(ofd);

    std::string cmd = "curl --silent --show-error --max-time " + std::to_string(timeout_sec) + " --data-binary @" + body_path + " --output " + out_path +
                      " --write-out '%{http_code}' " + url.scheme + "://" + url.host + ":" + std::to_string(url.port) + url.path + " 2>/dev/null";

    FILE* pipe = ::popen(cmd.c_str(), "r");
    if (pipe == nullptr) {
        ::unlink(body_path);
        ::unlink(out_path);
        return res;
    }
    char buf[64];
    std::string status_str;
    while (::fgets(buf, sizeof(buf), pipe) != nullptr) {
        status_str += buf;
    }
    const int pr = ::pclose(pipe);

    std::string out_body;
    FILE* ofile = ::fopen(out_path, "r");
    if (ofile != nullptr) {
        char rbuf[8192];
        size_t n;
        while ((n = ::fread(rbuf, 1, sizeof(rbuf), ofile)) > 0) {
            out_body.append(rbuf, n);
        }
        ::fclose(ofile);
    }
    ::unlink(body_path);
    ::unlink(out_path);

    if (pr != 0 || status_str.empty()) {
        return res;
    }
    res.status = std::atoi(status_str.c_str());
    res.body = out_body;
    return res;
}

HttpResult httpPost(const std::string& url, const std::string& body, int timeout_sec) {
    UrlParts parts;
    if (!parseUrl(url, parts)) {
        return HttpResult();
    }
    if (parts.scheme == "https") {
        return curlHttpPost(parts, body, timeout_sec);
    }
    return rawHttpPost(parts, body, timeout_sec);
}

std::string base64Encode(const std::string& data) {
    static constexpr const char* kTable = "ABCDEFGHIJKLMNOPQRSTUVWXYZabcdefghijklmnopqrstuvwxyz0123456789+/";
    std::string out;
    out.reserve(((data.size() + 2) / 3) * 4);
    size_t i = 0;
    while (i + 2 < data.size()) {
        const uint32_t v = (static_cast<uint8_t>(data[i]) << 16) | (static_cast<uint8_t>(data[i + 1]) << 8) | static_cast<uint8_t>(data[i + 2]);
        out += kTable[(v >> 18) & 0x3f];
        out += kTable[(v >> 12) & 0x3f];
        out += kTable[(v >> 6) & 0x3f];
        out += kTable[v & 0x3f];
        i += 3;
    }
    if (i + 1 == data.size()) {
        const uint32_t v = static_cast<uint8_t>(data[i]) << 16;
        out += kTable[(v >> 18) & 0x3f];
        out += kTable[(v >> 12) & 0x3f];
        out += "==";
    } else if (i + 2 == data.size()) {
        const uint32_t v = (static_cast<uint8_t>(data[i]) << 16) | (static_cast<uint8_t>(data[i + 1]) << 8);
        out += kTable[(v >> 18) & 0x3f];
        out += kTable[(v >> 12) & 0x3f];
        out += kTable[(v >> 6) & 0x3f];
        out += '=';
    }
    return out;
}

int base64Value(char c) {
    if (c >= 'A' && c <= 'Z') {
        return c - 'A';
    }
    if (c >= 'a' && c <= 'z') {
        return c - 'a' + 26;
    }
    if (c >= '0' && c <= '9') {
        return c - '0' + 52;
    }
    if (c == '+') {
        return 62;
    }
    if (c == '/') {
        return 63;
    }
    return -1;
}

std::string base64Decode(const std::string& data) {
    std::string out;
    uint32_t acc = 0;
    int bits = 0;
    for (const char c : data) {
        if (c == '=' || c == '\n' || c == '\r') {
            continue;
        }
        const int v = base64Value(c);
        if (v < 0) {
            continue;
        }
        acc = (acc << 6) | static_cast<uint32_t>(v);
        bits += 6;
        if (bits >= 8) {
            bits -= 8;
            out += static_cast<char>((acc >> bits) & 0xff);
        }
    }
    return out;
}

} // namespace trust::playground
