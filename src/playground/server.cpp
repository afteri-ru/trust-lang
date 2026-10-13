// src/playground/server.cpp
// trust-playground: реализация балансировщика (см. include/playground/server/server.h).

#include "playground/server/server.h"
#include "playground/server/util.h"
#include "trust/version.h"
#include "utils/error.hpp"
#include "utils/utils.hpp"

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

} // namespace

PlaygroundServer::PlaygroundServer(const PlaygroundConfig& cfg)
: cfg_(cfg)
// Порядок инициализации = порядок объявления полей в server.h (rateLimiter_/resultCache_ идут
// до alertNotifier_, семафоры слотов — после него): иначе -Wreorder-ctor (с -Werror).
, alertNotifier_(cfg.alertCmd, cfg.alertFrom, cfg.alertEmail)
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

int PlaygroundServer::countConnectedLocked() const {
    const auto now = std::chrono::steady_clock::now();
    int connected = 0;
    for (const auto& [tok, w] : workers_) {
        (void)tok;
        if ((now - w.lastSeen) < std::chrono::seconds(cfg_.pollTimeoutSec * 3)) {
            connected++;
        }
    }
    return connected;
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

void PlaygroundServer::alertLoop() {
    if (!alertNotifier_.enabled()) {
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
        alertNotifier_.sendPeriodic(currentStatsText());
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
            utils::writeAllToFd(client_fd, busy);
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
            // если handle()/utils::writeAllToFd() бросит исключение. Иначе при исключении в
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
                utils::writeAllToFd(client_fd, out);
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
