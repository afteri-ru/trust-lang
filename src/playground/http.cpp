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
#include <cstdint>
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
                // Прервано сигналом (например, Ctrl+C/SIGTERM при остановке воркера):
                // завершаем чтение, чтобы слот-поток вышел из цикла и увидел stop_.
                return false;
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

// Пишет весь буфер в fd (петля - защита от частичной записи большого тела).
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

    // Headers (до \r\n\r\n); для GET без тела - до конца raw.
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
        const size_t colon = header.find(':');
        const std::string name = (colon == std::string::npos) ? header : header.substr(0, colon);
        std::string value;
        if (colon != std::string::npos) {
            value = header.substr(colon + 1);
            const size_t vb = value.find_first_not_of(" \t");
            value = (vb == std::string::npos) ? std::string() : value.substr(vb);
        }
        // Сравнение имени заголовка без учёта регистра.
        const auto nameEq = [&](const char* n) -> bool {
            const size_t nl = std::strlen(n);
            if (name.size() != nl) {
                return false;
            }
            for (size_t i = 0; i < nl; ++i) {
                if (std::tolower(static_cast<unsigned char>(name[i])) != static_cast<unsigned char>(n[i])) {
                    return false;
                }
            }
            return true;
        };
        if (trust::transport::isContentLength(header)) {
            content_length = trust::transport::parseContentLength(header);
        } else if (nameEq("host")) {
            out.host = value;
        } else if (nameEq("origin")) {
            out.origin = value;
        } else if (nameEq("referer")) {
            out.referer = value;
        } else if (nameEq("x-forwarded-for")) {
            out.xForwardedFor = value;
        } else if (nameEq("x-real-ip")) {
            out.xRealIp = value;
        } else if (nameEq("x-stats-token")) {
            out.statsTokenHdr = value;
        } else if (nameEq("x-pow")) {
            out.xPow = value;
        } else if (nameEq("cookie")) {
            out.cookie = value;
        } else if (nameEq("x-example-name")) {
            out.exampleName = value;
        }
        pos = eol + 2;
    }

    // Body - content_length байт после заголовков.
    const size_t body_start = (headers_end == raw.size()) ? raw.size() : headers_end + 4;
    out.body = raw.substr(body_start, static_cast<size_t>(content_length));
    return true;
}

HttpResponse handleHttpRequest(const HttpRequest& req, const LspOptions& opts) {
    HttpResponse resp;
    resp.cors = true; // все эндпоинты этого сервера (/run, /health, preflight) - браузерные

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
    case 302:
        return "Found";
    case 400:
        return "Bad Request";
    case 402:
        return "Payment Required";
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
        // Access-Control-Allow-Origin НИКОГДА не '*': если конкретный origin не задан
        // (не совпал с allowlist или не loopback), ACAO не выводим - браузер блокирует
        // кросс-доменное чтение ответа с посторонних сайтов.
        if (!r.corsOrigin.empty()) {
            out += "Access-Control-Allow-Origin: " + r.corsOrigin + "\r\n";
        }
        out += "Access-Control-Allow-Methods: GET, POST, OPTIONS\r\n";
        // Кастомные заголовки фронтенда должны быть перечислены, иначе браузер отклонит
        // CORS-preflight (OPTIONS из-за X-Example-Name) и fetch упадёт - страница покажет
        // «Нет связи с балансировщиком», хотя /health (простой GET) будет «онлайн».
        out += "Access-Control-Allow-Headers: Content-Type, X-Example-Name, X-Stats-Token\r\n";
        // Без Expose-Headers браузер не отдаёт JS не-simple заголовки (Content-Disposition),
        // из-за чего glue-JS не мог бы узнать имя скачиваемого файла.
        out += "Access-Control-Expose-Headers: Content-Disposition\r\n";
        out += "Access-Control-Max-Age: 86400\r\n";
    }
    out += "Content-Type: " + r.content_type + "\r\n";
    if (!r.content_disposition.empty()) {
        out += "Content-Disposition: attachment; filename=\"" + r.content_disposition + "\"\r\n";
    }
    if (!r.location.empty()) {
        out += "Location: " + r.location + "\r\n";
    }
    for (const std::string& h : r.extraHeaders) {
        out += h + "\r\n";
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

bool constantTimeEqual(const std::string& a, const std::string& b) {
    if (a.size() != b.size()) {
        return false;
    }
    unsigned char diff = 0;
    for (size_t i = 0; i < a.size(); ++i) {
        diff |= static_cast<unsigned char>(a[i] ^ b[i]);
    }
    return diff == 0;
}

std::string sha256Hex(const std::string& data) {
    // FIPS 180-4 SHA-256, самодостаточная реализация (без внешних зависимостей).
    static constexpr uint32_t kK[64] = {0x428a2f98, 0x71374491, 0xb5c0fbcf, 0xe9b5dba5, 0x3956c25b, 0x59f111f1, 0x923f82a4, 0xab1c5ed5, 0xd807aa98, 0x12835b01,
                                        0x243185be, 0x550c7dc3, 0x72be5d74, 0x80deb1fe, 0x9bdc06a7, 0xc19bf174, 0xe49b69c1, 0xefbe4786, 0x0fc19dc6, 0x240ca1cc,
                                        0x2de92c6f, 0x4a7484aa, 0x5cb0a9dc, 0x76f988da, 0x983e5152, 0xa831c66d, 0xb00327c8, 0xbf597fc7, 0xc6e00bf3, 0xd5a79147,
                                        0x06ca6351, 0x14292967, 0x27b70a85, 0x2e1b2138, 0x4d2c6dfc, 0x53380d13, 0x650a7354, 0x766a0abb, 0x81c2c92e, 0x92722c85,
                                        0xa2bfe8a1, 0xa81a664b, 0xc24b8b70, 0xc76c51a3, 0xd192e819, 0xd6990624, 0xf40e3585, 0x106aa070, 0x19a4c116, 0x1e376c08,
                                        0x2748774c, 0x34b0bcb5, 0x391c0cb3, 0x4ed8aa4a, 0x5b9cca4f, 0x682e6ff3, 0x748f82ee, 0x78a5636f, 0x84c87814, 0x8cc70208,
                                        0x90befffa, 0xa4506ceb, 0xbef9a3f7, 0xc67178f2};

    uint32_t h[8] = {0x6a09e667, 0xbb67ae85, 0x3c6ef372, 0xa54ff53a, 0x510e527f, 0x9b05688c, 0x1f83d9ab, 0x5be0cd19};

    const auto process = [&](const uint8_t* block) {
        uint32_t w[64];
        for (int i = 0; i < 16; ++i) {
            w[i] = (static_cast<uint32_t>(block[i * 4]) << 24) | (static_cast<uint32_t>(block[i * 4 + 1]) << 16) |
                   (static_cast<uint32_t>(block[i * 4 + 2]) << 8) | static_cast<uint32_t>(block[i * 4 + 3]);
        }
        for (int i = 16; i < 64; ++i) {
            const uint32_t s0 = ((w[i - 15] >> 7) | (w[i - 15] << 25)) ^ ((w[i - 15] >> 18) | (w[i - 15] << 14)) ^ (w[i - 15] >> 3);
            const uint32_t s1 = ((w[i - 2] >> 17) | (w[i - 2] << 15)) ^ ((w[i - 2] >> 19) | (w[i - 2] << 13)) ^ (w[i - 2] >> 10);
            w[i] = w[i - 16] + s0 + w[i - 7] + s1;
        }
        uint32_t a = h[0], b = h[1], c = h[2], d = h[3], e = h[4], f = h[5], g = h[6], hh = h[7];
        for (int i = 0; i < 64; ++i) {
            const uint32_t S1 = ((e >> 6) | (e << 26)) ^ ((e >> 11) | (e << 21)) ^ ((e >> 25) | (e << 7));
            const uint32_t ch = (e & f) ^ (~e & g);
            const uint32_t t1 = hh + S1 + ch + kK[i] + w[i];
            const uint32_t S0 = ((a >> 2) | (a << 30)) ^ ((a >> 13) | (a << 19)) ^ ((a >> 22) | (a << 10));
            const uint32_t maj = (a & b) ^ (a & c) ^ (b & c);
            const uint32_t t2 = S0 + maj;
            hh = g;
            g = f;
            f = e;
            e = d + t1;
            d = c;
            c = b;
            b = a;
            a = t1 + t2;
        }
        h[0] += a;
        h[1] += b;
        h[2] += c;
        h[3] += d;
        h[4] += e;
        h[5] += f;
        h[6] += g;
        h[7] += hh;
    };

    const uint8_t* p = reinterpret_cast<const uint8_t*>(data.data());
    size_t off = 0;
    while (off + 64 <= data.size()) {
        process(p + off);
        off += 64;
    }

    // Padding (один или два блока).
    uint8_t tail[128];
    const size_t rem = data.size() - off;
    if (rem > 0) {
        std::memcpy(tail, p + off, rem);
    }
    tail[rem] = 0x80;
    const size_t total_pad = (rem < 56) ? 64 : 128;
    for (size_t i = rem + 1; i + 8 <= total_pad; ++i) {
        tail[i] = 0;
    }
    const uint64_t bits = static_cast<uint64_t>(data.size()) * 8;
    for (int i = 0; i < 8; ++i) {
        tail[total_pad - 8 + i] = static_cast<uint8_t>(bits >> (56 - 8 * i));
    }
    for (size_t b = 0; b < total_pad; b += 64) {
        process(tail + b);
    }

    static constexpr const char* kHex = "0123456789abcdef";
    std::string out;
    out.reserve(64);
    for (int i = 0; i < 8; ++i) {
        for (int k = 3; k >= 0; --k) {
            const uint8_t byte = static_cast<uint8_t>(h[i] >> (8 * k));
            out += kHex[byte >> 4];
            out += kHex[byte & 0x0f];
        }
    }
    return out;
}

} // namespace trust::playground
