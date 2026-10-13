// src/playground/server_http.cpp
// PlaygroundServer: HTTP helpers: origin/CORS checks, client ip, cookies, escaping

#include "playground/server/server.h"
#include "playground/server/util.h"
#include "trust/version.h"
#include "utils/error.hpp"

#include <nlohmann/json.hpp>

#include <arpa/inet.h>
#include <cerrno>
#include <cstring>
#include <netinet/in.h>
#include <sys/socket.h>
#include <unistd.h>

#include <algorithm>
#include <chrono>
#include <cstdint>
#include <cstdio>
#include <cstdlib>
#include <ctime>
#include <sstream>
#include <string>
#include <thread>

namespace trust {
namespace playground {

static bool isLoopbackOrigin(const std::string& origin) {
    const size_t scheme = origin.find("://");
    if (scheme == std::string::npos) {
        return false;
    }
    std::string hostport = origin.substr(scheme + 3);
    const size_t slash = hostport.find('/');
    if (slash != std::string::npos) {
        hostport = hostport.substr(0, slash);
    }
    std::string host = hostport;
    const size_t colon = hostport.rfind(':');
    if (colon != std::string::npos) {
        host = hostport.substr(0, colon);
    }
    if (host.size() >= 2 && host.front() == '[' && host.back() == ']') {
        host = host.substr(1, host.size() - 2);
    }
    if (host == "localhost" || host == "::1") {
        return true;
    }
    return host.rfind("127.", 0) == 0;
}

bool PlaygroundServer::browserOriginAllowed(const HttpRequest& req) const {
    if (!cfg_.allowedHosts.empty()) {
        std::string hostname = req.host;
        const size_t colon = hostname.find(':');
        if (colon != std::string::npos) {
            hostname = hostname.substr(0, colon);
        }
        bool ok = false;
        for (const std::string& h : cfg_.allowedHosts) {
            if (req.host == h || hostname == h) {
                ok = true;
                break;
            }
        }
        if (!ok) {
            return false;
        }
    }
    if (!cfg_.allowedOrigins.empty()) {
        bool ok = false;
        for (const std::string& o : cfg_.allowedOrigins) {
            if (req.origin == o) {
                ok = true;
                break;
            }
        }
        if (!ok) {
            return false;
        }
    } else if (!req.origin.empty() && !isLoopbackOrigin(req.origin)) {
        // allowed_origins не заданы: fail-closed. Запросы с посторонним браузерным Origin
        // отклоняем (403). Запросы БЕЗ Origin (curl/скрипты) пропускаем - их ограничивают
        // PoW/rate-limit.
        return false;
    }
    return true;
}

std::string PlaygroundServer::corsOriginFor(const HttpRequest& req) const {
    if (!cfg_.allowedOrigins.empty()) {
        for (const std::string& o : cfg_.allowedOrigins) {
            if (req.origin == o) {
                return o;
            }
        }
        return std::string();
    }
    // allowed_origins не заданы: НИКОГДА не отдаём '*'. CORS разрешаем только loopback-origin
    // (локальная разработка); для посторонних origin ACAO не выводим -> браузер блокирует
    // кросс-доменное чтение. Прод запрещает оставлять этот список пустым (см. предупреждение
    // при запуске).
    return isLoopbackOrigin(req.origin) ? req.origin : std::string();
}

std::string PlaygroundServer::effectiveClientIp(const HttpRequest& req, const std::string& peer_ip) const {
    // X-Forwarded-For доверяем ТОЛЬКО когда peer - loopback (за nginx). С внешнего адреса
    // XFF подделывается, и rate-limit обходился бы.
    const bool loopback = (peer_ip == "::1") || (peer_ip.size() >= 4 && peer_ip.compare(0, 4, "127.") == 0);
    if (loopback) {
        const std::string& xff = req.xForwardedFor;
        if (!xff.empty()) {
            const size_t comma = xff.find(',');
            const std::string first = (comma == std::string::npos) ? xff : xff.substr(0, comma);
            const size_t b = first.find_first_not_of(" \t");
            const std::string ip = (b == std::string::npos) ? std::string() : first.substr(b);
            if (!ip.empty()) {
                return ip;
            }
        }
        if (!req.xRealIp.empty()) {
            return req.xRealIp;
        }
    }
    return peer_ip;
}

int PlaygroundServer::currentPowDifficulty() {
    if (cfg_.powMinDifficulty <= 0) {
        return 0;
    }
    // Адаптивная надбавка к сложности по глубине очереди/полёта (под нагрузкой - выше).
    std::lock_guard<std::mutex> lock(mu_);
    int diff = cfg_.powMinDifficulty;
    const size_t load = queue_.size() + inFlight_.size();
    const size_t cap = static_cast<size_t>(cfg_.maxQueue);
    if (cap > 0 && load >= cap) {
        diff += 2;
    } else if (cap > 0 && load * 2 >= cap) {
        diff += 1;
    }
    if (diff > cfg_.powMaxDifficulty) {
        diff = cfg_.powMaxDifficulty;
    }
    return diff;
}

std::string PlaygroundServer::cookieSessionId(const HttpRequest& req) {
    // Cookie: a=b; tpg_stats=<id>; ...
    size_t pos = req.cookie.find("tpg_stats=");
    if (pos == std::string::npos) {
        return std::string();
    }
    pos += std::strlen("tpg_stats=");
    const size_t end = req.cookie.find(';', pos);
    const std::string val = (end == std::string::npos) ? req.cookie.substr(pos) : req.cookie.substr(pos, end - pos);
    const size_t b = val.find_first_not_of(" \t");
    return (b == std::string::npos) ? std::string() : val.substr(b);
}

std::string PlaygroundServer::htmlEscape(const std::string& s) {
    std::string out;
    out.reserve(s.size());
    for (const char c : s) {
        switch (c) {
        case '&':
            out += "&amp;";
            break;
        case '<':
            out += "&lt;";
            break;
        case '>':
            out += "&gt;";
            break;
        case '"':
            out += "&quot;";
            break;
        case '\'':
            out += "&#39;";
            break;
        default:
            out += c;
            break;
        }
    }
    return out;
}

std::string PlaygroundServer::sanitizeFilename(const std::string& s) {
    std::string out;
    out.reserve(s.size());
    for (const char c : s) {
        const unsigned char uc = static_cast<unsigned char>(c);
        if ((uc >= 'a' && uc <= 'z') || (uc >= 'A' && uc <= 'Z') || (uc >= '0' && uc <= '9') || c == '.' || c == '_' || c == '-') {
            out += c;
        }
    }
    return out;
}

namespace {

// true, если Origin вида "scheme://host[:port]" указывает на loopback (localhost/127.x/::1).
// Используется для fail-closed CORS-дефолта: без явного allowed_origins разрешаем CORS
// только локальной разработке.

} // namespace

} // namespace playground
} // namespace trust
