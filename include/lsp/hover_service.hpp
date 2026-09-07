#pragma once

// include/lsp/hover_service.hpp
// Hover (textDocument/hover) вынесен из монолита TrustLsp: построение содержимого
// (buildHoverContents) + обработчик. Stateless - принимают transport/documents/opts.

#include "lsp/lsp_options.hpp"
#include "lsp/document_manager.hpp"
#include "diag/mapper.hpp"

#include <nlohmann/json.hpp>

#include <string>

namespace trust {
namespace transport {
class Transport;
}

namespace lsp {
namespace hover {

/// Универсальный построитель содержимого ховера (Markdown-массив).
nlohmann::json buildHoverContents(const trust::SourceMapReader& reader, bool isCppRequest, const trust::SourceMapReader::Location& cursorLoc,
                                  const std::string& hoverText, const std::string& hoverLang, const std::string& trustFilePath, const std::string& cppFilePath,
                                  LspOptions& opts);

/// textDocument/hover.
void handleHover(trust::transport::Transport& transport, DocumentManager& documents, LspOptions& opts, const nlohmann::json& req);

} // namespace hover
} // namespace lsp
} // namespace trust
