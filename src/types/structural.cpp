// src/types/structural.cpp
// Структурное интернирование типов (TypeKey/TypeRegistry::m_structural): функциональные,
// кортежные, Range/Array, ссылочные (RefType) типы и подстановка типового параметра T→Elem.
// Модуль (декомпозиция registry.cpp).
#include "types/registry.hpp"
#include "types/group.hpp"
#include "types/type_names.hpp"
#include "types/type_diag.hpp"
#include "utils/error.hpp"
#include "diag/diag.hpp"
#include "diag/options.hpp"
#include "diag/base_diags.hpp"
#include <optional>
#include <string>
#include <vector>
namespace trust {

TypeId TypeRegistry::getOrCreateStructuralType(std::string_view name, TypeKind kind, std::vector<TypeId> children, std::optional<TypeData> data,
                                               std::string_view preprocInclude, std::vector<std::string> names) {
    // Структурная идентичность интернируется на 63 битах; бит «inferred» (kInferredFlag) и бит
    // «константность» (kConstFlag) не должны попадать в TypeKey/дескриптор (иначе дубликаты и
    // утечка признака в идентичность типа). Константность параметров функции учитывается на
    // уровне кодогенерации/прототипа, а не в структурном типе.
    for (const TypeId c : children) {
        EXPECT(!typeIsInferred(c) && "structural type child must not carry the inferred bit");
        EXPECT(!typeIsConst(c) && "structural type child must not carry the const bit");
    }
    EXPECT(names.empty() || names.size() == children.size() && "structural type names must match children count");
    TypeKey key{kind, children, std::move(names)};

    // Check if already exists via structural uniquing
    auto it = m_structural.find(key);
    if (it != m_structural.end()) {
        return it->second;
    }

    // Check if name is already taken. Структурные типы с пустым именем (например,
    // функциональные) идентифицируются по TypeKey (m_structural) и не участвуют в
    // name-коллизиях: имя "" не должно резервироваться в m_name_to_id, иначе вторая
    // отличная сигнатура функции дала бы ложную «duplicate type name ''».
    if (!name.empty()) {
        const std::string nkey(name);
        auto nameIt = m_name_to_id.find(nkey);
        const bool inBuiltin = m_builtin && m_builtin->name_to_id.find(nkey) != m_builtin->name_to_id.end();
        if (nameIt != m_name_to_id.end() || inBuiltin) {
            // If same TypeKey - return existing
            if (nameIt != m_name_to_id.end() && m_structural.find(key) != m_structural.end()) {
                return nameIt->second;
            }

            // If name collision with different type - error
            reportTypeDiag(m_diag, m_opts, diag::DiagId::ParseError, {}, "duplicate type name '{}'", name);
            return INVALID_TYPE_ID;
        }
    }

    // Create new type (registry_index = после всех встроенных, т.к. дескриптор - пользовательский)
    TypeId id = makeTypeId(kind, static_cast<uint32_t>(m_builtinCount + m_descriptors.size() + 1));
    m_descriptors.push_back({
        std::string(name), // name - владеющая копия
        {},                // attrs (empty, caller should set after creation)
        {},                // sourceRange
        {},                // cppName
        preprocInclude.empty() ? std::vector<std::string>{} : std::vector<std::string>{std::string(preprocInclude)}, // preprocIncludes
        INVALID_TYPE_ID, // baseType - structural types are not aliases
        std::move(data)  // data
    });
    if (!name.empty()) {
        m_name_to_id[std::string(name)] = id;
    }
    m_structural[key] = id;
    return id;
}

// -- Get or create FunctionType -------------------------
TypeId TypeRegistry::getOrCreateFunctionType(TypeId returnType, std::vector<TypeId> paramTypes, TypeId variadicType, bool hasTrust) {
    // Build children: [returnType, paramTypes..., variadicType (if variadic)]
    std::vector<TypeId> children;
    children.reserve(1 + paramTypes.size() + (variadicType != INVALID_TYPE_ID ? 1 : 0));
    children.push_back(returnType);
    children.insert(children.end(), paramTypes.begin(), paramTypes.end());
    if (variadicType != INVALID_TYPE_ID) {
        children.push_back(variadicType);
    }

    // Use a distinct TypeKind for function types: Group::kCallable, data=1
    TypeKind funcKind = makeTypeKind(Group::kCallable, 1);
    // Функция с trust-условиями (пред/пост) НЕ эквивалентна идентичной сигнатуре без условий:
    // бит в TypeKind → отдельный TypeKey → раздельное интернирование.
    if (hasTrust) {
        funcKind = setTrustFlag(funcKind);
    }

    FunctionTypeData funcData{returnType, paramTypes, variadicType};

    // Use empty name - structural function types don't need a name
    return getOrCreateStructuralType("", funcKind, std::move(children), std::move(funcData));
}

// -- Get or create TupleType (структурный кортеж) ---------
TypeId TypeRegistry::getOrCreateTupleType(std::vector<std::pair<std::string, TypeId>> elements) {
    std::vector<TypeId> children;
    std::vector<std::string> names;
    children.reserve(elements.size());
    names.reserve(elements.size());
    std::vector<TupleElementData> elems;
    elems.reserve(elements.size());
    for (auto& [name, type] : elements) {
        EXPECT(!typeIsInferred(type) && "tuple element type must not carry the inferred bit");
        EXPECT(!typeIsConst(type) && "tuple element type must not carry the const bit");
        children.push_back(type);
        names.push_back(name);
        elems.push_back(TupleElementData{name, type});
    }
    // data=1 в Group::kStructured - конкретный структурный кортеж (свободно: функция = kCallable/1).
    TypeKind tupleKind = makeTypeKind(Group::kStructured, 1);
    TupleTypeData data{std::move(elems)};
    return getOrCreateStructuralType("", tupleKind, std::move(children), std::move(data), "", std::move(names));
}

// -- Get or create RangeType (параметризованный Range<Elem>) --
TypeId TypeRegistry::getOrCreateRangeType(TypeId elementType) {
    EXPECT(!typeIsInferred(elementType) && "range element type must not carry the inferred bit");
    EXPECT(!typeIsConst(elementType) && "range element type must not carry the const bit");
    // Data=1 в Group::kRanges - конкретный структурный диапазон (абстрактный `:Range` - Data=0).
    TypeKind rangeKind = makeTypeKind(Group::kRanges, 1);
    TemplateTypeData data{getType(type_category::Range), {elementType}};
    TypeId id = getOrCreateStructuralType("", rangeKind, {elementType}, std::move(data), "@trust/range.hpp");
    // Рантайм-заголовки: range.hpp самодостаточен, но для toDict/элементов нужны dict.hpp и
    // rational.hpp - пайплайн извлекает только по прямому запросу, поэтому кладём все три.
    if (TypeDescriptor* desc = userDescriptorOf(id)) {
        desc->preprocIncludes = {"@trust/range.hpp", "@trust/dict.hpp", "@trust/rational.hpp"};
    }
    return id;
}

bool TypeRegistry::isRangeType(TypeId id) const noexcept {
    const auto* td = getTypeDataAs<TemplateTypeData>(getCanonicalTypeId(id));
    return td && getCanonicalTypeId(td->templateTypeId) == getCanonicalTypeId(getType(type_category::Range));
}

TypeId TypeRegistry::rangeElementType(TypeId id) const noexcept {
    const auto* td = getTypeDataAs<TemplateTypeData>(getCanonicalTypeId(id));
    if (td && !td->args.empty()) {
        return td->args[0];
    }
    return INVALID_TYPE_ID;
}

// Подстановка типового параметра T (Group::kTemplateParam) → elem в сигнатуре функционального
// типа. Единый механизм для параметризованных контейнеров (Range, Array): методы объявлены на
// абстрактном типе с T, а для конкретного Elem интернируется конкретная сигнатура.
static TypeId substituteElementParam(TypeRegistry& reg, TypeId elem, TypeId templateFuncType) {
    const auto* fd = reg.getTypeDataAs<FunctionTypeData>(templateFuncType);
    if (!fd) {
        return templateFuncType;
    }
    const TypeId tmplParam = reg.getType(type_category::TemplateParam);
    const bool retIsT = (fd->returnType == tmplParam);
    std::vector<TypeId> params = fd->paramTypes;
    bool hasT = retIsT;
    for (auto& p : params) {
        if (p == tmplParam) {
            p = elem;
            hasT = true;
        }
    }
    if (!hasT) {
        return templateFuncType; // нет T → сигнатура не зависит от элемента (напр. count())
    }
    const TypeId ret = retIsT ? elem : fd->returnType;
    return reg.getOrCreateFunctionType(ret, std::move(params), fd->variadicType);
}

TypeId TypeRegistry::instantiateRangeMethod(TypeId objType, TypeId templateFuncType) {
    // Элементный тип: конкретный Range<Elem> → Elem; абстрактный :Range → Any (нет Elem).
    TypeId elem = rangeElementType(objType);
    if (elem == INVALID_TYPE_ID) {
        elem = getType(type_generic::Any);
    }
    return substituteElementParam(*this, elem, templateFuncType);
}

// -- Get or create ArrayType (параметризованный Array<Elem>) --
TypeId TypeRegistry::getOrCreateArrayType(TypeId elementType, std::vector<uint64_t> dimensions) {
    EXPECT(!typeIsInferred(elementType) && "array element type must not carry the inferred bit");
    EXPECT(!typeIsConst(elementType) && "array element type must not carry the const bit");
    // TypeKey::names - единственный слот для «не-типовых» атрибутов (dims): children=[elem].
    // Формат: "<d1>,<d2>,...". Пустой → динамический.
    std::vector<std::string> names;
    if (!dimensions.empty()) {
        std::string enc;
        for (size_t i = 0; i < dimensions.size(); ++i) {
            if (i) {
                enc += ',';
            }
            enc += std::to_string(dimensions[i]);
        }
        names.push_back(std::move(enc));
    }
    // Data=1 в Group::kContainers - конкретный структурный массив (абстрактный `:Array` - Data=0).
    TypeKind arrayKind = makeTypeKind(Group::kContainers, 1);
    ArrayTypeData data{elementType, dimensions, false};
    TypeId id = getOrCreateStructuralType("", arrayKind, {elementType}, std::move(data), "", std::move(names));
    // Инклуды контейнера (std::vector/std::array) не зависят от элемента; добавляются при
    // кодогенерации по константности TypeId (resolveCppTypeId), поэтому здесь заголовки не задаём.
    return id;
}

bool TypeRegistry::isArrayType(TypeId id) const noexcept {
    const auto* td = getTypeDataAs<ArrayTypeData>(getCanonicalTypeId(id));
    return td != nullptr;
}

TypeId TypeRegistry::arrayElementType(TypeId id) const noexcept {
    const auto* td = getTypeDataAs<ArrayTypeData>(getCanonicalTypeId(id));
    return td ? td->elementType : INVALID_TYPE_ID;
}

const std::vector<uint64_t>& TypeRegistry::arrayDimensions(TypeId id) const noexcept {
    static const std::vector<uint64_t> kEmpty;
    const auto* td = getTypeDataAs<ArrayTypeData>(getCanonicalTypeId(id));
    return td ? td->dimensions : kEmpty;
}

TypeId TypeRegistry::instantiateArrayMethod(TypeId objType, TypeId templateFuncType) {
    // Элементный тип: конкретный Array<Elem> → Elem; абстрактный :Array → Any (нет Elem).
    TypeId elem = arrayElementType(objType);
    if (elem == INVALID_TYPE_ID) {
        elem = getType(type_generic::Any);
    }
    // Единая подстановка типового параметра T→Elem (как instantiateRangeMethod).
    return substituteElementParam(*this, elem, templateFuncType);
}

TypeId TypeRegistry::getOrCreateRefType(RefType kind, TypeId pointee) {
    // Составной ссылочный узел: отдельная группа kReftype (Data=1, RefType=вид), один
    // ребёнок pointee. Интернируется структурно по TypeKey{kind, children} - ссылка на
    // уже ссылочный тип (shared<ptr<Int32>>) получает свой узел, а не перезаписывает бит.
    TypeKind refType = makeTypeKind(Group::kReftype, 1, TypeClass::kTrivial, kind);
    RefTypeData data{pointee};
    return getOrCreateStructuralType("", refType, {pointee}, std::move(data));
}

TypeId TypeRegistry::applyRefType(TypeId base, RefType kind) {
    // Первая ссылка на тип без признака - fast-path бит. withRefType работает с TypeKind
    // (uint32) и обнуляет registry_index, поэтому пересобираем TypeId, сохраняя нижние
    // 32 бита (registry_index + kInferredFlag/kConstFlag). Вложенность - составной узел.
    if (getRefType(getKindFromId(base)) == RefType::kValue) {
        return (static_cast<uint64_t>(withRefType(getKindFromId(base), kind)) << 32) | (static_cast<uint32_t>(base) & 0xFFFFFFFFu);
    }
    return getOrCreateRefType(kind, base);
}
} // namespace trust
