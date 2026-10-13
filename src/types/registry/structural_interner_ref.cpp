// src/types/registry/structural_interner_ref.cpp
// Зона интернирования: ССЫЛОЧНЫЕ виды — getOrCreateRefType и applyRefType (TypeId/QualType).
#include "types/registry/structural_interner.hpp"

#include "types/registry/type_store.hpp"
#include "types/subst/template_subst.hpp"
#include "types/type_read.hpp"
#include "utils/error.hpp"

#include <vector>

namespace trust {

TypeId StructuralInterner::getOrCreateRefType(RefType kind, TypeId pointee, TypeId deleter, TypeId accessPolicy, TypeId impl) {
    // Составной ссылочный узел: отдельная группа kReftype (Data=1, RefType=вид), дети - pointee
    // (+ deleter для unique: D входит в тип; + accessPolicy: политика входит в тип;
    // + impl: класс реализации механизма доступа входит в тип).
    // Интернируется структурно по TypeKey{kind, children} - ссылка на уже ссылочный тип
    // (shared<ptr<Int32>>) получает свой узел, а не перезаписывает бит; unique<T,D1> != unique<T,D2>.
    EXPECT(isUnqualifiedType(pointee) && "getOrCreateRefType: pointee id must not carry type-state flags");
    EXPECT(isUnqualifiedType(deleter) && "getOrCreateRefType: deleter id must not carry type-state flags");
    EXPECT(isUnqualifiedType(accessPolicy) && "getOrCreateRefType: access policy id must not carry type-state flags");
    EXPECT(isUnqualifiedType(impl) && "getOrCreateRefType: impl id must not carry type-state flags");
    TypeKind refType = makeTypeKind(Group::kReftype, 1, TypeClass::kTrivial, kind);
    RefTypeData data{pointee, deleter, accessPolicy, impl};
    std::vector<TypeId> children{pointee};
    if (deleter != INVALID_TYPE_ID) {
        children.push_back(deleter);
    }
    if (accessPolicy != INVALID_TYPE_ID) {
        children.push_back(accessPolicy);
    }
    if (impl != INVALID_TYPE_ID) {
        children.push_back(impl);
    }
    return getOrCreateStructuralType("", refType, std::move(children), std::move(data));
}
TypeId StructuralInterner::applyRefType(TypeId base, RefType kind) {
    // TypeId-перегрузка работает ТОЛЬКО с identity: state-квалификаторы переносит QualType-версия.
    EXPECT(isUnqualifiedType(base) && "applyRefType: base type id must not carry type-state flags");
    EXPECT(base.isValid() && "applyRefType: base type id is invalid");
    // Fast-path бит допустим ТОЛЬКО когда base - не алиас: иначе канонизация (getDesugaredTypeId)
    // разворачивает baseType и теряет вид ссылки, а C++-имя pointee обязано остаться «как написано»
    // (`shared<MyInt>` → trust::Shared<c_MyInt>, а не канонический int32_t). Алиас (baseType != INVALID)
    // и вложенность - составной узел RefTypeData с pointee = base как написан.
    const bool alreadyRef = getRefType(getTypeKind(base)) != RefType::kValue;
    const bool isAlias = m_read.store().getBaseType(base) != INVALID_TYPE_ID;
    if (!alreadyRef && !isAlias) {
        // withRefType работает с TypeKind (uint32), поэтому пересобираем identity-TypeId через
        // единый QualType::withKind (state здесь нулевой: вход structural).
        return QualType::fromTypeId(base).withKind(withRefType(getTypeKind(base), kind)).getTypeId();
    }
    // Структурный узел: child обязан быть без поведенческих флагов (инвариант интернирования).
    return getOrCreateRefType(kind, base);
}
QualType StructuralInterner::applyRefType(QualType base, RefType kind) {
    EXPECT(base.isValid() && "applyRefType: base type is invalid");
    // Квалификаторы исходного типа (const/inferred) переносятся на результат.
    return base.withIdentity(applyRefType(base.getTypeId(), kind));
}
TypeId StructuralInterner::applyRefType(TypeId base, RefType kind, TypeId deleter, TypeId accessPolicy, TypeId impl) {
    // С deleter/policy/impl тип ВСЕГДА структурный узел: TypeKind (uint32) не хранит TypeId D/Policy/Impl,
    // а для unique D и для sync Policy/Impl входят в тип. Без них - обычный путь (fast-path/вложенность).
    if (deleter == INVALID_TYPE_ID && accessPolicy == INVALID_TYPE_ID && impl == INVALID_TYPE_ID) {
        return applyRefType(base, kind);
    }
    EXPECT(isUnqualifiedType(base) && "applyRefType: base type id must not carry type-state flags");
    EXPECT(base.isValid() && "applyRefType: base type id is invalid");
    return getOrCreateRefType(kind, base, deleter, accessPolicy, impl);
}
QualType StructuralInterner::applyRefType(QualType base, RefType kind, TypeId deleter, TypeId accessPolicy, TypeId impl) {
    EXPECT(base.isValid() && "applyRefType: base type is invalid");
    return base.withIdentity(applyRefType(base.getTypeId(), kind, deleter, accessPolicy, impl));
}

} // namespace trust
