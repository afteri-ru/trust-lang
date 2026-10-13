// include/module_loader/interface/interface_install.hpp
// Установка интерфейса файла в AST: найти блок интерфейса, разметить реэкспорт, собрать
// экспорт-набор и его факты, проверить «объявлено, но не определено».
// ⚠ Порядок шагов внутри наблюдаем: шаги мутируют `AstNodeBase::visibility`, и от порядка
// зависит эмитируемый C++ (реализация за forward'ом → приватные → публичные).
#pragma once

#include "ast/ast_nodes.hpp"
#include "location/module_index.hpp"
#include "syntax/module_header.hpp"

#include <vector>

namespace trust {

class Context;
class ModuleLoader;

/// Штамп «модуль объявления» на объявления уровня модуля (`AstNodeBase::module` и унаследованная
/// видимость). Внутрь тел функций/типов не спускается; сайты импорта пропускаются.
void stampModuleLevelNode(AstNodeBase& node, ModuleIndex module, AstNodeBase::Visibility inherited = AstNodeBase::Visibility::Public);

/// Интерфейс файла (`\path { ... };`): единственный писатель видимости объявлений модульного
/// уровня при загрузке. `body` — тело модуля; факты фиксируются через `loader`.
void installFileInterface(Context& ctx, ModuleLoader& loader, ModuleIndex idx, const ModuleHeaderScan& header, std::vector<AstNodePtr>& body);

} // namespace trust
