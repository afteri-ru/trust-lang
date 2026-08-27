#ifndef TRUST_TRUST_LSP_H
#define TRUST_TRUST_LSP_H

#include "lsp/lsp_protocol.h"
#include "lsp/document_manager.hpp"
#include "lsp/analysis_service.hpp"
#include "lsp/hover_service.hpp"
#include "lsp/navigation_service.hpp"
#include "diag/context.hpp"
#include "semantic/symbol_index.hpp"
#include "types/registry.hpp"

#include <nlohmann/json.hpp>

#include <atomic>
#include <memory>
#include <string>

// -- Trust LSP Server --
// TrustLsp - чистый диспетчер LSP-методов (маршрутизация handleRequest/handleNotification);
// вся логика обработчиков вынесена в stateless-сервисы:
//   - жизненный цикл документов/кэши            - DocumentManager (lsp/document_manager.hpp);
//   - транспиляция/диагностика                  - AnalysisService (lsp/analysis_service.hpp);
//   - definition/documentLink                   - lsp/navigation (navigation_service.hpp);
//   - hover                                     - lsp/hover (hover_service.hpp);
//   - completion                                - lsp/completion (completion.h);
//   - codeAction                                - lsp/codeaction (code_action.hpp);
//   - formatting                                - lsp/formatting (formatting_service.hpp).
// Имена для автодополнения берутся из ЕДИНЫХ источников:
//  - пользовательский код - таблица анализатора SymbolIndex (CachedSource.symbols);
//  - встроенные типы/методы/функции/макросы - глобальный BuiltinCatalog.
// Пер-файловый реестр (CachedSource.types) хранит только пользовательские типы и
// служит для резолва SymbolInfo::type (TypeId) → методы типа (member-завершение).

class TrustLsp {
  public:
    explicit TrustLsp(trust::transport::Transport& transport, const LspOptions& opts);
    ~TrustLsp() = default;

    void handleRequest(const nlohmann::json& req);
    void handleNotification(const nlohmann::json& req);
    bool isRunning() const { return running_; }

    // Пере-транспиляция документов, чей debounce-период истёк (вызывается главным циклом).
    void flushPendingTranspile();

  private:
    // LSP handlers (только с реальной логикой; stateless-обработчики вызываются
    // напрямую из handleRequest/handleNotification - см. комментарий выше).
    void handleInitialize(const nlohmann::json& req);
    void handleShutdown(const nlohmann::json& req);
    void handleDidChangeConfiguration(const nlohmann::json& req);
    void handleExecuteCommand(const nlohmann::json& req);

    void log(const std::string& msg) const;

    trust::transport::Transport& transport_;
    LspOptions opts_;

    // Кэши и жизненный цикл документов (didOpen/didChange/didClose, debounce, reverse-кэш).
    trust::lsp::DocumentManager documents_;
    // Транспиляция/анализ/диагностика.
    trust::lsp::AnalysisService analysis_;

    std::atomic<bool> running_{true};
};

#endif // TRUST_TRUST_LSP_H