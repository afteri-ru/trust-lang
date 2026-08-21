// src/playground/server.cpp
// trust-playground: реализация балансировщика (см. include/playground/server.h).

#include "playground/server.h"
#include "trust/version.h"

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
#include <thread>

namespace trust {
namespace playground {

namespace {

int createListenSocket(const std::string& bind_addr, int port, std::string& error) {
    const int fd = ::socket(AF_INET, SOCK_STREAM, 0);
    if (fd < 0) {
        error = "cannot create socket: " + std::string(std::strerror(errno));
        return -1;
    }
    int opt = 1;
    ::setsockopt(fd, SOL_SOCKET, SO_REUSEADDR, &opt, sizeof(opt));
    struct sockaddr_in addr{};
    addr.sin_family = AF_INET;
    addr.sin_port = htons(static_cast<uint16_t>(port));
    if (::inet_pton(AF_INET, bind_addr.c_str(), &addr.sin_addr) <= 0) {
        error = "invalid bind address: " + bind_addr;
        ::close(fd);
        return -1;
    }
    if (::bind(fd, reinterpret_cast<struct sockaddr*>(&addr), sizeof(addr)) < 0) {
        error = "cannot bind to " + bind_addr + ":" + std::to_string(port) + ": " + std::strerror(errno);
        ::close(fd);
        return -1;
    }
    if (::listen(fd, 64) < 0) {
        error = "cannot listen: " + std::string(std::strerror(errno));
        ::close(fd);
        return -1;
    }
    return fd;
}

// Читает raw HTTP/1.1-запрос: заголовки до \r\n\r\n + тело Content-Length байт.
// Безопасно: общий таймаут чтения (защита от slowloris), лимит на заголовки и
// суммарный размер (защита от объявления огромного Content-Length). При
// превышении/таймауте возвращает то, что успело прочитаться (обработчик
// вернёт 400/413); пустая строка - соединение закрылось без данных.
std::string readRawRequest(int fd, size_t max_body_bytes) {
    constexpr size_t kHeaderCap = 64 * 1024; // максимальный блок заголовков
    constexpr int kReadTimeoutSec = 15;      // общий таймаут на весь запрос
    const size_t cap = kHeaderCap + max_body_bytes;

    std::string buf;
    buf.reserve(std::min(cap, size_t{64 * 1024}));
    char tmp[8192];
    struct pollfd p{fd, POLLIN, 0};
    const auto deadline = std::chrono::steady_clock::now() + std::chrono::seconds(kReadTimeoutSec);

    // 1) Заголовки (до \r\n\r\n), с лимитом и таймаутом.
    size_t headers_end = std::string::npos;
    while (headers_end == std::string::npos) {
        if (buf.size() >= kHeaderCap) {
            return std::string(); // заголовки не завершились в лимит - bad request
        }
        const auto remain = std::chrono::duration_cast<std::chrono::milliseconds>(deadline - std::chrono::steady_clock::now());
        if (remain.count() <= 0) {
            return std::string(); // таймаут - slowloris
        }
        const int pr = ::poll(&p, 1, static_cast<int>(remain.count()));
        if (pr < 0) {
            if (errno == EINTR) {
                continue;
            }
            return std::string();
        }
        if (pr == 0) {
            return std::string(); // таймаут
        }
        const ssize_t n = ::read(fd, tmp, sizeof(tmp));
        if (n <= 0) {
            return buf; // клиент закрыл соединение (или ошибка)
        }
        buf.append(tmp, static_cast<size_t>(n));
        headers_end = buf.find("\r\n\r\n");
    }

    int content_length = 0;
    const size_t header_block_end = buf.find("\r\n\r\n");
    size_t pos = 0;
    while (pos < header_block_end) {
        const size_t eol = buf.find("\r\n", pos);
        if (eol == std::string::npos || eol > header_block_end) {
            break;
        }
        const std::string header = buf.substr(pos, eol - pos);
        if (trust::transport::isContentLength(header)) {
            content_length = trust::transport::parseContentLength(header);
        }
        pos = eol + 2;
    }

    // 2) Тело Content-Length байт, с общим лимитом (защита от объявленного огромного размера).
    const size_t body_start = header_block_end + 4;
    const size_t target = body_start + static_cast<size_t>(content_length);
    while (buf.size() < target) {
        if (buf.size() >= cap) {
            return buf; // превышен суммарный лимит - обработчик вернёт 413
        }
        const auto remain = std::chrono::duration_cast<std::chrono::milliseconds>(deadline - std::chrono::steady_clock::now());
        if (remain.count() <= 0) {
            return buf;
        }
        const int pr = ::poll(&p, 1, static_cast<int>(remain.count()));
        if (pr < 0) {
            if (errno == EINTR) {
                continue;
            }
            return buf;
        }
        if (pr == 0) {
            return buf;
        }
        const ssize_t n = ::read(fd, tmp, sizeof(tmp));
        if (n <= 0) {
            return buf;
        }
        buf.append(tmp, static_cast<size_t>(n));
    }
    return buf;
}

// Пишет весь буфер в fd (петля - иначе большой ответ/архив обрезается из-за
// частичной записи в сокетный буфер).
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

// Криптографически случайные байты из /dev/urandom.
std::string randomBytes(size_t n) {
    std::string out(n, '\0');
    FILE* f = std::fopen("/dev/urandom", "rb");
    if (f != nullptr) {
        if (std::fread(&out[0], 1, n, f) != n) {
            out.assign(n, '\0');
        }
        std::fclose(f);
    }
    return out;
}

// Случайный 64-битный job id (непоследовательный, не угадываемый по соседним).
int64_t randomJobId() {
    const std::string b = randomBytes(sizeof(uint64_t));
    uint64_t v = 0;
    for (size_t i = 0; i < sizeof(uint64_t); ++i) {
        v = (v << 8) | static_cast<uint8_t>(b[i]);
    }
    v &= static_cast<uint64_t>(INT64_MAX);
    return (v == 0) ? 1 : static_cast<int64_t>(v);
}

// Текущая дата/время в ISO-подобном формате UTC.
std::string utcNowString() {
    char buf[40];
    const std::time_t t = std::time(nullptr);
    std::tm tm{};
    gmtime_r(&t, &tm);
    std::strftime(buf, sizeof(buf), "%Y-%m-%dT%H:%M:%SZ", &tm);
    return buf;
}

// Ограничивает число соединений разумным диапазоном [1, kMaxConnSemaphore] (верх = потолок
// типа семафора). О превышении предупреждает конструктор (см. PlaygroundServer::ctor).
static int clampConnLimit(int v) {
    if (v < 1) {
        return 1;
    }
    if (v > static_cast<int>(trust::playground::kMaxConnSemaphore)) {
        return trust::playground::kMaxConnSemaphore;
    }
    return v;
}

// Обновляет пиковое значение занятости (high-water mark) после инкремента used.
static void bumpPeak(std::atomic<int>& used, std::atomic<int>& peak) {
    const int cur = used.load();
    int prev = peak.load();
    while (cur > prev && !peak.compare_exchange_weak(prev, cur)) {
    }
}

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

// true, если Origin вида "scheme://host[:port]" указывает на loopback (localhost/127.x/::1).
// Используется для fail-closed CORS-дефолта: без явного allowed_origins разрешаем CORS
// только локальной разработке.
bool isLoopbackOrigin(const std::string& origin) {
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

// Отправляет письмо через alert_cmd (по умолчанию sendmail -t), читающий письмо из stdin.
// Возвращает false при пустом получателе или ошибке команды.
bool sendMail(const std::string& cmd, const std::string& from, const std::string& to, const std::string& subject, const std::string& body) {
    if (to.empty() || cmd.empty()) {
        return false;
    }
    char date[64];
    const std::time_t t = std::time(nullptr);
    std::tm tm{};
    gmtime_r(&t, &tm);
    std::strftime(date, sizeof(date), "%a, %d %b %Y %H:%M:%S %z", &tm);
    const std::string msg = "From: " + from +
                            "\r\n"
                            "To: " +
                            to +
                            "\r\n"
                            "Subject: " +
                            subject +
                            "\r\n"
                            "Date: " +
                            date +
                            "\r\n"
                            "Content-Type: text/plain; charset=utf-8\r\n"
                            "\r\n" +
                            body;
    FILE* pipe = ::popen(cmd.c_str(), "w");
    if (pipe == nullptr) {
        return false;
    }
    const size_t n = std::fwrite(msg.data(), 1, msg.size(), pipe);
    const int rc = ::pclose(pipe);
    return n == msg.size() && rc == 0;
}

} // namespace

PlaygroundServer::PlaygroundServer(const PlaygroundConfig& cfg)
: cfg_(cfg)
, connSlots_(clampConnLimit(cfg.maxConns))
, clientConnSlots_(clampConnLimit(cfg.maxClientConns))
, workerConnSlots_(clampConnLimit(cfg.maxWorkerConns)) {
    // Проверка потолка и распределения лимитов соединений.
    //  cap = kMaxConnSemaphore - compile-time потолок семафора (для САМОГО max_conns).
    //  global = эффективный глобальный кап (max_conns после клампа в cap) - общий лимит на
    //           весь пул соединений.
    // Клиентский и воркерский пулы - отдельные семафоры, каждый может быть до своего лимита;
    // их СУММА может превышать global (это штатно: global откажет лишние соединения кодом 503).
    // Но отдельный пул не должен быть БОЛЬШЕ global: иначе его лишняя ёмкость недостижима
    // (общий кап сработает раньше), и такой конфиг вводит в заблуждение.
    const int cap = static_cast<int>(trust::playground::kMaxConnSemaphore);
    const int global = clampConnLimit(cfg.maxConns);
    if (cfg.maxConns > cap) {
        trust::errs() << "trust-playground (playground): WARNING: playground.max_conns=" << cfg.maxConns << " exceeds the maximum connection limit " << cap
                      << "; using " << cap << "\n";
    }
    if (cfg.maxClientConns > global) {
        trust::errs() << "trust-playground (playground): WARNING: playground.max_client_conns=" << cfg.maxClientConns << " exceeds global max_conns (" << global
                      << "); the client pool can't be fully utilized (global cap throttles first)\n";
    }
    if (cfg.maxWorkerConns > global) {
        trust::errs() << "trust-playground (playground): WARNING: playground.max_worker_conns=" << cfg.maxWorkerConns << " exceeds global max_conns (" << global
                      << "); the worker pool can't be fully utilized (global cap throttles first)\n";
    }
}

void PlaygroundServer::requestStop() {
    // Только async-signal-safe: set-флаг без cv_.notify_all() (последний захватывает
    // мьютекс и недопустим из обработчика сигнала). accept-цикл выходит по EINTR-перепроверке.
    stop_.store(true);
}

bool PlaygroundServer::isWorkerToken(const std::string& token) const {
    return !workerLabelForToken(cfg_, token).empty();
}

void PlaygroundServer::releaseJobSlot(const std::shared_ptr<Job>& job) {
    if (job->workerToken.empty() || job->released) {
        return;
    }
    auto it = workers_.find(job->workerToken);
    if (it != workers_.end() && it->second.inFlight > 0) {
        it->second.inFlight--;
    }
    job->released = true;
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

std::string PlaygroundServer::cacheGetLocked(const std::string& key, const std::string& code) {
    const auto now = std::chrono::steady_clock::now();
    auto it = cache_.find(key);
    if (it == cache_.end()) {
        return std::string();
    }
    const int ttl = cfg_.cacheTtlSec > 0 ? cfg_.cacheTtlSec : 0;
    if (ttl > 0 && now - it->second.created >= std::chrono::seconds(ttl)) {
        cache_.erase(it);
        return std::string();
    }
    // Защита от отравления кеша: результат отдаём, только если код совпадает с тем,
    // для которого он был закеширован (иначе злоумышленник подложил бы чужой вывод
    // под имя дефолтного примера).
    if (it->second.code != code) {
        cache_.erase(it);
        return std::string();
    }
    it->second.lastAccess = now;
    return it->second.result;
}

void PlaygroundServer::cachePutLocked(const std::string& key, const std::string& code, const std::string& result) {
    if (key.empty() || result.empty()) {
        return;
    }
    if (cfg_.cacheMaxEntries <= 0 && cfg_.cacheMaxMb <= 0) {
        return; // кеш отключён
    }
    const auto now = std::chrono::steady_clock::now();
    auto& e = cache_[key];
    e.code = code;
    e.result = result;
    e.size = result.size();
    e.created = now;
    e.lastAccess = now;
    cacheEvictLocked();
}

void PlaygroundServer::cacheEvictLocked() {
    const auto now = std::chrono::steady_clock::now();
    const int ttl = cfg_.cacheTtlSec > 0 ? cfg_.cacheTtlSec : 0;
    const size_t maxBytes = static_cast<size_t>(cfg_.cacheMaxMb) * 1024 * 1024;
    const size_t maxEntries = static_cast<size_t>(cfg_.cacheMaxEntries);
    for (auto it = cache_.begin(); it != cache_.end();) {
        if (ttl > 0 && now - it->second.created >= std::chrono::seconds(ttl)) {
            it = cache_.erase(it);
        } else {
            ++it;
        }
    }
    const auto totalBytes = [&]() {
        size_t s = 0;
        for (const auto& kv : cache_) {
            s += kv.second.size;
        }
        return s;
    };
    while ((maxEntries > 0 && cache_.size() > maxEntries) || (maxBytes > 0 && totalBytes() > maxBytes)) {
        auto lru = cache_.begin();
        for (auto it = cache_.begin(); it != cache_.end(); ++it) {
            if (it->second.lastAccess < lru->second.lastAccess) {
                lru = it;
            }
        }
        if (lru == cache_.end()) {
            break;
        }
        cache_.erase(lru);
    }
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

std::string PlaygroundServer::issuePowChallengeLocked(int difficulty) {
    static constexpr const char* kHex = "0123456789abcdef";
    const std::string bytes = randomBytes(16);
    std::string nonce;
    nonce.reserve(32);
    for (const unsigned char c : bytes) {
        nonce += kHex[c >> 4];
        nonce += kHex[c & 0x0f];
    }
    PowChallenge ch;
    ch.difficulty = difficulty;
    ch.created = std::chrono::steady_clock::now();
    ch.uses = 0;
    powChallenges_[nonce] = ch;
    const int ttl = cfg_.powNonceTtlSec > 0 ? cfg_.powNonceTtlSec : 60;
    const auto now = std::chrono::steady_clock::now();
    for (auto it = powChallenges_.begin(); it != powChallenges_.end();) {
        if (now - it->second.created >= std::chrono::seconds(ttl)) {
            it = powChallenges_.erase(it);
        } else {
            ++it;
        }
    }
    return nonce;
}

bool PlaygroundServer::verifyPowLocked(const std::string& header, int required_difficulty) {
    if (required_difficulty <= 0) {
        return true;
    }
    if (header.empty()) {
        return false;
    }
    const size_t colon = header.find(':');
    if (colon == std::string::npos) {
        return false;
    }
    const std::string nonce = header.substr(0, colon);
    const std::string solution = header.substr(colon + 1);
    auto it = powChallenges_.find(nonce);
    if (it == powChallenges_.end()) {
        return false;
    }
    const int ttl = cfg_.powNonceTtlSec > 0 ? cfg_.powNonceTtlSec : 60;
    if (std::chrono::steady_clock::now() - it->second.created >= std::chrono::seconds(ttl)) {
        powChallenges_.erase(it);
        return false;
    }
    if (it->second.difficulty < required_difficulty) {
        powChallenges_.erase(it);
        return false;
    }
    const int maxUses = cfg_.powMaxUsesPerNonce > 0 ? cfg_.powMaxUsesPerNonce : 1;
    if (it->second.uses >= maxUses) {
        powChallenges_.erase(it);
        return false;
    }
    // Решение валидно, если sha256(nonce + solution) начинается с required_difficulty
    // нулевых бит. Проверяем по старшим нибблам hex-строки.
    const std::string hash = trust::playground::sha256Hex(nonce + solution);
    static const int kLz[16] = {4, 3, 2, 2, 1, 1, 1, 1, 0, 0, 0, 0, 0, 0, 0, 0};
    int zeros = 0;
    for (const char c : hash) {
        const int v = (c >= '0' && c <= '9') ? (c - '0') : (c - 'a' + 10);
        const int n = kLz[v & 0x0f];
        if (zeros + n >= required_difficulty) {
            it->second.uses++;
            return true;
        }
        if (n == 0) {
            return false;
        }
        zeros += n;
    }
    return zeros >= required_difficulty;
}

bool PlaygroundServer::statsSessionOkLocked(const std::string& session_id) {
    if (session_id.empty()) {
        return false;
    }
    auto it = statsSessions_.find(session_id);
    if (it == statsSessions_.end()) {
        return false;
    }
    const auto now = std::chrono::steady_clock::now();
    const int ttl = cfg_.statsSessionTtlSec > 0 ? cfg_.statsSessionTtlSec : 600;
    const int max = cfg_.statsSessionMaxSec;
    if (now - it->second.lastAccess >= std::chrono::seconds(ttl) || (max > 0 && now - it->second.created >= std::chrono::seconds(max))) {
        statsSessions_.erase(it);
        return false;
    }
    it->second.lastAccess = now; // sliding refresh
    return true;
}

std::string PlaygroundServer::createStatsSessionLocked() {
    static constexpr const char* kHex = "0123456789abcdef";
    const std::string bytes = randomBytes(16);
    std::string id;
    id.reserve(32);
    for (const unsigned char c : bytes) {
        id += kHex[c >> 4];
        id += kHex[c & 0x0f];
    }
    StatsSession s;
    s.created = std::chrono::steady_clock::now();
    s.lastAccess = s.created;
    statsSessions_[id] = s;
    return id;
}

void PlaygroundServer::destroyStatsSessionLocked(const std::string& session_id) {
    if (!session_id.empty()) {
        statsSessions_.erase(session_id);
    }
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

bool PlaygroundServer::hasConnectedWorkerLocked() const {
    const auto now = std::chrono::steady_clock::now();
    for (const auto& [tok, w] : workers_) {
        (void)tok;
        if ((now - w.lastSeen) < std::chrono::seconds(cfg_.pollTimeoutSec * 3)) {
            return true;
        }
    }
    return false;
}

bool PlaygroundServer::rateLimitExceeded(const std::string& ip) {
    const auto now = std::chrono::steady_clock::now();
    std::lock_guard<std::mutex> lock(mu_);
    // Ограничиваем рост карты уникальных IP (защита от переполнения при флуде).
    if (ipHits_.size() >= static_cast<size_t>(cfg_.maxRateLimitIps) && ipHits_.find(ip) == ipHits_.end()) {
        ipHits_.clear();
        rateLimitResets_.fetch_add(1); // считаем сбросы (видно в /stats)
    }
    std::vector<std::chrono::steady_clock::time_point>& hits = ipHits_[ip];
    const auto cutoff = now - std::chrono::seconds(60);
    hits.erase(std::remove_if(hits.begin(), hits.end(), [&](const std::chrono::steady_clock::time_point& t) { return t < cutoff; }), hits.end());
    if (static_cast<int>(hits.size()) >= cfg_.rateLimitPerIp) {
        return true;
    }
    hits.push_back(now);
    return false;
}
HttpResponse PlaygroundServer::handle(const HttpRequest& req, const std::string& peer_ip) {
    HttpResponse resp;

    if (req.method == "OPTIONS") {
        // CORS preflight. Если задан allowlist Origin - пропускаем только разрешённые.
        if (!cfg_.allowedOrigins.empty() && !browserOriginAllowed(req)) {
            resp.status = 403;
            resp.content_type = "application/json; charset=utf-8";
            resp.body = "{\"error\":\"origin not allowed\"}";
            return resp;
        }
        resp.status = 204;
        resp.content_type = "";
        resp.body = "";
        resp.cors = true; // preflight для браузерных эндпоинтов (/run, /download)
        resp.corsOrigin = corsOriginFor(req);
        return resp;
    }
    if (req.method == "GET" && (req.target == "/health" || req.target == "/health/")) {
        // /health тоже подчиняется доменной привязке «только с конкретной песочницы»:
        // посторонний сайт не должен получать 200/статистику воркеров. Запросы без Origin
        // (curl/оператор) проходят - browserOriginAllowed разрешает пустой Origin.
        if (!browserOriginAllowed(req)) {
            resp.status = 403;
            resp.content_type = "application/json; charset=utf-8";
            resp.body = "{\"error\":\"forbidden\"}";
            return resp;
        }
        resp.status = 200;
        resp.content_type = "application/json; charset=utf-8";
        resp.cors = true;
        resp.corsOrigin = corsOriginFor(req);
        // Число «живых» воркеров - для статусной строки песочницы (публичный пинг готовности).
        const auto now = std::chrono::steady_clock::now();
        int connected = 0;
        {
            std::lock_guard<std::mutex> lock(mu_);
            for (const auto& [tok, w] : workers_) {
                if ((now - w.lastSeen) < std::chrono::seconds(cfg_.pollTimeoutSec * 3)) {
                    connected++;
                }
            }
        }
        nlohmann::json j{{"status", "ok"}, {"workers_connected", connected}};
        resp.body = j.dump();
        return resp;
    }
    if (req.method == "POST" && req.target == "/run") {
        return handleRun(req, peer_ip);
    }
    if (req.method == "POST" && req.target == "/poll") {
        return handlePoll(req);
    }
    if (req.method == "POST" && req.target == "/result") {
        return handleResult(req);
    }
    if (req.method == "POST" && req.target == "/download") {
        return handleDownload(req, peer_ip);
    }
    if (req.method == "GET" && req.target == "/challenge") {
        return handleChallenge(req);
    }
    if (req.method == "GET" && req.target == "/stats/login") {
        return handleStatsLogin(req);
    }
    if (req.method == "POST" && req.target == "/stats/login") {
        return handleStatsLogin(req);
    }
    if (req.method == "POST" && req.target == "/stats/logout") {
        return handleStatsLogout(req);
    }
    if (req.method == "GET" && (req.target == "/stats" || req.target.rfind("/stats?", 0) == 0)) {
        return handleStats(req);
    }

    resp.status = 404;
    resp.content_type = "application/json; charset=utf-8";
    resp.body = "{\"error\":\"not found\"}";
    return resp;
}

HttpResponse PlaygroundServer::handleRun(const HttpRequest& req, const std::string& peer_ip) {
    HttpResponse resp;
    resp.content_type = "application/json; charset=utf-8";
    resp.cors = true; // /run вызывается браузерной страницей с другого домена
    resp.corsOrigin = corsOriginFor(req);

    // Доменная привязка: публичный /run принимается только с конкретной песочницы
    // (Origin/Host из allowlist; при пустом allowed_origins - fail-closed loopback).
    if (!browserOriginAllowed(req)) {
        resp.status = 403;
        resp.body = "{\"error\":\"forbidden\"}";
        return resp;
    }

    if (req.body.empty()) {
        resp.status = 400;
        resp.body = "{\"error\":\"empty request body\"}";
        return resp;
    }
    if (static_cast<int>(req.body.size()) > cfg_.bodyLimitKb * 1024) {
        resp.status = 413;
        resp.body = "{\"error\":\"request body too large\"}";
        return resp;
    }

    // PoW (если включён конфигом): требование предъявить решённый челлендж. Неверное/отсутствующее
    // решение -> 402 + новый челлендж. Адаптивная сложность зависит от загрузки.
    if (cfg_.powMinDifficulty > 0) {
        const int diff = currentPowDifficulty();
        if (diff > 0) {
            std::lock_guard<std::mutex> plock(mu_);
            if (!verifyPowLocked(req.xPow, diff)) {
                const std::string nonce = issuePowChallengeLocked(diff);
                nlohmann::json j{{"ok", false}, {"error", "proof-of-work required"}, {"nonce", nonce}, {"difficulty", diff}, {"ttl_sec", cfg_.powNonceTtlSec}};
                resp.status = 402;
                resp.body = j.dump();
                return resp;
            }
        }
    }

    // Rate-limit по РЕАЛЬНОМУ клиентскому IP: за nginx peer всегда 127.0.0.1, поэтому
    // используем первый hop X-Forwarded-For (доверяем только с loopback).
    if (rateLimitExceeded(effectiveClientIp(req, peer_ip))) {
        resp.status = 429;
        resp.body = "{\"error\":\"rate limit exceeded\"}";
        return resp;
    }

    const std::string example_key = req.exampleName;
    std::unique_lock<std::mutex> lock(mu_);
    // Кеш примеров: если фронтенд прислал имя примера (X-Example-Name), кешируем ПО ИМЕНИ
    // (без хеширования и LRU - число примеров фиксировано). Пустое имя = произвольный код,
    // сразу на выполнение, без кеша. Заполнение кеша делает воркер при первом запросе.
    if (!example_key.empty()) {
        const std::string cached = cacheGetLocked(example_key, req.body);
        if (!cached.empty()) {
            resp.status = 200;
            resp.body = cached;
            return resp;
        }
    }
    if (!hasConnectedWorkerLocked()) {
        notifyAlert("no workers connected", buildStatsTextLocked());
        nlohmann::json j{{"ok", false}, {"unavailable", true}, {"error", "no workers connected"}, {"instructionsUrl", kInstructionsUrl}};
        resp.status = 503;
        resp.body = j.dump();
        return resp;
    }
    if (static_cast<int>(queue_.size()) >= cfg_.maxQueue) {
        notifyAlert("queue full", buildStatsTextLocked());
        resp.status = 503;
        resp.body = "{\"error\":\"queue full\"}";
        return resp;
    }

    auto job = std::make_shared<Job>();
    // Непоследовательный случайный id (не угадываемый по соседним); проверяем
    // уникальность среди активных задач (очередь + в полёте).
    do {
        job->id = randomJobId();
    } while (job->id <= 0 || inFlight_.count(job->id) != 0 ||
             std::any_of(queue_.begin(), queue_.end(), [&](const std::shared_ptr<Job>& j) { return j->id == job->id; }));
    job->code = req.body;
    job->exampleName = example_key;
    queue_.push_back(job);
    inFlight_[job->id] = job;
    cv_.notify_all();

    const auto deadline = std::chrono::steady_clock::now() + std::chrono::seconds(cfg_.jobTimeoutSec + cfg_.pollTimeoutSec);
    cv_.wait_until(lock, deadline, [&] { return job->done || stop_.load(); });

    releaseJobSlot(job); // освобождает слот, если задача не доставлена в срок
    inFlight_.erase(job->id);
    cv_.notify_all();

    if (!job->done) {
        nlohmann::json j{{"ok", false}, {"unavailable", true}, {"error", "no worker completed the request in time"}, {"instructionsUrl", kInstructionsUrl}};
        resp.status = 503;
        resp.body = j.dump();
        return resp;
    }

    // /run возвращает только JSON-контракт. Build-архив НЕ строится здесь - он лениво
    // собирается по отдельному POST /download (отдельный запрос, заново обрабатывает файл).
    resp.status = 200;
    resp.body = job->result;
    return resp;
}

HttpResponse PlaygroundServer::handleDownload(const HttpRequest& req, const std::string& peer_ip) {
    HttpResponse resp;
    resp.content_type = "application/json; charset=utf-8";
    resp.cors = true; // /download вызывается браузерной страницей (XHR) с другого домена
    resp.corsOrigin = corsOriginFor(req);

    // Доменная привязка (см. handleRun).
    if (!browserOriginAllowed(req)) {
        resp.status = 403;
        resp.body = "{\"error\":\"forbidden\"}";
        return resp;
    }

    // POST /download: тело = Trust-код. Архив собирается ЛЕНИВО - отдельный запрос,
    // заново обрабатывает файл (свежая транспиляция + сборка build-каталога trust-lsp),
    // БЕЗ кеша на балансировщике, и сразу отдаётся клиенту как gzip.
    if (req.body.empty()) {
        resp.status = 400;
        resp.body = "{\"error\":\"empty request body\"}";
        return resp;
    }
    if (static_cast<int>(req.body.size()) > cfg_.bodyLimitKb * 1024) {
        resp.status = 413;
        resp.body = "{\"error\":\"request body too large\"}";
        return resp;
    }

    // PoW (если включён) - см. handleRun.
    if (cfg_.powMinDifficulty > 0) {
        const int diff = currentPowDifficulty();
        if (diff > 0) {
            std::lock_guard<std::mutex> plock(mu_);
            if (!verifyPowLocked(req.xPow, diff)) {
                const std::string nonce = issuePowChallengeLocked(diff);
                nlohmann::json j{{"ok", false}, {"error", "proof-of-work required"}, {"nonce", nonce}, {"difficulty", diff}, {"ttl_sec", cfg_.powNonceTtlSec}};
                resp.status = 402;
                resp.body = j.dump();
                return resp;
            }
        }
    }

    // Флуд-защита: каждое «Скачать» запускает сборку на воркере - ограничиваем частоту
    // по реальному клиентскому IP (первый hop X-Forwarded-For за nginx).
    if (rateLimitExceeded(effectiveClientIp(req, peer_ip))) {
        resp.status = 429;
        resp.body = "{\"error\":\"rate limit exceeded\"}";
        return resp;
    }

    std::unique_lock<std::mutex> lock(mu_);
    if (!hasConnectedWorkerLocked()) {
        notifyAlert("no workers connected", buildStatsTextLocked());
        nlohmann::json j{{"ok", false}, {"unavailable", true}, {"error", "no workers connected"}, {"instructionsUrl", kInstructionsUrl}};
        resp.status = 503;
        resp.body = j.dump();
        return resp;
    }
    if (static_cast<int>(queue_.size()) >= cfg_.maxQueue) {
        notifyAlert("queue full", buildStatsTextLocked());
        resp.status = 503;
        resp.body = "{\"error\":\"queue full\"}";
        return resp;
    }

    auto job = std::make_shared<Job>();
    do {
        job->id = randomJobId();
    } while (job->id <= 0 || inFlight_.count(job->id) != 0 ||
             std::any_of(queue_.begin(), queue_.end(), [&](const std::shared_ptr<Job>& j) { return j->id == job->id; }));
    job->code = req.body;
    job->buildArchive = true;
    archivesRequested_.fetch_add(1); // счётчик запрошенных build-архивов (в статистике)
    queue_.push_back(job);
    inFlight_[job->id] = job;
    cv_.notify_all();

    const auto deadline = std::chrono::steady_clock::now() + std::chrono::seconds(cfg_.jobTimeoutSec + cfg_.pollTimeoutSec);
    cv_.wait_until(lock, deadline, [&] { return job->done || stop_.load(); });

    releaseJobSlot(job);
    inFlight_.erase(job->id);
    cv_.notify_all();

    if (!job->done) {
        nlohmann::json j{{"ok", false}, {"error", "no worker built the archive in time"}};
        resp.status = 503;
        resp.body = j.dump();
        return resp;
    }

    // Результат воркера содержит build-архив (base64) + имя файла. Отдаём как gzip
    // (проходной ответ, без кеширования).
    try {
        nlohmann::json j = nlohmann::json::parse(job->result);
        const std::string b64 = j.value("archive", std::string());
        if (b64.empty() || b64.size() > static_cast<size_t>(cfg_.maxArchiveKb) * 1024 * 4 / 3) {
            // Показываем реальную причину (error/log воркера/trust-lsp), а не немой дефолт.
            std::string reason;
            if (b64.empty()) {
                reason = j.value("error", std::string());
                if (reason.empty()) {
                    reason = j.value("log", std::string());
                }
                if (reason.empty()) {
                    reason = "archive not produced";
                }
            } else {
                reason = "archive exceeds max_archive_kb (" + std::to_string(cfg_.maxArchiveKb) + ")";
            }
            nlohmann::json e{{"error", reason}};
            resp.status = 502;
            resp.body = e.dump();
            return resp;
        }
        const std::string bytes = base64Decode(b64);
        resp.status = 200;
        resp.content_type = "application/gzip";
        resp.cors = true; // XHR (blob) с браузерной страницы на другом домене
        resp.content_disposition = sanitizeFilename(j.value("archiveName", std::string("trust-lang-") + TRUST_VERSION_FULL + "-generated.tar.gz"));
        resp.body = bytes;
        return resp;
    } catch (const std::exception&) {
        resp.status = 502;
        resp.body = "{\"error\":\"bad worker result\"}";
        return resp;
    }
}

HttpResponse PlaygroundServer::handlePoll(const HttpRequest& req) {
    HttpResponse resp;
    resp.content_type = "application/json; charset=utf-8";

    nlohmann::json body;
    try {
        body = nlohmann::json::parse(req.body);
    } catch (const std::exception&) {
        resp.status = 400;
        resp.body = "{\"error\":\"bad poll body\"}";
        return resp;
    }
    const std::string token = body.value("token", std::string());
    std::unique_lock<std::mutex> lock(mu_);
    if (!isWorkerToken(token)) {
        resp.status = 403;
        resp.body = "{\"error\":\"unauthorized\"}";
        return resp;
    }

    WorkerState& ws = workers_[token];
    ws.token = token;
    ws.label = workerLabelForToken(cfg_, token);
    ws.capacity = body.value("capacity", 4);
    if (ws.capacity < 1) {
        ws.capacity = 1;
    }
    // Метрики системы/воркера, переданные в /poll. Ограничиваем размер (защита от
    // переполнения памяти злонамеренным/сломанным воркером).
    if (body.contains("metrics")) {
        std::string metrics = body["metrics"].dump();
        if (metrics.size() <= 8192) {
            ws.metrics = metrics;
        }
    }
    ws.lastSeen = std::chrono::steady_clock::now();
    trackWorkerPresenceLocked(); // фиксируем переходы «все воркеры отключились / восстановились»

    // Ретрай: задачи, назначенные «пропавшим» воркерам (перестали поллить), возвращаем
    // в очередь (до cfg.retry попыток) и освобождаем слот - иначе задача теряется.
    if (cfg_.retry > 0) {
        const auto now = std::chrono::steady_clock::now();
        const auto stale = now - std::chrono::seconds(cfg_.pollTimeoutSec * 3);
        for (auto& [id, job] : inFlight_) {
            if (job->done || job->workerToken.empty()) {
                continue;
            }
            auto w = workers_.find(job->workerToken);
            const bool dead = (w == workers_.end()) || (w->second.lastSeen < stale);
            if (dead && job->attempts < cfg_.retry) {
                job->attempts++;
                releaseJobSlot(job);
                queue_.push_back(job); // повторная диспетчеризация
                cv_.notify_all();
            }
        }
    }

    std::shared_ptr<Job> job;
    const auto deadline = std::chrono::steady_clock::now() + std::chrono::seconds(cfg_.pollTimeoutSec);
    while (!job && !stop_.load()) {
        if (!queue_.empty() && ws.inFlight < ws.capacity) {
            job = queue_.front();
            queue_.pop_front();
            job->workerToken = token;
            ws.inFlight++;
            ws.assigned++;
        } else if (cv_.wait_until(lock, deadline) == std::cv_status::timeout) {
            break;
        }
    }

    if (!job) {
        resp.status = 204;
        resp.content_type = "";
        resp.body = "";
        return resp;
    }

    nlohmann::json j{{"jobId", job->id}, {"code", job->code}, {"buildArchive", job->buildArchive}};
    resp.status = 200;
    resp.body = j.dump();
    return resp;
}

HttpResponse PlaygroundServer::handleResult(const HttpRequest& req) {
    HttpResponse resp;
    resp.content_type = "application/json; charset=utf-8";

    // Ограничиваем размер тела: результат + build-архив (base64) не должны превышать
    // разумный предел, иначе это трата памяти/CPU на парсинг огромного JSON. Для задач
    // «собрать архив» (POST /download) допустимый размер - с учётом max_archive_kb.
    const size_t result_limit_kb = static_cast<size_t>(std::max(cfg_.maxResultKb, (cfg_.maxArchiveKb * 4 + 2) / 3));
    if (req.body.size() > result_limit_kb * 1024) {
        notifyAlert("result body too large", buildStatsTextLocked());
        resp.status = 413;
        resp.body = "{\"error\":\"result body too large\"}";
        return resp;
    }

    nlohmann::json body;
    try {
        body = nlohmann::json::parse(req.body);
    } catch (const std::exception&) {
        resp.status = 400;
        resp.body = "{\"error\":\"bad result body\"}";
        return resp;
    }
    const std::string token = body.value("token", std::string());
    const int64_t job_id = body.value("jobId", int64_t(0));
    const std::string result = body.value("result", std::string());

    std::unique_lock<std::mutex> lock(mu_);
    if (!isWorkerToken(token)) {
        resp.status = 403;
        resp.body = "{\"error\":\"unauthorized\"}";
        return resp;
    }

    auto it = inFlight_.find(job_id);
    // Принимаем результат только от воркера, которому задача была выдана, -
    // иначе воркер с валидным токеном мог бы подменить результат чужой задачи.
    if (it != inFlight_.end() && it->second->workerToken == token) {
        it->second->result = result;
        it->second->done = true;
        // Кеш примеров: запись по имени примера (X-Example-Name) делает ВОРКЕР при первом
        // запросе. Для произвольного кода (имя пусто) и для build-архива (/download) кеша нет.
        if (!it->second->buildArchive && !it->second->exampleName.empty()) {
            cachePutLocked(it->second->exampleName, it->second->code, result);
        }
        releaseJobSlot(it->second);
        cv_.notify_all();
    }
    auto wit = workers_.find(token);
    if (wit != workers_.end()) {
        wit->second.completed++;
        wit->second.lastSeen = std::chrono::steady_clock::now();
    }

    resp.status = 200;
    resp.body = "{\"status\":\"ok\"}";
    return resp;
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
            std::lock_guard<std::mutex> lock(mu_);
            authorized = statsSessionOkLocked(cookieSessionId(req));
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
    std::lock_guard<std::mutex> lock(mu_);
    const std::string nonce = issuePowChallengeLocked(diff);
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
        resp.body = "<!doctype html><html lang=\"ru\"><head><meta charset=\"utf-8\">"
                    "<title>trust-playground admin</title>"
                    "<style>body{font-family:ui-monospace,monospace;margin:20px;color:#24292f;}form{display:flex;flex-direction:column;gap:8px;max-width:320px;"
                    "}input{padding:6px;}button{padding:6px 12px;cursor:pointer;}</style>"
                    "</head><body><h1>trust-playground admin</h1>"
                    "<p>Введите токен доступа к статистике:</p>"
                    "<form method=\"post\" action=\"/stats/login\">"
                    "<input type=\"password\" name=\"token\" autocomplete=\"off\" autofocus required>"
                    "<button type=\"submit\">Войти</button>"
                    "</form></body></html>";
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
        resp.body = "<!doctype html><html lang=\"ru\"><body><h1>403</h1><p>Неверный токен. "
                    "<a href=\"/stats/login\">Попробовать снова</a></p></body></html>";
        return resp;
    }
    std::string session_id;
    {
        std::lock_guard<std::mutex> lock(mu_);
        session_id = createStatsSessionLocked();
    }
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
    {
        std::lock_guard<std::mutex> lock(mu_);
        destroyStatsSessionLocked(sid);
    }
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
    out["balancer"]["rate_limit_ips_tracked"] = static_cast<int>(ipHits_.size());
    out["balancer"]["rate_limit_resets"] = rateLimitResets_.load();
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
    std::ostringstream h;
    h << "<!doctype html><html lang=\"ru\"><head><meta charset=\"utf-8\">"
      << "<title>trust-playground stats</title>"
      << "<style>body{font-family:ui-monospace,monospace;margin:20px;color:#24292f;}"
      << "table{border-collapse:collapse;margin:12px 0;}th,td{border:1px solid #d1d5db;padding:4px 10px;text-align:left;font-size:13px;}"
      << "th{background:#f6f8fa;}h2{font-size:18px;}.on{color:#16a34a;}.off{color:#dc2626;font-weight:600;}"
      << ".logout{margin-left:12px;font-size:12px;}</style></head><body>"
      << "<h1>trust-playground statistics <form method=\"post\" action=\"/stats/logout\" style=\"display:inline\">"
      << "<button type=\"submit\" class=\"logout\">Выйти</button></form></h1><p>Time: " << utcNowString() << "</p>";
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
    h << "</table></body></html>";
    return h.str();
}

std::string PlaygroundServer::currentStatsText() {
    std::lock_guard<std::mutex> lock(mu_);
    return buildStatsTextLocked();
}

std::string PlaygroundServer::currentStatsHtml() {
    std::lock_guard<std::mutex> lock(mu_);
    return buildStatsHtmlLocked();
}

void PlaygroundServer::notifyAlert(const std::string& reason, const std::string& stats_text) {
    if (cfg_.alertEmail.empty()) {
        return;
    }
    // НЕМЕДЛЕННО при первом появлении события; повтор того же события в течение
    // alert_interval_sec не шлём (per-reason dedup), чтобы «нет воркеров» не спамило.
    const int cooldown_sec = cfg_.alertIntervalSec > 0 ? cfg_.alertIntervalSec : 86400;
    {
        std::lock_guard<std::mutex> al(alertMutex_);
        const auto now = std::chrono::steady_clock::now();
        auto it = lastAlertAt_.find(reason);
        if (it != lastAlertAt_.end() && now - it->second < std::chrono::seconds(cooldown_sec)) {
            return;
        }
        lastAlertAt_[reason] = now;
    }
    const std::string body = "trust-playground: " + reason + "\nTime: " + utcNowString() + "\n\n" + stats_text;
    const std::string subj = "trust-playground: " + reason;
    // Отправка в отдельном потоке - не блокирует обработчик запроса.
    const std::string cmd = cfg_.alertCmd, from = cfg_.alertFrom, to = cfg_.alertEmail;
    std::thread([cmd, from, to, subj, body] { sendMail(cmd, from, to, subj, body); }).detach();
}

void PlaygroundServer::alertLoop() {
    if (cfg_.alertEmail.empty()) {
        return;
    }
    const int interval = cfg_.alertIntervalSec > 0 ? cfg_.alertIntervalSec : 86400;
    const int steps = 20;
    while (!stop_.load()) {
        for (int i = 0; i < steps && !stop_.load(); ++i) {
            std::this_thread::sleep_for(std::chrono::milliseconds(static_cast<long long>(interval) * 1000 / steps));
        }
        if (stop_.load()) {
            break;
        }
        const std::string body = "trust-playground periodic stats\n"
                                 "Time: " +
                                 utcNowString() + "\n\n" + currentStatsText();
        const std::string cmd = cfg_.alertCmd, from = cfg_.alertFrom, to = cfg_.alertEmail;
        const std::string subj = "trust-playground periodic stats";
        std::thread([cmd, from, to, subj, body] { sendMail(cmd, from, to, subj, body); }).detach();
    }
}

void PlaygroundServer::trackWorkerPresenceLocked() {
    const auto now = std::chrono::steady_clock::now();
    int connected = 0;
    for (const auto& [tok, w] : workers_) {
        if ((now - w.lastSeen) < std::chrono::seconds(cfg_.pollTimeoutSec * 3)) {
            connected++;
        }
    }
    if (connected != connectedWorkersLast_) {
        if (connectedWorkersLast_ > 0 && connected == 0) {
            notifyAlert("all workers disconnected", buildStatsTextLocked()); // переход в «все воркеры отключились»
        } else if (connectedWorkersLast_ == 0 && connected > 0) {
            notifyAlert("workers reconnected", buildStatsTextLocked());
        }
        connectedWorkersLast_ = connected;
    }
}

int PlaygroundServer::run() {
    std::string error;
    const int listen_fd = createListenSocket(cfg_.listen, cfg_.port, error);
    if (listen_fd < 0) {
        trust::errs() << "trust-playground: " << error << "\n";
        return 1;
    }
    trust::errs() << "trust-playground (playground): listening on " << cfg_.listen << ":" << cfg_.port << "\n";
    // Предупреждения о потенциально небезопасной конфигурации.
    if (!trust::playground::isLoopbackHost(cfg_.listen)) {
        trust::errs() << "trust-playground (playground): WARNING: listen=" << cfg_.listen
                      << " is NOT loopback. X-Forwarded-For is NOT trusted (rate-limit/domain binding "
                         "can be spoofed from the outside); run behind nginx bound to 127.0.0.1.\n";
    }
    if (cfg_.allowedOrigins.empty()) {
        trust::errs() << "trust-playground (playground): WARNING: playground.allowed_origins is empty. "
                         "CORS is fail-closed (only loopback origins allowed); set allowed_origins to your "
                         "site domain for production (and allowed_hosts).\n";
    }

    // Периодические письма со статистикой (если настроен alert_email).
    std::thread(&PlaygroundServer::alertLoop, this).detach();

    while (!stop_.load()) {
        struct sockaddr_in client_addr{};
        socklen_t client_len = sizeof(client_addr);
        const int client_fd = ::accept(listen_fd, reinterpret_cast<struct sockaddr*>(&client_addr), &client_len);
        if (client_fd < 0) {
            if (errno == EINTR) {
                continue;
            }
            if (stop_.load()) {
                break;
            }
            trust::errs() << "trust-playground: accept failed: " << std::strerror(errno) << "\n";
            break;
        }

        char ip_buf[INET_ADDRSTRLEN];
        std::string peer = "unknown";
        if (::inet_ntop(AF_INET, &client_addr.sin_addr, ip_buf, sizeof(ip_buf)) != nullptr) {
            peer = ip_buf;
        }

        // Глобальный ЖЁСТКИЙ кап одновременных соединений (потоки/файловые дескрипторы) -
        // защита от DoS «тысячи открытых медленных соединений». Раздельные бюджеты на
        // клиентский и воркерский трафик применяются ПОСЛЕ разбора запроса (см. ниже).
        // Примечание о масштабировании: текущая модель - ПОТОК НА СОЕДИНЕНИЕ, поэтому кап
        // соединений = кап потоков (малый). Фундаментально «конечный ресурс» снимается
        // переходом на событийную модель (epoll/io_uring, без потока на соединение) и/или
        // горизонтальным масштабированием нескольких процессов балансировщика.
        if (!connSlots_.try_acquire()) {
            const std::string busy = serializeHttpResponse({503, "application/json; charset=utf-8", "{\"error\":\"too many connections\"}", true});
            writeAll(client_fd, busy);
            ::close(client_fd);
            continue;
        }
        connsInUse_.fetch_add(1);
        bumpPeak(connsInUse_, connsPeak_);

        // Максимальный размер тела запроса, который вообще может прийти на этот
        // сервер (берём максимум по всем эндпоинтам) - лимит чтения из сокета.
        const size_t max_body_kb = std::max({static_cast<size_t>(cfg_.bodyLimitKb), static_cast<size_t>(cfg_.maxResultKb),
                                             static_cast<size_t>(cfg_.maxWorkerMetricsKb), static_cast<size_t>((cfg_.maxArchiveKb * 4 + 2) / 3), size_t{256}});
        const size_t max_body_bytes = max_body_kb * 1024;

        std::thread([this, client_fd, peer, max_body_bytes] {
            // RAII: дескриптор и ГЛОБАЛЬНЫЙ слот (connSlots_) освобождаются ВСЕГДА, даже
            // если handle()/writeAll() бросит исключение. Иначе при исключении в
            // detached-потоке слот и fd утекали бы, и при флуде исчерпался бы лимит
            // одновременных соединений -> отказ в обслуживании (DoS).
            struct ConnGuard {
                int fd;
                std::counting_semaphore<kMaxConnSemaphore>& slots;
                std::atomic<int>& used;
                ~ConnGuard() {
                    if (fd >= 0) {
                        ::close(fd);
                    }
                    slots.release();
                    used.fetch_sub(1);
                }
            } guard{client_fd, connSlots_, connsInUse_};
            // RAII для РАЗДЕЛЬНОГО (клиентского/воркерского) слота.
            struct ClassSlotGuard {
                std::counting_semaphore<kMaxConnSemaphore>& slots;
                std::atomic<int>& used;
                ~ClassSlotGuard() {
                    slots.release();
                    used.fetch_sub(1);
                }
            };

            try {
                const std::string raw = readRawRequest(client_fd, max_body_bytes);
                HttpResponse resp;
                HttpRequest req;
                if (raw.empty() || !parseHttpRequest(raw, req)) {
                    resp.status = 400;
                    resp.content_type = "application/json; charset=utf-8";
                    resp.body = "{\"error\":\"bad request\"}";
                    resp.cors = true; // 400 может попасть на браузерный запрос
                } else {
                    // Раздельные пулы соединений (защита от само-DoS): воркерские long-poll
                    // (/poll,/result) НЕ выедают клиентский путь (/run,/download,/health,
                    // /challenge,/stats) и наоборот. Каждый класс имеет свой бюджет
                    // (max_client_conns / max_worker_conns), поверх глобального капа max_conns.
                    const bool is_worker = (req.method == "POST") && (req.target == "/poll" || req.target == "/result");
                    auto& class_slots = is_worker ? workerConnSlots_ : clientConnSlots_;
                    auto& class_used = is_worker ? workerConnsInUse_ : clientConnsInUse_;
                    if (!class_slots.try_acquire()) {
                        resp.status = 503;
                        resp.content_type = "application/json; charset=utf-8";
                        resp.body = "{\"error\":\"too many connections\"}";
                        resp.cors = true;
                    } else {
                        class_used.fetch_add(1);
                        if (is_worker) {
                            bumpPeak(workerConnsInUse_, workerConnsPeak_);
                        } else {
                            bumpPeak(clientConnsInUse_, clientConnsPeak_);
                        }
                        ClassSlotGuard cg{class_slots, class_used};
                        resp = handle(req, peer);
                    }
                }
                const std::string out = serializeHttpResponse(resp);
                writeAll(client_fd, out);
            } catch (const std::exception& e) {
                trust::errs() << "trust-playground: connection handler error: " << e.what() << "\n";
            } catch (...) {
                trust::errs() << "trust-playground: connection handler error (unknown)\n";
            }
        }).detach();
    }

    ::close(listen_fd);
    trust::errs() << "trust-playground (playground): exiting\n";
    return 0;
}

} // namespace playground
} // namespace trust
