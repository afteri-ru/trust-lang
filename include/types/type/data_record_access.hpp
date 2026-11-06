#pragma once
// Внутренний доступ зоны `types` к хранилищу формы record.
//
// Снаружи компонента форма читается ТОЛЬКО аксессорами владельца — `TypeStore::fieldsOf`/
// `basesOf`/`templateParamsOf`/`instanceOf` (E1/R1). Этот заголовок лежит в internal-подкаталоге
// `types/type`, поэтому внешним потребителям недоступен (internal_boundary_guard).

#include "types/type/data_payload.hpp"

namespace trust {

/// Единственный путь чтения полей хранилища ВНУТРИ зоны `types` (кодоген/семантика — через TypeStore).
struct RecordTypeAccess {
    [[nodiscard]] static const std::vector<TupleElementData>& fieldsOf(const RecordTypeData& data) noexcept { return data.fields; }
    [[nodiscard]] static const std::vector<TypeId>& templateParamsOf(const RecordTypeData& data) noexcept { return data.templateParams; }
    [[nodiscard]] static TypeId templateOf(const RecordTypeData& data) noexcept { return data.templateOf; }
    [[nodiscard]] static const std::vector<TypeId>& templateArgsOf(const RecordTypeData& data) noexcept { return data.templateArgs; }
};

} // namespace trust