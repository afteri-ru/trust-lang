#pragma once

// include/lsp/navigation_service.hpp
// Навигация LSP: Go to Definition (textDocument/definition) и кликабельные ссылки
// (textDocument/documentLink). Вынесены из монолита TrustLsp; stateless - принимают
// transport/documents/opts и сами отправляют ответ.

#include "lsp/lsp_options.hpp"
#include "lsp/document_manager.hpp"

#include <nlohmann/json.hpp>

namespace trust {
namespace transport {
class Transport;
}

namespace lsp {
namespace navigation {

/// textDocument/definition: Go to Definition.
void handleDefinition(trust::transport::Transport& transport, DocumentManager& documents, LspOptions& opts, const nlohmann::json& req);
/// textDocument/documentLink: кликабельные ссылки на C++/trust строки.
void handleDocumentLink(trust::transport::Transport& transport, DocumentManager& documents, LspOptions& opts, const nlohmann::json& req);

} // namespace navigation
} // namespace lsp
} // namespace trust
