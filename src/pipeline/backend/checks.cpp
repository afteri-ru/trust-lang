// src/pipeline/backend/checks.cpp
// Пост-проверки AST в конвейере.
#include "pipeline/backend/checks.hpp"

#include "attrs/attr.hpp"
#include "semantic/diag.hpp"
#include "session/context.hpp"

#include <utility>
#include <vector>

namespace trust {

void reportUnhandledAttributes(Context& ctx, const std::vector<AstNodePtr>& roots) {
    if (roots.empty()) {
        return;
    }
    // Диагностика semantic-уровня: если не зарегистрирована (parse-only путь без семантики) -
    // пропускаем. Severity::Ignore = "ignore" (пользователь выключил -Wunhandled-attr=ignore).
    if (!ctx.opts().is_registered(semantic::DiagId::UnhandledAttr)) {
        return;
    }
    const Severity sev = ctx.opts().get(semantic::DiagId::UnhandledAttr);
    if (sev == Severity::Ignore) {
        return;
    }
    std::vector<AstNodePtr> stack(roots.begin(), roots.end());
    while (!stack.empty()) {
        AstNodePtr node = std::move(stack.back());
        stack.pop_back();
        if (!node) {
            continue;
        }
        if (const AstNodeAttr* attrNode = node->as_attr()) {
            for (const AttrId id : attrNode->attrs()) {
                if (detail::is_handled(id)) {
                    continue;
                }
                ctx.diag().report(sev, node->range(), semantic::DiagId::UnhandledAttr,
                                  "attribute '{}' is not handled by the analyzer or the C++ code generator", ctx.attrs().get_name(id));
            }
        }
        for (const AstNodePtr& child : node->children()) {
            if (child) {
                stack.push_back(child);
            }
        }
    }
}

void reportUnhandledAttributes(Context& ctx, const HirModule& hir) {
    reportUnhandledAttributes(ctx, hir.nodes());
}

} // namespace trust
