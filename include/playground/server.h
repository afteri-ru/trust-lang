#pragma once
// include/playground/server.h
// trust-playground: балансировщик (режим --playground). Принимает запросы
// статического сайта (POST /run), ведёт реестр воркеров и диспетчеризует задачи
// на них через reverse long-poll (/poll, /result). Вычислений не выполняет.

#include "playground/config.h"
#include "playground/http.h"

#include <atomic>
#include <chrono>
#include <condition_variable>
#include <cstdint>
#include <deque>
#include <map>
#include <memory>
#include <mutex>
#include <semaphore>
#include <string>
#include <unordered_map>
#include <vector>

namespace trust {
namespace playground {

// Ссылка на инструкцию «как запустить свой воркер» (ответ «нет воркеров»).
inline constexpr const char* kInstructionsUrl = "https://trust-lang.net/docs/host-a-worker/";

class PlaygroundServer {
  public:
    explicit PlaygroundServer(const PlaygroundConfig& cfg);

    // Обрабатывает один HTTP-запрос и возвращает ответ. peer_ip — для rate-limit.
    HttpResponse handle(const HttpRequest& req, const std::string& peer_ip);

    // Блокирующий accept-цикл; возвращает 0 при чистой остановке.
    int run();

    void requestStop();

  private:
    struct Job {
        int64_t id = 0;
        std::string code;
        std::string workerToken; // воркер, забравший задачу
        std::string result;
        bool buildArchive = false; // true — задача «собрать build-архив» (POST /download); /run не строит архив
        bool done = false;
        bool released = false; // слот воркера освобождён (ровно один раз)
    };

    struct WorkerState {
        std::string token;
        std::string label;
        int capacity = 4;
        int inFlight = 0;
        std::string metrics; // JSON с метриками системы/воркера (из /poll)
        int assigned = 0;    // задач выдано воркеру (с начала работы)
        int completed = 0;   // результатов получено (с начала работы)
        std::chrono::steady_clock::time_point lastSeen{};
    };

    HttpResponse handleRun(const HttpRequest& req, const std::string& peer_ip);
    HttpResponse handlePoll(const HttpRequest& req);
    HttpResponse handleResult(const HttpRequest& req);
    HttpResponse handleDownload(const HttpRequest& req, const std::string& peer_ip);
    HttpResponse handleStats(const HttpRequest& req);

    bool rateLimitExceeded(const std::string& ip);
    bool isWorkerToken(const std::string& token) const;
    // Освобождает слот воркера под задачей (уменьшает inFlight) — не более одного раза.
    void releaseJobSlot(const std::shared_ptr<Job>& job);

    // ── Статистика / алерты ──
    // Строит JSON/текст/HTML статистики (требует удержания mu_). Единая точка — исключает
    // дублирование кода.
    nlohmann::json statsJsonLocked();
    std::string buildStatsJsonLocked();
    std::string buildStatsTextLocked(); // читаемый текст (для писем)
    std::string buildStatsHtmlLocked(); // HTML-страница (GET /stats?format=html)
    std::string currentStatsJson();     // захватывает mu_ и вызывает buildStatsJsonLocked()
    std::string currentStatsText();     // захватывает mu_ и вызывает buildStatsTextLocked()
    std::string currentStatsHtml();     // захватывает mu_ и вызывает buildStatsHtmlLocked()
    // Отправка письма-алерта НЕМЕДЛЕННО при первом появлении события; повтор того же события
    // в течение alert_interval_sec НЕ отправляется (per-reason dedup). НЕ захватывает mu_
    // (вызывается из обработчиков, держащих mu_); stats_text — уже собранная статистика
    // (buildStatsTextLocked), т.к. внутри нельзя взять mu_.
    void notifyAlert(const std::string& reason, const std::string& stats_text);
    // Периодическая отправка письма со статистикой (раз в alertIntervalSec).
    void alertLoop();
    // Фиксирует переходы «все воркеры отключились / восстановились» (в handlePoll под mu_).
    void trackWorkerPresenceLocked();

    PlaygroundConfig cfg_;
    std::mutex mu_;
    std::condition_variable cv_;
    std::map<std::string, WorkerState> workers_;
    std::deque<std::shared_ptr<Job>> queue_;
    std::unordered_map<int64_t, std::shared_ptr<Job>> inFlight_;
    std::map<std::string, std::vector<std::chrono::steady_clock::time_point>> ipHits_;
    // Счётчик сбросов rate-limit карты при переполнении (maxRateLimitIps).
    std::atomic<uint64_t> rateLimitResets_{0};
    // Счётчик запрошенных build-архивов (POST /download — ленивая сборка по коду).
    std::atomic<uint64_t> archivesRequested_{0};
    // Состояние алертов (почта). Отдельный мьютекс: notifyAlert вызывается под mu_,
    // но не должен блокировать запрос на отправке письма.
    std::mutex alertMutex_;
    // Время последней отправки письма по каждому событию (per-reason dedup: повтор того же
    // события в течение alert_interval_sec не шлём). Защищено alertMutex_.
    std::map<std::string, std::chrono::steady_clock::time_point> lastAlertAt_;
    int connectedWorkersLast_{-1}; // -1 = неизвестно (старт) — при первом поллинге не шлём алерт
    std::atomic<bool> stop_{false};
    // Ограничитель числа одновременных соединений: защита от DoS «тысячи
    // открытых медленных соединений» (каждое соединение обрабатывает свой поток).
    std::counting_semaphore<1024> connSlots_{128};
};

} // namespace playground
} // namespace trust
