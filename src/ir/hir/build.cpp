// Построение HIR: клон неизменяемой AST + lowering на клоне (см. include/ir/hir_module.hpp).
#include "ir/hir_module.hpp"

#include "ast/clone.hpp"
#include "ir/lowering.hpp"
#include "session/context.hpp"

#include <utility>
#include <vector>

namespace trust {

HirModule buildHir(const std::vector<AstNodePtr>& astNodes, Context& ctx, TypeSystem& registry) {
    // Lowering мутирует форму, поэтому работает ТОЛЬКО на клоне: AST остаётся нетронутой
    // (её читают solver/диагностики/LSP после построения HIR).
    std::vector<AstNodePtr> nodes = cloneTrees(astNodes);
    LowerCtx lowerCtx;
    lowerCtx.ctx = &ctx;
    lowerCtx.types = &registry;
    lowerBody(nodes, lowerCtx);
    return HirModule(std::move(nodes));
}

} // namespace trust
