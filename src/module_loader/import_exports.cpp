// src/module_loader/import_exports.cpp
#include "module_loader/import_exports.hpp"

#include "module_loader/module_export.hpp"
#include "module_loader/module_loader.hpp"
#include "session/context.hpp"
#include "syntax/parser.h"

#include <unordered_set>
#include <vector>

namespace trust {

namespace {

void resolveNode(Context& ctx, ModuleLoader& loader, const AstNodePtr& node, std::unordered_set<ModuleIndex>& expanded) {
    if (!node) {
        return;
    }
    if (node->kind() == ParserToken::Kind::ModuleDecl) {
        auto& mn = static_cast<ModuleNode&>(*node);
        if (mn.isImport()) {
            mn.parts().clear();
            bool anyNew = false;
            for (const ModuleIndex idx : mn.moduleIndices()) {
                if (!loader.isLoaded(idx)) {
                    reportModuleNotLoaded(ctx, mn.range(), mn.importSpelling());
                    continue;
                }
                const std::shared_ptr<const SiteProjection> projection = loader.siteProjection(idx, mn.importMaskSet());
                anyNew = expanded.insert(idx).second || anyNew;
                // Тело и проекция НЕ копируются: часть держит разделяемые объекты.
                mn.parts().push_back(ModuleNode::ModulePart{idx, loader.bodyAstShared(idx), projection});
            }
            // Непопадание масок — по ОБЪЕДИНЁННОМУ набору частей; считается в семантике
            // (нужен только там, где выдаётся `-Wunmatched-mask`).
            // Вложенные импорты живут в телах частей (своего тела у сайта нет).
            if (anyNew) {
                for (const auto& part : mn.parts()) {
                    EXPECT(part.body && "module part must carry a shared body");
                    for (const auto& child : *part.body) {
                        resolveNode(ctx, loader, child, expanded);
                    }
                }
            }
            return;
        }
    }
    for (const auto& child : node->children()) {
        resolveNode(ctx, loader, child, expanded);
    }
}

} // namespace

void resolveImportExports(Context& ctx, ModuleLoader& loader, const std::vector<AstNodePtr>& astNodes) {
    std::unordered_set<ModuleIndex> expanded;
    for (const auto& node : astNodes) {
        resolveNode(ctx, loader, node, expanded);
    }
}

} // namespace trust
