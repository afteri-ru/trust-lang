#ifndef TRUST_PLAYGROUND_SERVER_H
#define TRUST_PLAYGROUND_SERVER_H

#include "lsp/lsp_protocol.h"

#include <string>

namespace trust::playground {

// ── HTTP-модели (ядро без сокетов — юнит-тестируемо) ──

struct HttpRequest {
    std::string method;
    std::string target;
    std::string body;
};

struct HttpResponse {
    int status = 200;
    std::string content_type = "text/plain; charset=utf-8";
    std::string body;
    bool cors = false;               // CORS: Access-Control-Allow-Origin: * — только на публичных браузерных эндпоинтах (/run, /download, /health, preflight);
                                     // токен-защищённые/воркерские (/stats, /poll, /result) — без него
    std::string content_disposition; // если непусто: Content-Disposition: attachment; filename="..."
};

// Разбирает raw HTTP/1.1-запрос (request line + headers + \r\n\r\n + body).
// Возвращает false при некорректной request line или пустом вводе.
bool parseHttpRequest(const std::string& raw, HttpRequest& out);

// Обрабатывает один запрос и возвращает HTTP-ответ.
//   POST /run    — тело = Trust-код (text/plain) → JSON-контракт lsp/HTML.md
//   GET /health  — {"status":"ok"}
//   OPTIONS /run — 204 preflight (CORS)
// Прочие — 404; пустое тело POST — 400.
HttpResponse handleHttpRequest(const HttpRequest& req, const LspOptions& opts);

// Сериализует HttpResponse в raw HTTP/1.1-ответ (для записи в сокет и тестов).
std::string serializeHttpResponse(const HttpResponse& r);

// ── HTTP-клиент (используется воркером и интеграционными тестами) ──

struct HttpResult {
    int status = 0; // 0 — сетевая ошибка / невозможность установить соединение
    std::string body;
};

// Выполняет POST с JSON-телом по URL (http — сырые сокеты, https — curl).
// Возвращает код ответа и тело; при сетевой ошибке status = 0.
HttpResult httpPost(const std::string& url, const std::string& body, int timeout_sec);

// Base64 (RFC 4648) для передачи бинарного build-архива между воркером и балансировщиком.
std::string base64Encode(const std::string& data);
std::string base64Decode(const std::string& data);

} // namespace trust::playground

#endif // TRUST_PLAYGROUND_SERVER_H
