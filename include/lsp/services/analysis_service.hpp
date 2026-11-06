#pragma once

// Транспиляция/анализ/диагностика LSP: transpileSource/transpileSourceFile/publishDiagnostics.
// Вынесены из монолита TrustLsp. Владеет opts_/documents_/transport_ ссылками; TrustLsp
// делегирует. Также предоставляет общий конвертер LspOptions → PipelineOpts.

#include "lsp/catalog/lsp_options.hpp"
#include "lsp/services/document_manager.hpp"

#include <map>
#include <set>
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
    /// URI, в которые документ уже публиковал диагностики: при повторном анализе исчезнувшие URI
    /// гасятся пустым массивом.
    std::map<std::string, std::set<std::string>> publishedUrisByDocument_;
    /// Владелец URI с кросс-файловыми диагностиками (последний опубликовавший документ): чужой
    /// документ их не гасит.
    std::map<std::string, std::string> uriOwners_;
};

} // namespace lsp
} // namespace trust
