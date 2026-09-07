#pragma once
// include/playground/server.h
// trust-playground: балансировщик (режим --playground). Принимает запросы
// статического сайта (POST /run), ведёт реестр воркеров и диспетчеризует задачи
// на них через reverse long-poll (/poll, /result). Вычислений не выполняет.

#include "playground/alert_notifier.h"
#include "playground/config.h"
#include "playground/http.h"
#include "playground/pow_guard.h"
#include "playground/rate_limiter.h"
#include "playground/result_cache.h"
#include "playground/stats_session_store.h"

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

    bool isWorkerToken(const std::string& token) const;
    // Есть ли хотя бы один ПОДКЛЮЧЁННЫЙ воркер (поллил в пределах pollTimeoutSec*3).
    // Требует mu_. Если подключённых нет - /run и /download отвечают «нет воркеров» СРАЗУ,
    // не ставя задачу в очередь и не дожидаясь таймаута.
    bool hasConnectedWorkerLocked() const;
    // Число подключённых воркеров (поллили в пределах pollTimeoutSec*3). Требует mu_.
    // Используется для отслеживания переходов «все воркеры отключились / восстановились».
    int countConnectedLocked() const;
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

    // Вычисляет текущую сложность (ведущих нулевых бит) из нагрузки. 0 = PoW выключен.
    // Сама выдача/проверка челленджей - в компоненте PowGuard (powGuard_).
    int currentPowDifficulty();

    // -- Админ-сессии /stats --
    // Сессии хранятся в компоненте StatsSessionStore (statsSessions_).
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
    // Периодическая отправка письма со статистикой (раз в alertIntervalSec). Алерты по
    // событиям и переходы «все воркеры отключились / восстановились» - в AlertNotifier
    // (alertNotifier_): notify() / onWorkerCountChange().
    void alertLoop();

    PlaygroundConfig cfg_;
    std::mutex mu_;
    std::condition_variable cv_;
    std::map<std::string, WorkerState> workers_;
    std::deque<std::shared_ptr<Job>> queue_;
    std::unordered_map<int64_t, std::shared_ptr<Job>> inFlight_;
    // Счётчик запрошенных build-архивов (POST /download - ленивая сборка по коду).
    std::atomic<uint64_t> archivesRequested_{0};
    std::atomic<bool> stop_{false};
    // -- Сервисы (вынесенные зоны ответственности, каждая с собственным мьютексом) --
    RateLimiter rateLimiter_;         // rate-limit по IP (ipHits_/rateLimitResets_)
    ResultCache resultCache_;         // LRU-кеш результатов /run (cache_)
    StatsSessionStore statsSessions_; // админ-сессии /stats (statsSessions_)
    PowGuard powGuard_;               // PoW-челленджи /run и /download (powChallenges_)
    AlertNotifier alertNotifier_;     // почтовые алерты (alertMutex_/lastAlertAt_/connectedWorkersLast_)
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
