// include/module_loader/import_exports.hpp
// Связывание сайтов импорта с загруженными модулями: индекс модуля и экспорт-интерфейс.
#pragma once

#include "ast/ast_nodes.hpp"

#include <vector>

namespace trust {

class Context;
class ModuleLoader;

/// Рекурсивно заполняет экспорт-интерфейс всех сайтов импорта (`ModuleNode::isImport()`):
/// берёт индекс модуля из узла (`ModuleNode::moduleIndex`), сохраняет «полный» экспорт в реестре и кладёт
/// отфильтрованный (по маскам `\module(mod, masks)`) список экспортов в узел.
void resolveImportExports(Context& ctx, ModuleLoader& loader, const std::vector<AstNodePtr>& astNodes);

} // namespace trust
