#include "types/registry/structural_interner.hpp"

#include "types/func/fn_identity.hpp"
#include "types/registry/type_name_index.hpp"
#include "types/registry/type_store.hpp"
#include "types/type_read.hpp"
#include "types/names/storage_key.hpp"
#include "types/names/type_names.hpp"
#include "diag/base_diags.hpp"
#include "diag/diag.hpp"
#include "diag/options.hpp"
#include "diag/report.hpp"
#include "utils/error.hpp"
#include <algorithm>
#include <map>
#include <variant>

namespace trust {

namespace {

template <typename>
inline constexpr bool kUnhandledTypeData = false;

/// Identity discriminator DERIVED from TypeData: the fields that define identity but are not
/// TypeIds and therefore cannot be encoded in the TypeKey children list. Keeping the derivation
/// here makes it impossible for a caller to forget a data field that participates in identity.
/// Исчерпывающий `std::visit`: новый вариант TypeData не компилируется, пока его не-типовые
/// поля идентичности не описаны здесь (compile-time exhaustiveness).
struct StructuralTagVisitor {
    template <typename D>
    StructuralTag operator()(const D& d) const {
        if constexpr (std::is_same_v<D, FunctionTypeData>) {
            // Every path that interns a function type derives the same slot layout from the data.
            // SINGLE source: FunctionIdentity::slotLayout (shared with internKey).
            return FunctionIdentity::slotLayout(d.paramTypes.size(), d.variadicType != INVALID_TYPE_ID, d.paramPackPosition);
        } else if constexpr (std::is_same_v<D, NativeTemplateTypeData>) {
            // Native template instantiation: the LANGUAGE key (abstract template) is part of the
            // type identity — NOT its C++ name (R37). `std::pair<T>` != `std::tuple<T>` by their
            // abstract template. Abstract templates are registered by name (not through this
            // path), so they never reach this branch.
            return NativeTemplateTag{d.templateOf};
        } else if constexpr (std::is_same_v<D, ArrayTypeData>) {
            // Размерности и признак открытого массива (срез) - не TypeId, поэтому живут в
            // типизированном tag: `Array<T>`, `Array<T,[4]>` и открытый `Array<T,[]>` различны.
            return ArrayShapeTag{d.dimensions, d.isVariadic};
        } else if constexpr (std::is_same_v<D, TensorTypeData>) {
            // Статическая форма тензора - тоже не TypeId: `Tensor<T,[2,3]>` != `Tensor<T,[3,2]>`.
            return TensorShapeTag{d.shape};
        } else if constexpr (std::is_same_v<D, TemplateTypeData>) {
            // Параметризованный тип (Range<Elem>): сам шаблон-маркер входит в идентичность,
            // а не только type-аргументы. children содержат только args.
            return TemplateMarkerTag{d.templateTypeId};
        } else if constexpr (std::is_same_v<D, TemplateParamTypeData>) {
            // Идентичность типового параметра: владелец+позиция (depth,index) — не-типовые поля, но
            // задают уникальность (TemplateParamTag). Имя в identity НЕ входит (только диагностика).
            return TemplateParamTag{d.owner, d.depth, d.index};
        } else if constexpr (std::is_same_v<D, SimpleTypeData> || std::is_same_v<D, MemberPointerTypeData> || std::is_same_v<D, RefTypeData> ||
                             std::is_same_v<D, PackExpansionTypeData> || std::is_same_v<D, TupleTypeData> || std::is_same_v<D, EnumTypeData> ||
                             std::is_same_v<D, VariantTypeData> || std::is_same_v<D, NativeClassTypeData> || std::is_same_v<D, RecordTypeData>) {
            // Идентичность полностью выражена kind/children.
            return std::monostate{};
        } else {
            static_assert(kUnhandledTypeData<D>, "structuralKeyTag: unhandled TypeData variant - describe its non-type identity fields");
            return std::monostate{};
        }
    }
};

StructuralTag structuralKeyTag(const std::optional<TypeData>& data) {
    if (!data) {
        return std::monostate{};
    }
    return std::visit(StructuralTagVisitor{}, *data);
}

} // namespace

StructuralInterner::StructuralInterner(const TypeReadModel& read, TypeStore& store, TypeNameIndex& names, DiagnosticEngine& diag, const Options& opts) noexcept
: m_read(read)
, m_store(store)
, m_names(names)
, m_diag(diag)
, m_opts(opts) {
}

void StructuralInterner::reset(const std::unordered_map<TypeKey, TypeId, TypeKeyHash>& seed) {
    m_structural = seed;
    m_failedInstantiationCycles.clear();
}

std::unordered_map<TypeKey, TypeId, TypeKeyHash> StructuralInterner::takeStructuralTable() {
    std::unordered_map<TypeKey, TypeId, TypeKeyHash> out = std::move(m_structural);
    m_structural.clear();
    return out;
}

TypeId StructuralInterner::getOrCreateStructuralType(std::string_view name, TypeKind kind, std::vector<TypeKeyChild> children, std::optional<TypeData> data) {
    TypeKey key{kind, std::move(children), structuralKeyTag(data)};
    return internStructural(std::move(key), std::move(data), name);
}
TypeId StructuralInterner::internStructural(TypeKey key, std::optional<TypeData> data, std::string_view name) {
    // Структурная идентичность интернируется на 63 битах; поведенческие флаги (inferred/const/
    // uninit) не должны попадать в TypeKey/дескриптор (иначе дубликаты и утечка признака в
    // идентичность типа). Для ФУНКЦИЙ children снимаются в `FunctionIdentity::internKey`, а
    // `getOrCreateFunctionType` нормализует и поля `FunctionTypeData` (identity data, без state);
    // проверка ниже страхует остальные пути (Array/Tuple/Ref/...).
    for (const TypeKeyChild& c : key.children) {
        EXPECT(isUnqualifiedType(c.type) && "structural type child must not carry type-state flags");
    }

    // Check if already exists via structural uniquing
    auto it = m_structural.find(key);
    if (it != m_structural.end()) {
        return it->second;
    }

    // Ключ регистрации имени; пустое имя (безымянные структурные типы) ключа не имеет.
    const std::optional<StorageKey> nameKey = name.empty() ? std::nullopt : std::optional<StorageKey>(internStorageKey(exportedKeyOf(name)));
    // Check if name is already taken. Структурные типы с пустым именем (например,
    // функциональные) идентифицируются по TypeKey (m_structural) и не участвуют в
    // name-коллизиях: имя "" не должно резервироваться в TypeNameIndex, иначе вторая
    // отличная сигнатура функции дала бы ложную «duplicate type name ''».
    if (!name.empty()) {
        const bool nameTaken = m_names.contains(*nameKey);
        const bool inBuiltin = m_names.isBuiltinTypeName(name);
        if (nameTaken || inBuiltin) {
            // The same name may be re-requested only for the SAME interned identity. Returning the
            // name mapping when the key maps to another TypeId would silently produce two ids for
            // one name, so verify both maps agree.
            if (nameTaken) {
                const TypeId registered = *m_names.find(*nameKey);
                const auto keyIt = m_structural.find(key);
                if (keyIt != m_structural.end() && keyIt->second == registered) {
                    return registered;
                }
            }

            // If name collision with different type - error
            diag::reportWithOptions(m_diag, m_opts, diag::DiagId::ParseError, {}, "duplicate type name '{}'", name);
            return INVALID_TYPE_ID;
        }
    }

    // Create new type (registry_index = после всех встроенных, т.к. дескриптор - пользовательский)
    TypeDescriptor desc{
        std::string(name), // name - владеющая копия
        {},                // attrs (empty, caller should set after creation)
        {},                // sourceRange
        INVALID_TYPE_ID,   // baseType - structural types are not aliases
        std::move(data)    // data
    };
    const TypeId id = nameKey.has_value() ? m_store.push(key.kind, std::move(desc), *nameKey) : m_store.push(key.kind, std::move(desc));
    if (nameKey.has_value()) {
        m_names.bind(*nameKey, id);
    }
    m_structural.emplace(std::move(key), id);
    return id;
}
TypeId StructuralInterner::getOrCreateFunctionType(TypeId returnType, std::vector<TypeId> paramTypes, TypeId variadicType, bool hasTrust, TypeId paramPackType,
                                                   uint8_t paramPackPosition) {
    // Pack slot invariant: position is meaningful ONLY together with a pack type. Without
    // this check a garbage position would leak into the identity key (and only there),
    // breaking the "name == identity" projection (invalid input is an error, not a fallback).
    EXPECT((paramPackType != INVALID_TYPE_ID || paramPackPosition == 0) && "getOrCreateFunctionType: paramPackPosition must be 0 without a pack slot");
    EXPECT((paramPackType == INVALID_TYPE_ID || paramPackPosition != 0) && "getOrCreateFunctionType: a pack slot requires a non-zero position");
    // FunctionTypeData is identity data: drop type-state qualifiers (same invariant as TypeKey).
    returnType = getUnqualifiedType(returnType);
    for (TypeId& param : paramTypes) {
        param = getUnqualifiedType(param);
    }
    variadicType = getUnqualifiedType(variadicType);
    paramPackType = getUnqualifiedType(paramPackType);
    const FunctionIdentity identity = m_read.identity();
    TypeKey key = identity.internKey(returnType, paramTypes, variadicType, paramPackType, paramPackPosition, hasTrust);
    FunctionTypeData funcData{returnType, paramTypes, variadicType, paramPackType, paramPackPosition};
    // Use empty name - structural function types don't need a name.
    return internStructural(std::move(key), std::move(funcData), "");
}
TypeId StructuralInterner::getOrCreateTupleType(std::vector<std::pair<std::string, QualType>> elements) {
    std::vector<TypeKeyChild> children;
    children.reserve(elements.size());
    std::vector<TupleElementData> elems;
    elems.reserve(elements.size());
    for (auto& [name, type] : elements) {
        // Идентичность элемента - TypeId; квалификаторы (const/inferred) не входят в TypeKey.
        // Tuple-элементы - occurrence-позиции (не объявленные поля), поэтому в данных они
        // нормализуются к identity; const/inferred носят типы выражений, а не кортеж.
        children.push_back(TypeKeyChild{type.getTypeId(), name});
        elems.push_back(TupleElementData{name, QualType::fromTypeId(type.getTypeId())});
    }
    // data=1 в Group::kStructured - конкретный структурный кортеж (свободно: функция = kCallable/1).
    TypeKind tupleKind = makeTypeKind(Group::kStructured, 1);
    TupleTypeData data{std::move(elems)};
    return getOrCreateStructuralType("", tupleKind, std::move(children), std::move(data));
}

} // namespace trust
