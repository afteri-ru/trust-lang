#ifndef TRUST_TRUST_LSP_H
#define TRUST_TRUST_LSP_H

#include "lsp/lsp_protocol.h"
#include "lsp/transpile.h"
#include "diag/context.hpp"

#include <nlohmann/json.hpp>

#include <atomic>
#include <memory>
#include <string>
#include <unordered_map>
#include <vector>

// ── Trust LSP Server ──
// Хранит source map + сгенерированные C++ строки для одного файла
struct CachedSource {
    std::unique_ptr<trust::Context> sourceMap;
    std::string cppOutput;
    std::string cppFilePath;          // полный путь к .cpp файлу на диске (если tempDir задан)
    trust::ReaderFile trustReaderIdx; // input (trust) file index
    trust::ReaderFile cppReaderIdx;   // output (cpp) file index
};

class TrustLsp {
  public:
    explicit TrustLsp(trust::transport::Transport& transport, const LspOptions& opts);
    ~TrustLsp() = default;

    void handleRequest(const nlohmann::json& req);
    void handleNotification(const nlohmann::json& req);
    bool isRunning() const { return running_; }

  private:
    // LSP handlers
    void handleInitialize(const nlohmann::json& req);
    void handleShutdown(const nlohmann::json& req);
    void handleDidOpen(const nlohmann::json& req);
    void handleDidClose(const nlohmann::json& req);
    void handleDidChange(const nlohmann::json& req);
    void handleDefinition(const nlohmann::json& req);
    void handleHover(const nlohmann::json& req);
    void handleDocumentLink(const nlohmann::json& req);
    void handleDidChangeConfiguration(const nlohmann::json& req);

    // Транспиляция при открытии файла (in-process)
    // Возвращает пустую строку при успехе, текст ошибки при неудаче.
    std::string transpileSourceFile(const std::string& trustFilePath);

    // Вспомогательные: читают читателя и FileIdx из кеша по пути
    // Возвращают nullptr при ошибке
    struct CachedReader {
        const trust::SourceMapReader* reader;
        trust::ReaderFile trustReaderIdx; // input (trust) file index
        trust::ReaderFile cppReaderIdx;   // output (cpp) file index
        bool isCppRequest;                // true — запрос из C++ файла (курсор в cpp)
    };
    CachedReader getCachedReader(const std::string& filePath, std::string& outError);

    // Универсальный построитель содержимого ховера
    // Строит Markdown-массив с базовым кодом + Markdown-ссылками на определения,
    // используя getWordAt() для выделения имени под курсором и поиска в NameMap.
    nlohmann::json buildHoverContents(const trust::SourceMapReader& reader, bool isCppRequest, const trust::SourceMapReader::Location& cursorLoc,
                                      const std::string& hoverText, const std::string& hoverLang, const std::string& trustFilePath,
                                      const std::string& cppFilePath);

    // Диагностика
    void publishDiagnostics(const std::string& uri);

    void log(const std::string& msg) const;

    trust::transport::Transport& transport_;
    LspOptions opts_;

    // Cache: URI → CachedSource (для DidOpen файлов)
    std::unordered_map<std::string, CachedSource> sourceCache_;

    // Reverse cache: cppFilePath → trustFilePath (для C++ → Trust навигации)
    std::unordered_map<std::string, std::string> cppToTrustCache_;

    std::atomic<bool> running_{true};
};

#endif // TRUST_TRUST_LSP_H