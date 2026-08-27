#pragma once

// include/lsp/analysis_service.hpp
// Транспиляция/анализ/диагностика LSP: transpileSource/transpileSourceFile/publishDiagnostics.
// Вынесены из монолита TrustLsp. Владеет opts_/documents_/transport_ ссылками; TrustLsp
// делегирует. Также предоставляет общий конвертер LspOptions → PipelineOpts.

#include "lsp/lsp_options.hpp"
#include "lsp/document_manager.hpp"

#include <string>

namespace trust {
namespace transport {
class Transport;
}
struct PipelineOpts;

namespace lsp {
namespace analysis {

/// Конвертация LspOptions → PipelineOpts (общий для транспиляции и форматтера).
trust::PipelineOpts lspOptsToPipelineOpts(const LspOptions& lspOpts);

} // namespace analysis

/// Анализ/транспиляция + публикация диагностик.
class AnalysisService {
  public:
    AnalysisService(trust::transport::Transport& transport, LspOptions& opts, DocumentManager& documents);

    /// Транспиляция файла с диска. Пустая строка при успехе, текст ошибки при неудаче.
    std::string transpileSourceFile(const std::string& trustFilePath);
    /// Транспиляция текста буфера (без чтения с диска). Пустая строка при успехе, текст ошибки.
    std::string transpileSource(const std::string& trustFilePath, const std::string& trustCode);
    /// Публикация диагностик файла (textDocument/publishDiagnostics).
    void publishDiagnostics(const std::string& uri);

  private:
    void log(const std::string& msg) const;

    trust::transport::Transport& transport_;
    LspOptions& opts_;
    DocumentManager& documents_;
};

} // namespace lsp
} // namespace trust
