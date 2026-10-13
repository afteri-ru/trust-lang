// src/playground/server_endpoints.cpp
// PlaygroundServer: run/poll/result/download endpoints

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

static int64_t randomJobId() {
    const std::string b = trust::playground::randomBytes(sizeof(uint64_t));
    uint64_t v = 0;
    for (size_t i = 0; i < sizeof(uint64_t); ++i) {
        v = (v << 8) | static_cast<uint8_t>(b[i]);
    }
    v &= static_cast<uint64_t>(INT64_MAX);
    return (v == 0) ? 1 : static_cast<int64_t>(v);
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
            if (!powGuard_.verify(req.xPow, diff, cfg_.powNonceTtlSec, cfg_.powMaxUsesPerNonce)) {
                const std::string nonce = powGuard_.issue(diff, cfg_.powNonceTtlSec);
                nlohmann::json j{{"ok", false}, {"error", "proof-of-work required"}, {"nonce", nonce}, {"difficulty", diff}, {"ttl_sec", cfg_.powNonceTtlSec}};
                resp.status = 402;
                resp.body = j.dump();
                return resp;
            }
        }
    }

    // Rate-limit по РЕАЛЬНОМУ клиентскому IP: за nginx peer всегда 127.0.0.1, поэтому
    // используем первый hop X-Forwarded-For (доверяем только с loopback).
    if (rateLimiter_.exceeded(effectiveClientIp(req, peer_ip), static_cast<size_t>(cfg_.maxRateLimitIps), cfg_.rateLimitPerIp)) {
        resp.status = 429;
        resp.body = "{\"error\":\"rate limit exceeded\"}";
        return resp;
    }

    // Кеш примеров: если фронтенд прислал имя примера (X-Example-Name), кешируем ПО ИМЕНИ
    // (без хеширования и LRU - число примеров фиксировано). Пустое имя = произвольный код,
    // сразу на выполнение, без кеша. Заполнение кеша делает воркер при первом запросе.
    // Кеш - отдельный компонент (ResultCache) с собственным мьютексом - обращаемся до mu_.
    const std::string example_key = req.exampleName;
    if (!example_key.empty()) {
        const std::string cached = resultCache_.get(example_key, req.body, cfg_.cacheTtlSec);
        if (!cached.empty()) {
            resp.status = 200;
            resp.body = cached;
            return resp;
        }
    }
    std::unique_lock<std::mutex> lock(mu_);
    if (!hasConnectedWorkerLocked()) {
        alertNotifier_.notify("no workers connected", buildStatsTextLocked(), cfg_.alertIntervalSec);
        nlohmann::json j{{"ok", false}, {"unavailable", true}, {"error", "no workers connected"}, {"instructionsUrl", kInstructionsUrl}};
        resp.status = 503;
        resp.body = j.dump();
        return resp;
    }
    if (static_cast<int>(queue_.size()) >= cfg_.maxQueue) {
        alertNotifier_.notify("queue full", buildStatsTextLocked(), cfg_.alertIntervalSec);
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
            if (!powGuard_.verify(req.xPow, diff, cfg_.powNonceTtlSec, cfg_.powMaxUsesPerNonce)) {
                const std::string nonce = powGuard_.issue(diff, cfg_.powNonceTtlSec);
                nlohmann::json j{{"ok", false}, {"error", "proof-of-work required"}, {"nonce", nonce}, {"difficulty", diff}, {"ttl_sec", cfg_.powNonceTtlSec}};
                resp.status = 402;
                resp.body = j.dump();
                return resp;
            }
        }
    }

    // Флуд-защита: каждое «Скачать» запускает сборку на воркере - ограничиваем частоту
    // по реальному клиентскому IP (первый hop X-Forwarded-For за nginx).
    if (rateLimiter_.exceeded(effectiveClientIp(req, peer_ip), static_cast<size_t>(cfg_.maxRateLimitIps), cfg_.rateLimitPerIp)) {
        resp.status = 429;
        resp.body = "{\"error\":\"rate limit exceeded\"}";
        return resp;
    }

    std::unique_lock<std::mutex> lock(mu_);
    if (!hasConnectedWorkerLocked()) {
        alertNotifier_.notify("no workers connected", buildStatsTextLocked(), cfg_.alertIntervalSec);
        nlohmann::json j{{"ok", false}, {"unavailable", true}, {"error", "no workers connected"}, {"instructionsUrl", kInstructionsUrl}};
        resp.status = 503;
        resp.body = j.dump();
        return resp;
    }
    if (static_cast<int>(queue_.size()) >= cfg_.maxQueue) {
        alertNotifier_.notify("queue full", buildStatsTextLocked(), cfg_.alertIntervalSec);
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
        resp.content_disposition = sanitizeFilename(j.value("archiveName", std::string("trust-lang-") + TRUST_VERSION + "-generated.tar.gz"));
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
    // Фиксируем переходы «все воркеры отключились / восстановились» (под mu_).
    alertNotifier_.onWorkerCountChange(countConnectedLocked(), buildStatsTextLocked(), cfg_.alertIntervalSec);

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
        alertNotifier_.notify("result body too large", buildStatsTextLocked(), cfg_.alertIntervalSec);
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
            resultCache_.put(it->second->exampleName, it->second->code, result, cfg_.cacheTtlSec, static_cast<size_t>(cfg_.cacheMaxEntries),
                             static_cast<size_t>(cfg_.cacheMaxMb));
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

namespace {

// Криптографически случайные байты из /dev/urandom.
// Случайный 64-битный job id (непоследовательный, не угадываемый по соседним).
// Случайные байты берём из общих утилит (playground/server/util.h, randomBytes).

} // namespace

} // namespace playground
} // namespace trust
