// src/playground/server_stats.cpp
// PlaygroundServer: admin statistics, login/logout, PoW challenge and stats rendering

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

// Встроенные HTML-страницы (login / 403 / шаблон stats) вынесены во внешние файлы через
// #embed (co-location - файлы лежат рядом в src/playground/). Это позволяет редактировать
// HTML/CSS без C++-экранирования и тестировать независимо.
static const unsigned char kLoginHtmlBytes[] = {
#embed "login.html"
};
static const std::string kLoginHtml(reinterpret_cast<const char*>(kLoginHtmlBytes), sizeof(kLoginHtmlBytes));

static const unsigned char kError403HtmlBytes[] = {
#embed "error403.html"
};
static const std::string kError403Html(reinterpret_cast<const char*>(kError403HtmlBytes), sizeof(kError403HtmlBytes));

// Шаблон stats-страницы; плейсхолдеры %%TIME%% и %%BODY%% заменяются в buildStatsHtmlLocked.
static const unsigned char kStatsPageBytes[] = {
#embed "stats_page.html"
};
static const std::string kStatsPage(reinterpret_cast<const char*>(kStatsPageBytes), sizeof(kStatsPageBytes));

namespace trust {
namespace playground {

// Простое URL-декодирование ("%XX" -> байт, '+' -> пробел). Для токенов доступа.
static std::string urlDecode(const std::string& s) {
    std::string out;
    out.reserve(s.size());
    for (size_t i = 0; i < s.size(); ++i) {
        const char c = s[i];
        if (c == '%' && i + 2 < s.size()) {
            auto hex = [](char h) -> int {
                if (h >= '0' && h <= '9') {
                    return h - '0';
                }
                if (h >= 'a' && h <= 'f') {
                    return h - 'a' + 10;
                }
                if (h >= 'A' && h <= 'F') {
                    return h - 'A' + 10;
                }
                return -1;
            };
            const int hi = hex(s[i + 1]);
            const int lo = hex(s[i + 2]);
            if (hi >= 0 && lo >= 0) {
                out += static_cast<char>((hi << 4) | lo);
                i += 2;
                continue;
            }
        }
        out += (c == '+') ? ' ' : c;
    }
    return out;
}

HttpResponse PlaygroundServer::handleStats(const HttpRequest& req) {
    HttpResponse resp;
    const bool want_html = req.target.find("format=html") != std::string::npos;

    // Аутентификация: по заголовку X-Stats-Token (скрипты/API) ИЛИ по cookie-сессии
    // (браузерная админ-страница). Токен НЕ передаётся в query-строке (без fallback).
    bool authorized = false;
    if (!cfg_.statsToken.empty()) {
        if (trust::playground::constantTimeEqual(req.statsTokenHdr, cfg_.statsToken)) {
            authorized = true;
        }
        if (!authorized) {
            authorized = statsSessions_.ok(cookieSessionId(req), cfg_.statsSessionTtlSec, cfg_.statsSessionMaxSec);
        }
    }
    if (!authorized) {
        resp.content_type = "application/json; charset=utf-8";
        resp.status = 403;
        resp.body = "{\"error\":\"forbidden\"}";
        return resp;
    }

    resp.status = 200;
    if (want_html) {
        resp.content_type = "text/html; charset=utf-8";
        resp.body = currentStatsHtml();
    } else {
        resp.content_type = "application/json; charset=utf-8";
        resp.body = currentStatsJson();
    }
    return resp;
}

HttpResponse PlaygroundServer::handleChallenge(const HttpRequest& req) {
    HttpResponse resp;
    resp.content_type = "application/json; charset=utf-8";
    resp.cors = true;
    resp.corsOrigin = corsOriginFor(req);
    if (cfg_.powMinDifficulty <= 0) {
        // PoW выключен: челлендж не требуется.
        nlohmann::json j{{"ok", true}, {"required", false}};
        resp.status = 200;
        resp.body = j.dump();
        return resp;
    }
    const int diff = currentPowDifficulty();
    const std::string nonce = powGuard_.issue(diff, cfg_.powNonceTtlSec);
    nlohmann::json j{{"ok", true}, {"required", true}, {"nonce", nonce}, {"difficulty", diff}, {"ttl_sec", cfg_.powNonceTtlSec}};
    resp.status = 200;
    resp.body = j.dump();
    return resp;
}

HttpResponse PlaygroundServer::handleStatsLogin(const HttpRequest& req) {
    HttpResponse resp;
    resp.content_type = "text/html; charset=utf-8";
    if (req.method == "GET") {
        // Форма входа (токен доступа к статистике).
        resp.status = 200;
        resp.body = kLoginHtml;
        return resp;
    }
    // POST: принимаем токен из формы (application/x-www-form-urlencoded "token=<value>")
    // или из JSON-тела {"token": "..."}.
    std::string token;
    try {
        const auto j = nlohmann::json::parse(req.body);
        token = j.value("token", std::string());
    } catch (const std::exception&) {
        if (req.body.rfind("token=", 0) == 0) {
            token = req.body.substr(std::strlen("token="));
            const size_t amp = token.find('&');
            if (amp != std::string::npos) {
                token = token.substr(0, amp);
            }
        } else {
            token = req.body;
        }
    }
    // Простое URL-декодирование значения (формы кодируют "+"/"%XX"; токен - hex, но
    // на всякий случай).
    token = urlDecode(token);
    const size_t b = token.find_first_not_of(" \t\r\n");
    token = (b == std::string::npos) ? std::string() : token.substr(b);
    const size_t e = token.find_last_not_of(" \t\r\n");
    if (!token.empty() && e != std::string::npos) {
        token = token.substr(0, e + 1);
    }
    if (cfg_.statsToken.empty() || !trust::playground::constantTimeEqual(token, cfg_.statsToken)) {
        resp.status = 403;
        resp.body = kError403Html;
        return resp;
    }
    const std::string session_id = statsSessions_.create();
    const int ttl = cfg_.statsSessionTtlSec > 0 ? cfg_.statsSessionTtlSec : 600;
    resp.status = 302;
    resp.location = "/stats?format=html";
    resp.extraHeaders.push_back("Set-Cookie: tpg_stats=" + session_id + "; HttpOnly; Path=/stats; SameSite=Strict; Max-Age=" + std::to_string(ttl));
    resp.body = "";
    return resp;
}

HttpResponse PlaygroundServer::handleStatsLogout(const HttpRequest& req) {
    HttpResponse resp;
    resp.content_type = "text/html; charset=utf-8";
    const std::string sid = cookieSessionId(req);
    statsSessions_.destroy(sid);
    resp.status = 302;
    resp.location = "/stats/login";
    resp.extraHeaders.push_back("Set-Cookie: tpg_stats=; HttpOnly; Path=/stats; SameSite=Strict; Max-Age=0");
    resp.body = "";
    return resp;
}

// Единая точка построения данных статистики (используется /stats, /stats?format=html и
// письмами). Требует удержания mu_.
nlohmann::json PlaygroundServer::statsJsonLocked() {
    const auto now = std::chrono::steady_clock::now();
    nlohmann::json out;
    out["balancer"]["workers_known"] = static_cast<int>(workers_.size());
    out["balancer"]["queue"] = static_cast<int>(queue_.size());
    out["balancer"]["in_flight"] = static_cast<int>(inFlight_.size());
    out["balancer"]["max_queue"] = cfg_.maxQueue;
    out["balancer"]["rate_limit_ips_tracked"] = static_cast<int>(rateLimiter_.trackedCount());
    out["balancer"]["rate_limit_resets"] = rateLimiter_.resets();
    out["balancer"]["archives_requested"] = archivesRequested_.load();
    // Соединения: текущее занято / ПИК утилизации (и % от лимита) по глобальному и
    // раздельным (клиент/воркер) пулам. Сам лимит известен из конфига.
    const auto pct = [](int used, int limit) -> int { return limit > 0 ? (used * 100 / limit) : 0; };
    out["balancer"]["conns_used"] = connsInUse_.load();
    out["balancer"]["conns_peak"] = connsPeak_.load();
    out["balancer"]["conns_peak_pct"] = pct(connsPeak_.load(), cfg_.maxConns);
    out["balancer"]["client_conns_used"] = clientConnsInUse_.load();
    out["balancer"]["client_conns_peak"] = clientConnsPeak_.load();
    out["balancer"]["client_conns_peak_pct"] = pct(clientConnsPeak_.load(), cfg_.maxClientConns);
    out["balancer"]["worker_conns_used"] = workerConnsInUse_.load();
    out["balancer"]["worker_conns_peak"] = workerConnsPeak_.load();
    out["balancer"]["worker_conns_peak_pct"] = pct(workerConnsPeak_.load(), cfg_.maxWorkerConns);

    nlohmann::json workers = nlohmann::json::array();
    int connected = 0;
    for (const auto& [tok, w] : workers_) {
        nlohmann::json j;
        j["token_hash"] = tok.substr(0, 8) + "..."; // токен не раскрываем полностью
        j["label"] = w.label;
        j["connected"] = (now - w.lastSeen) < std::chrono::seconds(cfg_.pollTimeoutSec * 3);
        if (j["connected"]) {
            connected++;
        }
        j["capacity"] = w.capacity;
        j["in_flight"] = w.inFlight;
        j["assigned"] = w.assigned;
        j["completed"] = w.completed;
        if (!w.metrics.empty()) {
            try {
                j["metrics"] = nlohmann::json::parse(w.metrics);
            } catch (const std::exception&) {
                j["metrics"] = nullptr;
            }
        }
        workers.push_back(j);
    }
    out["balancer"]["workers_connected"] = connected;
    out["workers"] = workers;
    return out;
}

std::string PlaygroundServer::buildStatsJsonLocked() {
    return statsJsonLocked().dump();
}

std::string PlaygroundServer::currentStatsJson() {
    std::lock_guard<std::mutex> lock(mu_);
    return buildStatsJsonLocked();
}

// Читаемая текстовая сводка статистики (для тел писем). Требует mu_.
std::string PlaygroundServer::buildStatsTextLocked() {
    nlohmann::json j = statsJsonLocked();
    std::ostringstream ss;
    const auto& b = j["balancer"];
    ss << "Balancer:\n";
    ss << "  workers known:     " << b.value("workers_known", 0) << "\n";
    ss << "  workers connected: " << b.value("workers_connected", 0) << "\n";
    ss << "  queue:             " << b.value("queue", 0) << " / " << b.value("max_queue", 0) << "\n";
    ss << "  in_flight:         " << b.value("in_flight", 0) << "\n";
    ss << "  rate-limit IPs:    " << b.value("rate_limit_ips_tracked", 0) << " (resets: " << b.value("rate_limit_resets", 0) << ")\n";
    ss << "  archives requested:" << b.value("archives_requested", 0) << "\n";
    ss << "  conns (used/peak):  global=" << b.value("conns_used", 0) << "/" << b.value("conns_peak", 0) << " (" << b.value("conns_peak_pct", 0) << "%)"
       << " client=" << b.value("client_conns_used", 0) << "/" << b.value("client_conns_peak", 0) << " (" << b.value("client_conns_peak_pct", 0) << "%)"
       << " worker=" << b.value("worker_conns_used", 0) << "/" << b.value("worker_conns_peak", 0) << " (" << b.value("worker_conns_peak_pct", 0) << "%)\n";
    ss << "\nWorkers:\n";
    for (const auto& w : j["workers"]) {
        const bool on = w.value("connected", false);
        ss << "  " << w.value("label", "?") << " [" << w.value("token_hash", "") << "] " << (on ? "connected" : "OFFLINE") << " cap=" << w.value("capacity", 0)
           << " in_flight=" << w.value("in_flight", 0) << " assigned=" << w.value("assigned", 0) << " done=" << w.value("completed", 0) << "\n";
        if (w.contains("metrics") && !w["metrics"].is_null()) {
            const auto& m = w["metrics"];
            ss << "    uptime=" << m.value("uptime_sec", 0) << "s"
               << " mem_used=" << (m.value("mem_used", int64_t(0)) / (1024 * 1024)) << "MB"
               << " (max " << (m.value("mem_used_max", int64_t(0)) / (1024 * 1024)) << "MB)"
               << " load1=" << m.value("load1", 0.0) << " (max " << m.value("load_max", 0.0) << ")"
               << " net_rx=" << m.value("net_rx_bytes", int64_t(0)) << "B\n";
        }
    }
    return ss.str();
}

// HTML-страница статистики (GET /stats?format=html). Требует mu_.
std::string PlaygroundServer::buildStatsHtmlLocked() {
    nlohmann::json j = statsJsonLocked();
    // Динамическое тело (таблицы Balancer/Workers) собирается в h и вставляется
    // в шаблон kStatsPage (плейсхолдеры %%TIME%% / %%BODY%%).
    std::ostringstream h;
    const auto& b = j["balancer"];
    h << "<h2>Balancer</h2><table>"
      << "<tr><th>workers known</th><td>" << b.value("workers_known", 0) << "</td></tr>"
      << "<tr><th>workers connected</th><td>" << b.value("workers_connected", 0) << "</td></tr>"
      << "<tr><th>queue</th><td>" << b.value("queue", 0) << " / " << b.value("max_queue", 0) << "</td></tr>"
      << "<tr><th>in flight</th><td>" << b.value("in_flight", 0) << "</td></tr>"
      << "<tr><th>rate-limit IPs</th><td>" << b.value("rate_limit_ips_tracked", 0) << " (resets: " << b.value("rate_limit_resets", 0) << ")</td></tr>"
      << "<tr><th>archives requested</th><td>" << b.value("archives_requested", 0) << "</td></tr>"
      << "<tr><th>conns global (used/peak)</th><td>" << b.value("conns_used", 0) << " / " << b.value("conns_peak", 0) << " (" << b.value("conns_peak_pct", 0)
      << "%)</td></tr>"
      << "<tr><th>conns client (used/peak)</th><td>" << b.value("client_conns_used", 0) << " / " << b.value("client_conns_peak", 0) << " ("
      << b.value("client_conns_peak_pct", 0) << "%)</td></tr>"
      << "<tr><th>conns worker (used/peak)</th><td>" << b.value("worker_conns_used", 0) << " / " << b.value("worker_conns_peak", 0) << " ("
      << b.value("worker_conns_peak_pct", 0) << "%)</td></tr>"
      << "</table>";
    h << "<h2>Workers</h2><table><tr><th>label</th><th>token</th><th>state</th><th>cap</th><th>in "
         "flight</th><th>assigned</th><th>done</th><th>metrics</th></tr>";
    for (const auto& w : j["workers"]) {
        const bool on = w.value("connected", false);
        h << "<tr><td>" << htmlEscape(w.value("label", "")) << "</td><td>" << htmlEscape(w.value("token_hash", "")) << "</td>"
          << "<td class=\"" << (on ? "on" : "off") << "\">" << (on ? "connected" : "OFFLINE") << "</td>"
          << "<td>" << w.value("capacity", 0) << "</td><td>" << w.value("in_flight", 0) << "</td>"
          << "<td>" << w.value("assigned", 0) << "</td><td>" << w.value("completed", 0) << "</td><td>";
        if (w.contains("metrics") && !w["metrics"].is_null()) {
            const auto& m = w["metrics"];
            h << "uptime=" << m.value("uptime_sec", 0) << "s, mem_used=" << (m.value("mem_used", int64_t(0)) / (1024 * 1024)) << "MB"
              << " (max " << (m.value("mem_used_max", int64_t(0)) / (1024 * 1024)) << "MB), load1=" << m.value("load1", 0.0) << " (max "
              << m.value("load_max", 0.0) << ")";
        } else {
            h << "-";
        }
        h << "</td></tr>";
    }
    h << "</table>";

    // Подстановка динамических данных в шаблон stats-страницы.
    // Плейсхолдеры %%TIME%% / %%BODY%% обязаны присутствовать в шаблоне; их отсутствие -
    // ошибка разработчика (тихий пропуск запрещён, см. AGENTS.md правило 5).
    std::string page = kStatsPage;
    const std::string kTimePlaceholder = "%%TIME%%";
    const size_t tpos = page.find(kTimePlaceholder);
    EXPECT(tpos != std::string::npos && "stats page template must contain %%TIME%% placeholder");
    page.replace(tpos, kTimePlaceholder.size(), utcNowString());
    const std::string kBodyPlaceholder = "%%BODY%%";
    const size_t bpos = page.find(kBodyPlaceholder);
    EXPECT(bpos != std::string::npos && "stats page template must contain %%BODY%% placeholder");
    page.replace(bpos, kBodyPlaceholder.size(), h.str());
    return page;
}

std::string PlaygroundServer::currentStatsText() {
    std::lock_guard<std::mutex> lock(mu_);
    return buildStatsTextLocked();
}

std::string PlaygroundServer::currentStatsHtml() {
    std::lock_guard<std::mutex> lock(mu_);
    return buildStatsHtmlLocked();
}

} // namespace playground
} // namespace trust
