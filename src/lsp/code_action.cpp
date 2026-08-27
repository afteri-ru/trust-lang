#include "lsp/code_action.hpp"

#include "lsp/lsp_protocol.h"
#include "lsp/lsp_utils.hpp"

using json = nlohmann::json;

namespace trust {
namespace lsp {
namespace codeaction {

void handleCodeAction(trust::transport::Transport& transport, const json& req) {
    json id = req.value("id", json());
    json params = req.value("params", json());
    std::string uri = params.value("textDocument", json()).value("uri", "");
    json actions = json::array();
    try {
        const json& ctxDiags = params.value("context", json()).value("diagnostics", json::array());
        for (const auto& d : ctxDiags) {
            // Достаём fixits из data (стандарт LSP) с fallback на прямое поле "fixits".
            json fixits;
            if (d.contains("data") && d["data"].is_object() && d["data"].contains("fixits") && d["data"]["fixits"].is_array()) {
                fixits = d["data"]["fixits"];
            } else if (d.contains("fixits") && d["fixits"].is_array()) {
                fixits = d["fixits"];
            } else {
                continue;
            }
            std::string message = d.value("message", "diagnostic");
            json changes = json::array();
            for (const auto& f : fixits) {
                json te = {{"range", f["range"]}, {"newText", f.value("replacement", "")}};
                changes.push_back(std::move(te));
            }
            if (changes.empty()) {
                continue;
            }
            json action = {
                {"title", "Fix: " + message}, {"kind", "quickfix"}, {"diagnostics", json::array({d})}, {"edit", {{"changes", {{uri, std::move(changes)}}}}}};
            actions.push_back(std::move(action));
        }
    } catch (const std::exception& e) {
        lspReportHandlerError("codeAction error: " + std::string(e.what()));
    } catch (...) {
        lspReportHandlerError("codeAction error: unknown");
    }
    sendLspResponse(transport, id, actions);
}

} // namespace codeaction
} // namespace lsp
} // namespace trust
