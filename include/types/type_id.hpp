#pragma once

// include/types/type_id.hpp
// Компиляторный слой над ОБЩИМ ABI-заголовком рантайма `runtime/trust/type_state.hpp`:
// TypeId/QualType, layout-константы и разбор битов определены РОВНО ОДИН РАЗ (там же, где их
// использует рантайм), чтобы компилятор и рантайм не могли разойтись в представлении.
//
// Здесь остаются только компилятор-специфичные операции:
//   * makeTypeId — валидирующее конструирование (EXPECT на 24-битный registry_index);
//   * isTrustedType — семантический флаг TypeKind (компиляторное понятие).
//
// Путь `runtime/trust/...` разрешается через PROJECT_INCLUDE_DIR (include/): include-путь
// компилятора НЕ расширяется, прочие переопределяемые runtime-заголовки ему не видны.

#include <cstdint>

#include "runtime/trust/type_state.hpp"
#include "types/typekind.hpp"
#include "utils/error.hpp"

namespace trust {

// -- Construction -----------------------------------------
/// registry_index обязан влезать в 24 бита (kMaxTypeIndex); новые индексы проверяет
/// TypeStore::allocateTypeIndex (EXPECT). Явная диагностика вместо молчаливой маски: значение
/// вне диапазона заняло бы qualifier-байт и схлопнулось бы с другим identity-ключом.
inline TypeId makeTypeId(TypeKind kind, uint32_t registry_index = 0) {
    EXPECT(registry_index <= kMaxTypeIndex && "makeTypeId: registry_index exceeds 24 bits");
    return TypeId::fromParts(kind, registry_index);
}

// -- Флаг «тип несёт trust-условия» (kTrustFlag) -----------
// Бит в TypeKind (верхняя половина TypeId, Reserved). Это СЕМАНТИЧЕСКИЙ дифференциатор
// идентичности: тип/функция с пред-/пост-условиями/утверждениями не эквивалентен идентичному
// без условий. В отличие от kInferred/kConst (нижняя половина, квалификаторы вхождения),
// бит НЕ снимается getRegistryIndex/getDesugaredTypeId (маскируют нижнюю половину) и входит в
// ключи структурного интернирования (TypeKey::kind). Используется для защиты от автоматического
// вывода типа: переменная, чей выведенный тип несёт trust-условия, обязана иметь явную
// аннотацию типа (см. types/MEMORY.md, семантика analyzeVarDecl/typeExpr).
constexpr bool isTrustedType(TypeId id) noexcept {
    return hasTrustFlag(getTypeKind(id));
}

} // namespace trust
