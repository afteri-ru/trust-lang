#pragma once
// include/playground/config.h
// trust-playground: единый конфиг для обоих режимов (playground/worker).
//
// Формат: строки key=value; комментарии '#'; пустые строки игнорируются.
// Префиксы ключей: "playground." — настройки балансировщика, "worker." —
// настройки исполнителя. Строка БЕЗ известного префикса — запись реестра
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
    // ── playground (балансировщик) ──
    std::string listen = "127.0.0.1";
    int port = 8080;
    int maxQueue = 256;
    int jobTimeoutSec = 30;
    int bodyLimitKb = 256;
    int rateLimitPerIp = 20;
    int retry = 1;
    int pollTimeoutSec = 30;
    // Лимиты и ограничения балансировщика (защита от переполнения/DoS).
    int maxArchiveKb = 512;     // макс. размер build-архива (КБ) в /download
    int maxResultKb = 2048;     // макс. размер тела /result (КБ)
    int maxWorkerMetricsKb = 8; // макс. размер метрик воркера в /poll (КБ)
    int maxRateLimitIps = 8192; // макс. уникальных IP в rate-limit

    // ── Алерты (электронная почта при проблемах/изменениях + периодическая статистика) ──
    std::string alertEmail;                               // получатель; пусто — алерты отключены
    int alertIntervalSec = 86400;                         // период отправки периодической статистики (сек); default 1 сутки
    std::string alertFrom = "trust-playground@localhost"; // From в письме
    std::string alertCmd = "sendmail -t";                 // команда отправки (читает письмо из stdin)

    // ── worker (исполнитель) ──
    // URL балансировщика по умолчанию — публичный playground (переопределяется
    // worker.playground_url в конфиге / --playground-url).
    std::string playgroundUrl = "https://playground.trust-lang.net";
    std::string token;  // собственный токен воркера (hex)
    std::string lspBin; // путь к исполняемому trust-lsp
    int maxParallel = 4;
    int maxMemoryMb = 512;
    int maxOutputKb = 2048;
    int workerJobTimeoutSec = 30;
    int pollIntervalMs = 200;
    int statsIntervalMs = 10000;      // период вывода статистики в консоль
    std::vector<std::string> lspOpts; // доп. опции, всегда передаваемые в trust-lsp (--json)

    // ── общее ──
    std::string projectDir;
    std::string logLevel = "info";
    std::string statsToken; // токен доступа к GET /stats балансировщика (playground.stats_token)

    // ── реестр воркеров (label -> token), заполняется из строк без префикса ──
    std::vector<WorkerRegistryEntry> workers;
};

// Парсит конфиг из файла. При ошибке возвращает false и заполняет error.
bool loadConfig(const std::string& path, PlaygroundConfig& out, std::string& error);

// Проверяет формат токена (ровно 64 hex-символа). Пустая строка — false.
bool isValidToken(const std::string& token);

// Генерирует новый токен воркера: 32 байта из /dev/urandom → 64 hex-символа.
// При ошибке чтения /dev/urandom возвращает пустую строку (вызывающий обрабатывает как ошибку).
std::string generateToken();

// Проверяет, что host — loopback (localhost / 127.x / ::1). Для loopback разрешено
// http:// (локальная разработка/тесты); для прочих хостов обязателен https://.
bool isLoopbackHost(const std::string& host);

// Валидирует схему playground_url воркера: для не-loopback хоста обязателен https://
// (воркер передаёт по соединению свой auth-токен и полезную нагрузку задач — через
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
