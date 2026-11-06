#pragma once

// Логика автодополнения (textDocument/completion) LSP.
// ЕДИНЫЕ источники имён (без текстовых проходов по документу и пер-файловых копий
// реестра):
//  - пользовательский код (переменные/функции/типы/макросы) - таблица анализатора
//    SymbolIndex (SymbolCollectorHook + реестр макросов);
//  - встроенные типы/методы/функции/макросы - глобальный BuiltinCatalog
//    (shared иммутабельное ядро TypeSystem + predef/DSL-макросы).
// Вставка идёт через textEdit с диапазоном набранного префикса - сигнатура не дублируется.

#include "lsp/catalog/builtin_catalog.h"
#include "symbols/symbol_index.hpp"
#include "types/type_system.hpp"
#include "session/context.hpp"

#include <nlohmann/json.hpp>

#include <string>
#include <string_view>
#include <vector>

struct LspOptions; // глобальный тип (определён в lsp/lsp_protocol.h), НЕ в trust::lsp

namespace trust {
namespace transport {
class Transport;
}
namespace lsp {
class DocumentManager;
namespace completion {

// LSP CompletionItemKind
inline constexpr int kKindMethod = 2;
inline constexpr int kKindVariable = 6;
inline constexpr int kKindClass = 7;
inline constexpr int kKindNamespace = 9;
inline constexpr int kKindField = 5;
inline constexpr int kKindKeyword = 14;

// Снять ведущую сигнатуру (% $ : @) с имени. Единый доменный хелпер: используется
// completion и hover-документацией (сопоставление имени под курсором с SymbolInfo).
std::string_view stripSigil(std::string_view s);

// textDocument/completion: обработчик автодополнения (glue поверх сбора кандидатов и
// сортировки). Вынесен из монолита TrustLsp (src/lsp/catalog/trust_lsp.cpp). Stateless —
// принимает transport/documents/opts и сам отправляет ответ (как hover::/navigation::).
void handleCompletion(trust::transport::Transport& transport, DocumentManager& documents, LspOptions& opts, const nlohmann::json& req);

} // namespace completion
} // namespace lsp
} // namespace trust
