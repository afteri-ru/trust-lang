// Видимость типов в экспортируемых объявлениях (см. exported_signature_check.hpp).
#include "types/type_read.hpp"
#include "semantic/decl/exported_signature_check.hpp"

#include "semantic/core/pass.hpp"
#include "symbols/symbol_table.hpp"
#include "diag/diag.hpp"
#include "types/type_system.hpp"
#include "types/type_data.hpp"
#include "types/types.hpp"
#include "types/typekind.hpp"

#include <set>
#include <vector>

namespace trust::semantic {

// Модульный корневой скоуп = [global(0), moduleRoot(1)]: только здесь объявление экспортируемое.
// Глубже (блок/функция/тело типа) — не экспорт; stdlib DSL анализируется в том же корневом скоупе.
bool isExportedTopLevel(const AnalysisContext& actx) {
    const SymbolTable& symbols = actx.symbols();
    // Модульный уровень — ScopeKind::ModuleRoot (stdlib DSL/без ModuleNode: первый вложенный скоуп).
    return symbols.atModuleLevel() && !symbols.inFilePrivateScope() && actx.namespaces().currentFunc() == nullptr && !actx.namespaces().insideTypeBody();
}

void checkExportedTypeVisibility(AnalysisContext& actx, TypeId root, MapperRange range, std::string_view entity) {
    if (root == INVALID_TYPE_ID) {
        return;
    }
    const TypeReadModel& reg = actx.registry();
    std::vector<TypeId> work{getUnqualifiedType(root)};
    std::set<TypeId> visited;
    while (!work.empty()) {
        const TypeId id = work.back();
        work.pop_back();
        if (id == INVALID_TYPE_ID || !visited.insert(id).second) {
            continue;
        }
        // Типовой параметр шаблона - не file-private сущность (легален в сигнатуре).
        if (reg.store().isFilePrivate(id)) {
            actx.ctx().diag().report(Severity::Error, range, "exported {} uses file-private type '{}'", entity, reg.printer().typeNameOf(id));
            return;
        }
        // Алиас-цепочка отдельно не обходится: каждый exported-алиас проверяется в точке своего
        // объявления (его цель — base_id), поэтому приватная цель ловится там.
        const std::optional<TypeData>& data = reg.store().getTypeData(id);
        if (data.has_value()) {
            forEachTypeChild(*data, [&](TypeId child) { work.push_back(getUnqualifiedType(child)); });
        }
        // Базовые классы record-типа живут в дескрипторе (не в TypeData).
        for (const BaseClass& base : reg.store().basesOf(id)) {
            work.push_back(getUnqualifiedType(base.type));
        }
    }
}

} // namespace trust::semantic
