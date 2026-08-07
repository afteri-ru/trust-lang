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
// вернёт 400/413); пустая строка — соединение закрылось без данных.
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
            return std::string(); // заголовки не завершились в лимит — bad request
        }
        const auto remain = std::chrono::duration_cast<std::chrono::milliseconds>(deadline - std::chrono::steady_clock::now());
        if (remain.count() <= 0) {
            return std::string(); // таймаут — slowloris
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
            return buf; // превышен суммарный лимит — обработчик вернёт 413
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

// Пишет весь буфер в fd (петля — иначе большой ответ/архив обрезается из-за
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
: cfg_(cfg) {
}

void PlaygroundServer::requestStop() {
    stop_.store(true);
    cv_.notify_all();
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
        resp.status = 204;
        resp.content_type = "";
        resp.body = "";
        resp.cors = true; // preflight для браузерных эндпоинтов (/run, /download)
        return resp;
    }
    if (req.method == "GET" && (req.target == "/health" || req.target == "/health/")) {
        resp.status = 200;
        resp.content_type = "application/json; charset=utf-8";
        resp.body = "{\"status\":\"ok\"}";
        resp.cors = true;
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
    if (req.method == "POST" && req.target.rfind("/download", 0) == 0) {
        return handleDownload(req, peer_ip);
    }
    if (req.method == "GET" && req.target.rfind("/stats", 0) == 0) {
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
    if (rateLimitExceeded(peer_ip)) {
        resp.status = 429;
        resp.body = "{\"error\":\"rate limit exceeded\"}";
        return resp;
    }

    std::unique_lock<std::mutex> lock(mu_);
    if (workers_.empty()) {
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

    // /run возвращает только JSON-контракт. Build-архив НЕ строится здесь — он лениво
    // собирается по отдельному POST /download (отдельный запрос, заново обрабатывает файл).
    resp.status = 200;
    resp.body = job->result;
    return resp;
}

HttpResponse PlaygroundServer::handleDownload(const HttpRequest& req, const std::string& peer_ip) {
    HttpResponse resp;
    resp.content_type = "application/json; charset=utf-8";
    resp.cors = true; // /download вызывается браузерной страницей (XHR) с другого домена

    // POST /download: тело = Trust-код. Архив собирается ЛЕНИВО — отдельный запрос,
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
    // Флуд-защита: каждое «Скачать» запускает сборку на воркере — ограничиваем частоту.
    if (rateLimitExceeded(peer_ip)) {
        resp.status = 429;
        resp.body = "{\"error\":\"rate limit exceeded\"}";
        return resp;
    }

    std::unique_lock<std::mutex> lock(mu_);
    if (workers_.empty()) {
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
            resp.status = 502;
            resp.body = "{\"error\":\"archive not produced\"}";
            return resp;
        }
        const std::string bytes = base64Decode(b64);
        resp.status = 200;
        resp.content_type = "application/gzip";
        resp.cors = true; // XHR (blob) с браузерной страницы на другом домене
        resp.content_disposition = j.value("archiveName", std::string("trust-lang-") + TRUST_VERSION_FULL + "-generated.tar.gz");
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
    // «собрать архив» (POST /download) допустимый размер — с учётом max_archive_kb.
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
    // Принимаем результат только от воркера, которому задача была выдана, —
    // иначе воркер с валидным токеном мог бы подменить результат чужой задачи.
    if (it != inFlight_.end() && it->second->workerToken == token) {
        it->second->result = result;
        it->second->done = true;
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
    // Доступ по токену статистики (отдельный параметр конфигурации). Токен берём до '&',
    // чтобы параметр format=html (и другие) не попадал в значение токена.
    std::string token;
    const size_t q = req.target.find("?token=");
    if (q != std::string::npos) {
        const size_t start = q + 7;
        const size_t end = req.target.find('&', start);
        token = req.target.substr(start, end == std::string::npos ? std::string::npos : end - start);
    }
    if (cfg_.statsToken.empty() || token != cfg_.statsToken) {
        resp.content_type = "application/json; charset=utf-8";
        resp.status = 403;
        resp.body = "{\"error\":\"forbidden\"}";
        return resp;
    }
    // format=html → HTML-страница статистики; иначе JSON (обратная совместимость).
    const bool want_html = req.target.find("format=html") != std::string::npos;
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
      << "th{background:#f6f8fa;}h2{font-size:18px;}.on{color:#16a34a;}.off{color:#dc2626;font-weight:600;}</style></head><body>"
      << "<h1>trust-playground statistics</h1><p>Time: " << utcNowString() << "</p>";
    const auto& b = j["balancer"];
    h << "<h2>Balancer</h2><table>"
      << "<tr><th>workers known</th><td>" << b.value("workers_known", 0) << "</td></tr>"
      << "<tr><th>workers connected</th><td>" << b.value("workers_connected", 0) << "</td></tr>"
      << "<tr><th>queue</th><td>" << b.value("queue", 0) << " / " << b.value("max_queue", 0) << "</td></tr>"
      << "<tr><th>in flight</th><td>" << b.value("in_flight", 0) << "</td></tr>"
      << "<tr><th>rate-limit IPs</th><td>" << b.value("rate_limit_ips_tracked", 0) << " (resets: " << b.value("rate_limit_resets", 0) << ")</td></tr>"
      << "<tr><th>archives requested</th><td>" << b.value("archives_requested", 0) << "</td></tr>"
      << "</table>";
    h << "<h2>Workers</h2><table><tr><th>label</th><th>token</th><th>state</th><th>cap</th><th>in "
         "flight</th><th>assigned</th><th>done</th><th>metrics</th></tr>";
    for (const auto& w : j["workers"]) {
        const bool on = w.value("connected", false);
        h << "<tr><td>" << w.value("label", "") << "</td><td>" << w.value("token_hash", "") << "</td>"
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
    // Отправка в отдельном потоке — не блокирует обработчик запроса.
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

        // Лимит одновременных соединений (защита от DoS).
        if (!connSlots_.try_acquire()) {
            const std::string busy = serializeHttpResponse({503, "application/json; charset=utf-8", "{\"error\":\"too many connections\"}", true});
            writeAll(client_fd, busy);
            ::close(client_fd);
            continue;
        }

        // Максимальный размер тела запроса, который вообще может прийти на этот
        // сервер (берём максимум по всем эндпоинтам) — лимит чтения из сокета.
        const size_t max_body_kb = std::max({static_cast<size_t>(cfg_.bodyLimitKb), static_cast<size_t>(cfg_.maxResultKb),
                                             static_cast<size_t>(cfg_.maxWorkerMetricsKb), static_cast<size_t>((cfg_.maxArchiveKb * 4 + 2) / 3), size_t{256}});
        const size_t max_body_bytes = max_body_kb * 1024;

        std::thread([this, client_fd, peer, max_body_bytes] {
            const std::string raw = readRawRequest(client_fd, max_body_bytes);
            HttpResponse resp;
            HttpRequest req;
            if (raw.empty() || !parseHttpRequest(raw, req)) {
                resp.status = 400;
                resp.content_type = "application/json; charset=utf-8";
                resp.body = "{\"error\":\"bad request\"}";
                resp.cors = true; // 400 может попасть на браузерный запрос
            } else {
                resp = handle(req, peer);
            }
            const std::string out = serializeHttpResponse(resp);
            writeAll(client_fd, out);
            ::close(client_fd);
            connSlots_.release();
        }).detach();
    }

    ::close(listen_fd);
    trust::errs() << "trust-playground (playground): exiting\n";
    return 0;
}

} // namespace playground
} // namespace trust
