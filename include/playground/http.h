#ifndef TRUST_PLAYGROUND_SERVER_H
#define TRUST_PLAYGROUND_SERVER_H

#include "lsp/lsp_protocol.h"

#include <string>

namespace trust::playground {

// -- HTTP-модели (ядро без сокетов - юнит-тестируемо) --

struct HttpRequest {
    std::string method;
    std::string target;
    std::string body;
    // Заголовки, извлекаемые при разборе запроса (для доменной привязки, forwarded-IP
    // rate-limit и проверки токена песочницы). Пустые - заголовок не был передан.
    std::string host;          // Host
    std::string origin;        // Origin (браузерный запрос)
    std::string referer;       // Referer (fallback для доменной привязки)
    std::string xForwardedFor; // X-Forwarded-For (первый hop XFF - реальный клиент)
    std::string xRealIp;       // X-Real-IP (fallback к XFF)
    std::string statsTokenHdr; // X-Stats-Token (админ-доступ к /stats JSON)
    std::string xPow;          // X-PoW: "nonce:solution" (proof-of-work)
    std::string cookie;        // Cookie (админ-сессии /stats)
    std::string exampleName;   // X-Example-Name: имя файла примера (для кеша); пусто = произвольный код
};

struct HttpResponse {
    int status = 200;
    std::string content_type = "text/plain; charset=utf-8";
    std::string body;
    bool cors = false;                     // CORS: выводить Access-Control-Allow-*
    std::string corsOrigin;                // если cors && непусто: Access-Control-Allow-Origin: <этот origin>
                                           // (а не '*') - конкретный origin из allowlist.
    std::string content_disposition;       // если непусто: Content-Disposition: attachment; filename="..."
    std::string location;                  // если непусто: Location: <...> (302 redirect)
    std::vector<std::string> extraHeaders; // произвольные доп. заголовки (например Set-Cookie)
};

// Разбирает raw HTTP/1.1-запрос (request line + headers + \r\n\r\n + body).
// Возвращает false при некорректной request line или пустом вводе.
bool parseHttpRequest(const std::string& raw, HttpRequest& out);

// Обрабатывает один запрос и возвращает HTTP-ответ.
//   POST /run    - тело = Trust-код (text/plain) → JSON-контракт lsp/HTML.md
//   GET /health  - {"status":"ok"}
//   OPTIONS /run - 204 preflight (CORS)
// Прочие - 404; пустое тело POST - 400.
HttpResponse handleHttpRequest(const HttpRequest& req, const LspOptions& opts);

// Сериализует HttpResponse в raw HTTP/1.1-ответ (для записи в сокет и тестов).
std::string serializeHttpResponse(const HttpResponse& r);

// -- HTTP-клиент (используется воркером и интеграционными тестами) --

struct HttpResult {
    int status = 0; // 0 - сетевая ошибка / невозможность установить соединение
    std::string body;
};

// Выполняет POST с JSON-телом по URL (http - сырые сокеты, https - curl).
// Возвращает код ответа и тело; при сетевой ошибке status = 0.
HttpResult httpPost(const std::string& url, const std::string& body, int timeout_sec);

// Base64 (RFC 4648) для передачи бинарного build-архива между воркером и балансировщиком.
std::string base64Encode(const std::string& data);
std::string base64Decode(const std::string& data);

// SHA-256 (самодостаточная реализация, без внешних зависимостей). Возвращает hex-строку
// (64 символа). Используется для ключа кеша транспилированных файлов (/run) и для
// проверки proof-of-work челленджа.
std::string sha256Hex(const std::string& data);

// Константно-временное сравнение двух строк (для проверки секретных токенов:
// stats_token воркера/админки), чтобы не раскрывать разницу по времени.
bool constantTimeEqual(const std::string& a, const std::string& b);

} // namespace trust::playground

#endif // TRUST_PLAYGROUND_SERVER_H
