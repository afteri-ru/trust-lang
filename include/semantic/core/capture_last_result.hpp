#pragma once

// ПРЕ-СЕМАНТИЧЕСКИЙ проход «$^ = результат последней операции»: структурное переписывание
// пары соседних сиблингов в обычный Trust-код. Это НЕ lowering HIR: проход выполняется
// SemanticPassRunner ДО core.run, потому что созданная временная должна пройти обычную
// семантическую типизацию.
// AST при этом остаётся деревом разбора: отдельный проход живёт в semantic (он ставит
// диагностику и создаёт VarDecl/ErrorExpr), а не в hir-lowering.

#include "ast/ast_nodes.hpp"

#include <vector>

namespace trust {

class Context;

/// Контекст capture-прохода: владелец диагностик/атрибутов и счётчик временных.
struct CaptureLastResultContext {
    Context* ctx = nullptr;       ///< nullptr - без диагностики (unit/ручные узлы)
    int lastResultCounter = 0;    ///< уникальные имена `__trust_last_N` в пределах прохода
};

/// Переписывает список операторов (рекурсивно по вложенным телам).
void captureLastResult(std::vector<AstNodePtr>& body, CaptureLastResultContext& ctx);

} // namespace trust
