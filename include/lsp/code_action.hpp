#pragma once

// include/lsp/code_action.hpp
// textDocument/codeAction: построение quickfix из fixits диагностики.
// Вынесен из монолита TrustLsp (src/lsp/trust_lsp.cpp). Stateless: принимает только
// нужные зависимости (transport + req) и сам отправляет ответ (как hover::/navigation::).

#include <nlohmann/json.hpp>

namespace trust {
namespace transport {
class Transport;
}

namespace lsp {
namespace codeaction {

/// textDocument/codeAction: быстрые исправления по fixits опубликованных диагностик.
void handleCodeAction(trust::transport::Transport& transport, const nlohmann::json& req);

} // namespace codeaction
} // namespace lsp
} // namespace trust
