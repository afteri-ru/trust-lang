#pragma once

// Частичный предикат POD для полей объявленного Struct (R4/R13).
// Отвергает только типы, однозначно НЕ-POD в целевом C++ по языковой модели: инстансы классов
// (`Group::kClassDefs`), ссылки-обёртки/ссылки (нетривиальные члены), строки, `Any`, изменяемый
// массив (`std::vector`). Неизвестные/нативные типы НЕ отвергаются — их вердикт остаётся у
// `static_assert` целевого C++ (`podAssertText`), см. R4 (модель layout/ABI не строится).
// Единственная точка правила: `record_decl_analyzer` вызывает его, а не хранит собственный список.

#include "types/type_id.hpp"
#include "types/type_read.hpp"
#include "types/typekind.hpp"

namespace trust {
namespace semantic {

[[nodiscard]] inline bool isDefinitelyNonPodFieldType(TypeId id, const TypeReadModel& reg) noexcept {
    if (id == INVALID_TYPE_ID) {
        return false;
    }
    const TypeId canon = reg.store().desugaredTypeId(id);
    const TypeKind kind = getTypeKind(canon);
    // Ссылки-обёртки (владение/охрана) и ссылки — нетривиальные члены. Сырые указатели тривиальны.
    const RefType ref = typeRefOf(kind);
    if (ref != RefType::kValue && ref != RefType::kPtr && ref != RefType::kMptr) {
        return true;
    }
    const Group group = typeGroupOf(kind);
    // Пользовательский класс-инстанс, строки (std::string/wstring), Any (std::any) — нетривиальны.
    if (group == Group::kClassDefs || group == Group::kStrChar || group == Group::kStrWide || group == Group::kAny) {
        return true;
    }
    // Изменяемый массив (`:Array` / `:Elem[n]`) — std::vector, нетривиальный.
    return reg.store().isArrayType(canon);
}

} // namespace semantic
} // namespace trust
