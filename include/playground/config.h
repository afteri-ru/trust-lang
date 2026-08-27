#pragma once
// include/playground/config.h
// trust-playground: единый конфиг для обоих режимов (playground/worker).
//
// Формат: строки key=value; комментарии '#' (полнострочные и строчные - всё от первого
// '#' вне кавычек); пустые строки игнорируются.
// Префиксы ключей: "playground." - настройки балансировщика, "worker." -
// настройки исполнителя. Строка БЕЗ известного префикса - запись реестра
// воркеров вида label=token (токен обязан быть hex, 64 символа / 32 байта).
// Каждый режим читает только свои ключи; чужие игнорируются.

#include <cstdint>
#include <string>
#include <vector>

namespace trust {
namespace playground {

// Запись реестра воркеров (из секции без префикса).
struct WorkerRegistryEntry {
    std::string label; // имя воркера (для идентификации в логах)
    std::string token; // hex-токен (64 символа / 32 байта)
};

struct PlaygroundConfig {
    // -- playground (балансировщик) --
    std::string listen = "127.0.0.1";
    int port = 8080;
    int maxQueue = 256;
    int jobTimeoutSec = 30;
    int bodyLimitKb = 256;
    int rateLimitPerIp = 20;
    int retry = 1;
    int pollTimeoutSec = 15; // long-poll воркера: короткий - быстрее освобождает соединение
    // Пределы одновременных соединений (см. run()). Выведены из ресурсов (capacity plan):
    //  балансировщик 8 ГБ / 8 ядер (I/O-bound, «поток на соединение»);
    //  воркер 256 ГБ / 64 ядра (транспиляция CPU-bound, max_parallel=64, память на транспайл
    //  задаётся worker.max_memory_mb).
    //  maxConns        - глобальный ЖЁСТКИЙ кап (потоки/fd). 2048 соединений ≈ ~2 ГБ
    //                    коммиченной памяти на стеки+буферы - комфортно в 8 ГБ.
    //  maxClientConns  - отдельный бюджет клиентских эндпоинтов (/run,/download,/health,/challenge,/stats).
    //  maxWorkerConns  - отдельный бюджет воркерских (/poll,/result). Должен покрывать
    //                    Σ(max_parallel воркеров): 1024 ≈ 16 воркеров × 64 (или 32 × 32);
    //                    при большем флоте воркеров поднять.
    // Разделение - защита от само-DoS (воркерские long-poll не выедают клиентский путь).
    int maxConns = 2048;
    int maxClientConns = 1024;
    int maxWorkerConns = 1024;
    // Лимиты и ограничения балансировщика (защита от переполнения/DoS).
    int maxArchiveKb = 512;     // макс. размер build-архива (КБ) в /download
    int maxResultKb = 2048;     // макс. размер тела /result (КБ)
    int maxWorkerMetricsKb = 8; // макс. размер метрик воркера в /poll (КБ)
    int maxRateLimitIps = 8192; // макс. уникальных IP в rate-limit

    // -- Доступ к публичным эндпоинтам (только с конкретной песочницы) --
    // Разрешённые Origin (разделитель ','). Пусто - fail-closed: CORS только для
    // loopback-origin (dev), посторонние браузерные Origin -> 403. Заполнение ->
    // /run и /download принимаются только с этих доменов, CORS-ответ отдаётся конкретным origin.
    std::vector<std::string> allowedOrigins;
    // Разрешённые Host (разделитель ','). Пусто - Host не проверяется. Заполнение ->
    // запросы с других Host отклоняются (403) на браузерных эндпоинтах.
    std::vector<std::string> allowedHosts;

    // -- PoW (proof-of-work) на /run и /download: по умолчанию ВЫКЛЮЧЕН (powMinDifficulty==0) --
    int powMinDifficulty = 0;   // минимальная сложность (ведущих нулевых бит); 0 = PoW выключен
    int powMaxDifficulty = 24;  // потолок сложности (режим защиты при флуде)
    int powNonceTtlSec = 60;    // время жизни nonce-челленджа (сек)
    int powMaxUsesPerNonce = 8; // макс. использований одного решённого челленджа (кеш на TTL)

    // -- Кеш транспилированных файлов (/run по SHA-256 кода) --
    int cacheMaxEntries = 256; // макс. записей в кеше
    int cacheMaxMb = 64;       // макс. суммарный размер кеша (МБ)
    int cacheTtlSec = 3600;    // TTL записей кеша (сек)

    // -- Админ-сессия /stats (cookie) --
    int statsSessionTtlSec = 600; // таймаут бездействия админ-сессии (сек); sliding refresh
    int statsSessionMaxSec = 0;   // абсолютный потолок жизни сессии (сек); 0 = выкл

    // -- Алерты (электронная почта при проблемах/изменениях + периодическая статистика) --
    std::string alertEmail;                               // получатель; пусто - алерты отключены
    int alertIntervalSec = 86400;                         // период отправки периодической статистики (сек); default 1 сутки
    std::string alertFrom = "trust-playground@localhost"; // From в письме
    std::string alertCmd = "sendmail -t";                 // команда отправки (читает письмо из stdin)

    // -- worker (исполнитель) --
    // URL балансировщика по умолчанию - публичный playground (переопределяется
    // worker.playground_url в конфиге / --playground-url).
    std::string playgroundUrl = "https://playground.trust-lang.net";
    std::string token;  // собственный токен воркера (hex)
    std::string lspBin; // путь к исполняемому trust-lsp
    // Параллельных задач. Транспиляция CPU-bound => брать по числу ядер (воркер 256 ГБ/64
    // ядра => max_parallel=64; память на транспайл задаётся max_memory_mb). install_worker.sh
    // по умолчанию ставит $(nproc).
    int maxParallel = 4;
    int maxMemoryMb = 512;
    int maxOutputKb = 2048;
    int workerJobTimeoutSec = 30;
    int pollIntervalMs = 200;
    int statsIntervalMs = 10000;      // период вывода статистики в консоль
    std::vector<std::string> lspOpts; // доп. опции, всегда передаваемые в trust-lsp (--json)

    // -- Защита от переполнения диска (воркер) --
    std::string tmpDir;      // worker.tmp_dir; пусто = /tmp. Свой каталог воркера (chmod 0700),
                             // чтобы чистить только своё и не оставлять архивы в общем /tmp.
    int tmpTtlSec = 3600;    // worker.tmp_ttl_sec: TTL осиротевших tmp (старше - чистим)
    int diskFreeMinMb = 512; // worker.disk_free_min_mb: порог свободного места в tmp;
                             // ниже - не принимаем новую задачу (защита от переполнения)

    // -- общее --
    std::string projectDir;
    std::string logLevel = "info";
    std::string statsToken; // токен доступа к GET /stats балансировщика (playground.stats_token)

    // -- реестр воркеров (label -> token), заполняется из строк без префикса --
    std::vector<WorkerRegistryEntry> workers;
};

// Парсит конфиг из файла. При ошибке возвращает false и заполняет error.
bool loadConfig(const std::string& path, PlaygroundConfig& out, std::string& error);

// Проверяет формат токена (ровно 64 hex-символа). Пустая строка - false.
bool isValidToken(const std::string& token);

// Снимает одну пару окружающих одинарных/двойных кавычек, если они есть и не закрывают
// пустую строку. Значения в конфиге/CLI могут приходить в кавычках (например, '8').
std::string unquote(const std::string& s);

// Генерирует новый токен воркера: 32 байта из /dev/urandom → 64 hex-символа.
// При ошибке чтения /dev/urandom возвращает пустую строку (вызывающий обрабатывает как ошибку).
std::string generateToken();

// Проверяет, что host - loopback (localhost / 127.x / ::1). Для loopback разрешено
// http:// (локальная разработка/тесты); для прочих хостов обязателен https://.
bool isLoopbackHost(const std::string& host);

// Валидирует схему playground_url воркера: для не-loopback хоста обязателен https://
// (воркер передаёт по соединению свой auth-токен и полезную нагрузку задач - через
// открытый http они были бы видны/подменяемы). При некорректной схеме возвращает false
// и заполняет error.
bool validateWorkerPlaygroundUrl(const std::string& url, std::string& error);

// Возвращает label воркера по токену из реестра; пусто, если не зарегистрирован.
std::string workerLabelForToken(const PlaygroundConfig& cfg, const std::string& token);

// Сохраняет эффективные worker-настройки в конфиг (путь). Существующие не-worker
// строки (playground.*, токены) сохраняются; worker.* перезаписываются.
bool saveWorkerConfig(const std::string& path, const PlaygroundConfig& cfg, std::string& error);

} // namespace playground
} // namespace trust
