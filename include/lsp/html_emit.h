#ifndef TRUST_LSP_HTML_EMIT_H
#define TRUST_LSP_HTML_EMIT_H

#include "lsp/lsp_protocol.h"

#include <string>
#include <vector>

namespace trust::lsp {

// ── Результат in-process транспиляции Trust → C++ + построчный source-map ──
// Это «live-контракт» двухоконного playground (godbolt-стиль): страница
// запрашивает его у отдельного сервера при каждом изменении исходника.
struct HtmlResult {
    std::string source;                         // исходный Trust-текст
    std::string cpp;                            // сгенерированный C++ (полный вывод, с autogen-заголовком/include)
    std::vector<std::vector<int>> trust_to_cpp; // индекс = номер строки Trust (1-based) → строки C++
    std::vector<std::vector<int>> cpp_to_trust; // индекс = номер строки C++ (1-based) → строки Trust
    bool ok = true;                             // успешна ли транспиляция
    std::string error;                          // текст ошибки при неудаче (диагностика)
};

// Значение по умолчанию для --monaco-url (официальный CDN, полная сборка Monaco).
inline constexpr const char* kDefaultMonacoUrl = "https://cdn.jsdelivr.net/npm/monaco-editor@0.52.2/min/vs";

// In-process транспиляция Trust-кода в C++ с построчным маппингом.
// file_name — идентификатор исходника (ключ/имя для диагностики).
HtmlResult transpileToResult(const std::string& trust_code, const std::string& file_name, const LspOptions& opts);

// JSON-представление результата:
//   {"source":..,"cpp":..,"ok":..,"error":..,"trustToCpp":[[..],..],"cppToTrust":[[..],..]}
std::string resultToJson(const HtmlResult& r);

// HTML-фрагмент godbolt-стиля: два Monaco-редактора (Trust | C++ read-only)
// + встроенный glue-JS (Monarch-токенайзер Trust, инициализация, синхронная
// навигация между окнами, debounced живая пере-транспиляция через fetch).
// monaco_url — базовый путь сборки Monaco (AMD "vs"). server_url — endpoint,
// который по POST (Trust-код как текст) возвращает resultToJson(); пустой —
// живая пере-транспиляция отключена. full_page — обернуть в полную HTML-страницу.
std::string resultToHtml(const HtmlResult& r, const LspOptions& opts, const std::string& monaco_url = kDefaultMonacoUrl, const std::string& server_url = "",
                         bool full_page = false);

// Загружает все *.src из каталога (сортировка по имени) как список примеров
// для комбобокса playground. Имя = basename без расширения. Несуществующий
// каталог или файлы без .src игнорируются (возвращается пустой список).
std::vector<LspExample> loadExamplesFromDir(const std::string& dir);

// Экранирование строки как JSON-строкового литерала (для встраивания в JS).
std::string jsonEscape(const std::string& s);

} // namespace trust::lsp

#endif // TRUST_LSP_HTML_EMIT_H
