#ifndef TRUST_LSP_PROTOCOL_H
#define TRUST_LSP_PROTOCOL_H

#include <string>
#include <vector>

#include "utils/transport.hpp"
#include <nlohmann/json.hpp>

// ── Опции командной строки ──
// Режимы работы trust-lsp:
enum class LspMode {
    Interactive, ///< LSP поверх stdin/stdout (по умолчанию)
    Server,      ///< LSP поверх TCP (server[=<port>])
    Json,        ///< Транспиляция Trust→C++ + построчный map → JSON в stdout (--json)
    Html,        ///< Транспиляция → godbolt-стиль HTML-фрагмент в stdout (--html)
};

// Пример playground: имя (label) + исходный Trust-текст. Список примеров
// встраивается в HTML-фрагмент как статический блок (комбобокс выбора).
struct LspExample {
    std::string name;   // имя примера (basename без расширения)
    std::string source; // полный исходный Trust-текст примера
};

struct LspOptions {
    LspMode mode = LspMode::Interactive;
    int port = -1;                         // -1 = interactive (stdin/stdout), >0 = TCP server
    std::string inputFile;                 // для Json/Html: путь к .src; пусто или "-" = читать stdin
    std::string projectDir;                // рабочая директория проекта
    std::string tempDir;                   // каталог для временных транспилированных .cpp файлов
    std::string emitBuildDir;              // --emit-build-dir: собрать tar.gz build-каталога (без компиляции) в этот каталог
    std::string monacoUrl;                 // базовый путь сборки Monaco (AMD "vs") для --html
    std::string serverUrl;                 // endpoint живого запуска (POST Trust-код → JSON) для --html
    std::string examplesDir;               // --examples-dir: каталог с *.src для комбобокса примеров
    std::vector<LspExample> examples;      // загруженный список примеров (для HTML-эмиссии)
    std::vector<std::string> pipelineArgs; // доп. опции (-W...), передаваемые в pipeline (--json/--html)
    bool htmlFull = false;                 // --html-full: обернуть фрагмент в полную HTML-страницу
    bool trace = false;                    // трассировка LSP
    bool help = false;
};

static constexpr int LSP_DEFAULT_PORT = 4712;

// ── LSP Protocol helpers ──
// Принимают trust::transport::Transport& (см. utils/transport.hpp)
nlohmann::json readLspPacket(trust::transport::Transport& transport);
void sendLspResponse(trust::transport::Transport& transport, const nlohmann::json& id, const nlohmann::json& result);
void sendLspError(trust::transport::Transport& transport, const nlohmann::json& id, int code, const std::string& message);
void sendLspNotification(trust::transport::Transport& transport, const std::string& method, const nlohmann::json& params);
void sendLspRequest(trust::transport::Transport& transport, const std::string& method, const nlohmann::json& params);

// ── CLI parsing ──
LspOptions parseLspOptions(int argc, const char* argv[]);
void printLspUsage(const char* prog);

#endif // TRUST_LSP_PROTOCOL_H
