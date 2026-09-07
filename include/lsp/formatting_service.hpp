#pragma once

// include/lsp/formatting_service.hpp
// textDocument/formatting: pretty-print всего документа через formatter_lib.
// Вынесен из монолита TrustLsp (src/lsp/trust_lsp.cpp). Stateless: принимает
// transport/documents/opts и сам отправляет ответ (как hover::/navigation::).

#include "lsp/lsp_options.hpp"
#include "lsp/document_manager.hpp"

#include <nlohmann/json.hpp>

namespace trust {
namespace transport {
class Transport;
}

namespace lsp {
namespace formatting {

/// textDocument/formatting: форматирование документа (pretty-print всего файла).
void handleFormatting(trust::transport::Transport& transport, DocumentManager& documents, LspOptions& opts, const nlohmann::json& req);

} // namespace formatting
} // namespace lsp
} // namespace trust
