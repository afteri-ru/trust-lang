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

// Верхняя граница счётчика семафора для соединений (compile-time; это потолок типа
// std::counting_semaphore<N>). Меняется с пересборкой. Эффективный лимит задаётся из
// конфига (max_conns/max_client_conns/max_worker_conns) и клампится в [1, kMaxConnSemaphore];
// при превышении в конструкторе выводится предупреждение.
inline constexpr int kMaxConnSemaphore = 2048;

// Ссылка на инструкцию «как запустить свой воркер» / статью о песочнице (ответ «нет воркеров»).
inline constexpr const char* kInstructionsUrl = "https://trust-lang.net/docs/sandbox/";

class PlaygroundServer {
  public:
    explicit PlaygroundServer(const PlaygroundConfig& cfg);

    // Обрабатывает один HTTP-запрос и возвращает ответ. peer_ip - для rate-limit.
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
        bool buildArchive = false; // true - задача «собрать build-архив» (POST /download); /run не строит архив
        bool done = false;
        bool released = false;   // слот воркера освобождён (ровно один раз)
        int attempts = 0;        // число переназначений задачи (retry)
        std::string exampleName; // имя файла примера (для кеша); пусто - произвольный код, не кешируем
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
    HttpResponse handleChallenge(const HttpRequest& req);
    HttpResponse handleStatsLogin(const HttpRequest& req);
    HttpResponse handleStatsLogout(const HttpRequest& req);

    bool rateLimitExceeded(const std::string& ip);
    bool isWorkerToken(const std::string& token) const;
    // Есть ли хотя бы один ПОДКЛЮЧЁННЫЙ воркер (поллил в пределах pollTimeoutSec*3).
    // Требует mu_. Если подключённых нет - /run и /download отвечают «нет воркеров» СРАЗУ,
    // не ставя задачу в очередь и не дожидаясь таймаута.
    bool hasConnectedWorkerLocked() const;
    // Освобождает слот воркера под задачей (уменьшает inFlight) - не более одного раза.
    void releaseJobSlot(const std::shared_ptr<Job>& job);

    // -- Доступ только с конкретной песочницы --
    // Проверяет Origin (и Host) публичных браузерных эндпоинтов против allowlist.
    // Возвращает false, если запрос нужно отклонить (403). Если allowlist пуст - true.
    bool browserOriginAllowed(const HttpRequest& req) const;
    // Возвращает origin для CORS-ответа (совпавший с allowlist или loopback) или пусто (→ без ACAO).
    std::string corsOriginFor(const HttpRequest& req) const;
    // Реальный IP клиента: если peer - loopback (за nginx), берём первый hop X-Forwarded-For
    // (или X-Real-IP); иначе - peer (XFF нельзя доверять с внешнего адреса).
    std::string effectiveClientIp(const HttpRequest& req, const std::string& peer_ip) const;

    // -- Кеш примеров (/run, ключ = имя примера) --
    // Требуют удержания mu_. getCached возвращает результат ТОЛЬКО если код совпадает
    // с закешированным (защита от «отравления» кеша произвольным телом под именем примера);
    // пусто - записи нет/истекла/код не совпал.
    std::string cacheGetLocked(const std::string& key, const std::string& code);
    void cachePutLocked(const std::string& key, const std::string& code, const std::string& result);
    void cacheEvictLocked(); // вытеснение по лимитам (entries / mb / ttl)

    // -- PoW --
    // Вычисляет текущую сложность (ведущих нулевых бит) из нагрузки. 0 = PoW выключен.
    int currentPowDifficulty();
    // Выпускает новый челлендж (nonce), возвращает его; требует mu_.
    std::string issuePowChallengeLocked(int difficulty);
    // Проверяет X-PoW (nonce:solution). Потребляет nonce (лимит использований). Требует mu_.
    bool verifyPowLocked(const std::string& header, int required_difficulty);

    // -- Админ-сессии /stats --
    // Проверяет cookie-сессию (sliding refresh + потолок). Требует mu_.
    bool statsSessionOkLocked(const std::string& session_id);
    // Создаёт сессию, возвращает session_id (случайный hex). Требует mu_.
    std::string createStatsSessionLocked();
    void destroyStatsSessionLocked(const std::string& session_id);
    // Извлекает session_id из Cookie-заголовка (tpg_stats=<id>; ...).
    static std::string cookieSessionId(const HttpRequest& req);
    // Экранирует строку для безопасной вставки в HTML (/stats?format=html, формы логина).
    static std::string htmlEscape(const std::string& s);
    // Санитизация имени скачиваемого файла (только [A-Za-z0-9._-]).
    static std::string sanitizeFilename(const std::string& s);

    // -- Статистика / алерты --
    // Строит JSON/текст/HTML статистики (требует удержания mu_). Единая точка - исключает
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
    // (вызывается из обработчиков, держащих mu_); stats_text - уже собранная статистика
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
    // Кеш транспилированных файлов /run: SHA-256 кода -> результат (JSON-контракт).
    // Под mu_. Вытеснение LRU по cache_max_entries / cache_max_mb / cache_ttl_sec.
    struct CacheEntry {
        std::string code; // код, для которого закеширован результат (защита от отравления кеша)
        std::string result;
        size_t size = 0;
        std::chrono::steady_clock::time_point created{};
        std::chrono::steady_clock::time_point lastAccess{};
    };
    std::unordered_map<std::string, CacheEntry> cache_;
    // PoW-челленджи: nonce -> {сложность, created, uses}. Под mu_. TTL pow_nonce_ttl_sec.
    struct PowChallenge {
        int difficulty = 0;
        std::chrono::steady_clock::time_point created{};
        int uses = 0;
    };
    std::unordered_map<std::string, PowChallenge> powChallenges_;
    // Админ-сессии /stats: session_id -> {created, lastAccess}. Под mu_. TTL sliding.
    struct StatsSession {
        std::chrono::steady_clock::time_point created{};
        std::chrono::steady_clock::time_point lastAccess{};
    };
    std::unordered_map<std::string, StatsSession> statsSessions_;
    // Счётчик запрошенных build-архивов (POST /download - ленивая сборка по коду).
    std::atomic<uint64_t> archivesRequested_{0};
    // Состояние алертов (почта). Отдельный мьютекс: notifyAlert вызывается под mu_,
    // но не должен блокировать запрос на отправке письма.
    std::mutex alertMutex_;
    // Время последней отправки письма по каждому событию (per-reason dedup: повтор того же
    // события в течение alert_interval_sec не шлём). Защищено alertMutex_.
    std::map<std::string, std::chrono::steady_clock::time_point> lastAlertAt_;
    int connectedWorkersLast_{-1}; // -1 = неизвестно (старт) - при первом поллинге не шлём алерт
    std::atomic<bool> stop_{false};
    // -- Пределы одновременных соединений (раздельные пулы, см. run()) --
    // Инициализируются из конфига в конструкторе.
    // Глобальный ЖЁСТКИЙ кап на ВСЕ соединения (потоки/файловые дескрипторы) - защита от
    // DoS «тысячи открытых медленных соединений» (каждое соединение обрабатывает поток).
    std::counting_semaphore<kMaxConnSemaphore> connSlots_;
    // Отдельный бюджет на клиентские эндпоинты (/run,/download,/health,/challenge,/stats).
    std::counting_semaphore<kMaxConnSemaphore> clientConnSlots_;
    // Отдельный бюджет на воркерские (/poll,/result). Разделение - защита от само-DoS:
    // long-poll воркеров не выедает клиентский путь и наоборот (см. комментарий в run()).
    std::counting_semaphore<kMaxConnSemaphore> workerConnSlots_;
    // Текущее занятое число соединений и ПИК утилизации (для статистики): глобальное,
    // клиентское, воркерское. Лимиты известны из конфига, поэтому в статистике показываем
    // текущее + пиковое значение (и процент от лимита), а не сам лимит.
    std::atomic<int> connsInUse_{0};
    std::atomic<int> clientConnsInUse_{0};
    std::atomic<int> workerConnsInUse_{0};
    std::atomic<int> connsPeak_{0};
    std::atomic<int> clientConnsPeak_{0};
    std::atomic<int> workerConnsPeak_{0};
};

} // namespace playground
} // namespace trust
