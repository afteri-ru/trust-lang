#pragma once

// Владение кэшами и жизненным циклом документов LSP (didOpen/didChange/didClose, debounce,
// reverse-кэш). Вынесен из монолита TrustLsp (src/lsp/catalog/trust_lsp.cpp). TrustLsp делегирует
// сюда документ-операции; кэши доступны через accessor'ы (для transpile/publishDiagnostics).
// Транспиляция/публикация диагностик инжектится обратными вызовами (AnalysisService).

#include "lsp/catalog/lsp_options.hpp"

#include "semantic/semantic_model.hpp"
#include "session/context.hpp"
#include "types/type_system.hpp"

#include <nlohmann/json.hpp>

#include <chrono>
#include <functional>
#include <memory>
#include <optional>
#include <string>
#include <unordered_map>
#include <utility>
#include <vector>

namespace trust {
namespace lsp {

// Хранит source map + сгенерированные C++ строки для одного файла.
// Имена для автодополнения берутся из ЕДИНЫХ источников (SemanticModel + BuiltinCatalog);
// пер-файловая модель владеет типовой системой (SymbolInfo::type валиден).
struct CachedSource {
    std::unique_ptr<trust::Context> sourceMap;
    std::string cppFilePath;          // полный путь к .cpp файлу на диске (если tempDir задан)
    trust::ReaderFile trustReaderIdx; // input (trust) file index
    trust::ReaderFile cppReaderIdx;   // output (cpp) file index
    /// Продукт семантики: persistent-таблица символов + проекции (символы/видимость/ссылки).
    /// Может отсутствовать, если шаг семантики не выполнялся (fatal лексера/парсера).
    std::optional<trust::SemanticModel> model;
};

/// Реестр открытых документов и их кэшей + жизненный цикл (didOpen/Change/Close, debounce).
class DocumentManager {
  public:
    using TranspileFn = std::function<std::string(const std::string& trustFilePath, const std::string& code)>;
    using PublishFn = std::function<void(const std::string& uri)>;
    using LogFn = std::function<void(const std::string& msg)>;

    void setCallbacks(TranspileFn transpile, PublishFn publish, LogFn log) {
        transpile_ = std::move(transpile);
        publish_ = std::move(publish);
        log_ = std::move(log);
    }

    std::unordered_map<std::string, CachedSource>& sourceCache() { return sourceCache_; }
    const std::unordered_map<std::string, CachedSource>& sourceCache() const { return sourceCache_; }
    std::unordered_map<std::string, std::string>& cppToTrustCache() { return cppToTrustCache_; }
    const std::unordered_map<std::string, std::string>& cppToTrustCache() const { return cppToTrustCache_; }
    std::unordered_map<std::string, std::string>& openDocuments() { return openDocuments_; }
    const std::unordered_map<std::string, std::string>& openDocuments() const { return openDocuments_; }
    std::unordered_map<std::string, std::chrono::steady_clock::time_point>& pendingTranspile() { return pendingTranspile_; }

    void didOpen(const nlohmann::json& req);
    void didClose(const nlohmann::json& req);
    void didChange(const nlohmann::json& req);
    void flushDocument(const std::string& filePath);
    void flushPendingTranspile();
    /// Применяет contentChanges (Incremental/Full) к буферу; возвращает актуальный текст.
    std::string applyContentChanges(const std::string& filePath, const nlohmann::json& contentChanges);

    // Читатель и FileIdx из кеша по пути (для hover/definition/documentLink/completion).
    struct CachedReader {
        const trust::SourceMapReader* reader;
        trust::ReaderFile trustReaderIdx; // input (trust) file index
        trust::ReaderFile cppReaderIdx;   // output (cpp) file index
        bool isCppRequest;                // true - запрос из C++ файла (курсор в cpp)
    };
    CachedReader getCachedReader(const std::string& filePath, std::string& outError);

    static constexpr int kDebounceMs = 200;

  private:
    void log(const std::string& msg) const {
        if (log_) {
            log_(msg);
        }
    }

    std::unordered_map<std::string, CachedSource> sourceCache_;
    std::unordered_map<std::string, std::string> cppToTrustCache_;
    std::unordered_map<std::string, std::string> openDocuments_;
    std::unordered_map<std::string, std::chrono::steady_clock::time_point> pendingTranspile_;

    TranspileFn transpile_;
    PublishFn publish_;
    LogFn log_;
};

} // namespace lsp
} // namespace trust
