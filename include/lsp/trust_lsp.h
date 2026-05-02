#ifndef TRUST_TRUST_LSP_H
#define TRUST_TRUST_LSP_H

#include "lsp/lsp_protocol.h"
#include "lsp/transpile.h"
#include "debug/trust_source.h"

#include <nlohmann/json.hpp>

#include <atomic>
#include <memory>
#include <string>
#include <unordered_map>
#include <vector>

// ── Trust LSP Server ──
// Хранит source map + сгенерированные C++ строки для одного файла
struct CachedSource {
    std::unique_ptr<const trust::TrustSource> sourceMap;
    std::vector<std::string> cppLines;
};

class TrustLsp {
  public:
    explicit TrustLsp(LspTransport &transport, const LspOptions &opts);
    ~TrustLsp() = default;

    void handleRequest(const nlohmann::json &req);
    void handleNotification(const nlohmann::json &req);
    bool isRunning() const { return running_; }

  private:
    // LSP handlers
    void handleInitialize(const nlohmann::json &req);
    void handleShutdown(const nlohmann::json &req);
    void handleDidOpen(const nlohmann::json &req);
    void handleDidClose(const nlohmann::json &req);
    void handleDidChange(const nlohmann::json &req);
    void handleDefinition(const nlohmann::json &req);
    void handleHover(const nlohmann::json &req);
    void handleDocumentLink(const nlohmann::json &req);

    // Транспиляция при открытии файла (in-process)
    // Возвращает пустую строку при успехе, текст ошибки при неудаче.
    std::string transpileSourceFile(const std::string &trustFilePath);

    // Диагностика
    void publishDiagnostics(const std::string &uri, const std::vector<std::pair<std::string, std::string>> &errors);

    // Helpers
    bool loadSourceMap(const std::string &trustFilePath);
    void log(const std::string &msg) const;

    LspTransport &transport_;
    LspOptions opts_;

    // Cache: URI → CachedSource (для DidOpen файлов)
    std::unordered_map<std::string, CachedSource> sourceCache_;
    std::string currentTrustFile_;
    const trust::TrustSource *source_ = nullptr;

    std::atomic<bool> running_{true};
};

#endif // TRUST_TRUST_LSP_H