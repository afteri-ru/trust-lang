// Зона встроенных определений: языковой предикат абстрактного структурного контейнера.
#include "types/registry/builtin_registry.hpp"

#include "types/names/type_names.hpp"
#include "types/registry/type_store.hpp"

namespace trust {

TypeId BuiltinRegistry::abstractBuiltinOf(TypeId id) const noexcept {
    const TypeId canonical = m_store.desugaredTypeId(id);
    if (m_store.isArrayType(canonical)) {
        return m_store.builtinIdByName(type::Array);
    }
    if (m_store.isRangeType(canonical)) {
        return m_store.builtinIdByName(type_category::Range);
    }
    if (m_store.isTensorType(canonical)) {
        return m_store.builtinIdByName(m_store.isSparseTensorType(canonical) ? type::SparseTensor : type::Tensor);
    }
    return INVALID_TYPE_ID;
}

} // namespace trust