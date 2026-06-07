#include "types/registry.hpp"
#include "types/group.hpp"
#include "types/type_names.hpp"
#include "utils/error.hpp"
#include "diag/context.hpp"

#include <unordered_map>

namespace trust {

// ── Concrete groups (Data != 0) ────────────────────────────
static constexpr bool isAbstractGroup(Group group) noexcept {
    switch (group) {
    case Group::kAny:
    case Group::kTensors:
    case Group::kContainers:
    case Group::kStructured:
    case Group::kCallable:
    case Group::kClasses:
    case Group::kRanges:
    case Group::kIterators:
    case Group::kDateTime:
    case Group::kAsync:
    case Group::kSync:
    case Group::kExceptions:
    case Group::kNative:
    case Group::kArithmetics:
    case Group::kTemplateParam:
        return true;
    default:
        return false;
    }
}

// ── Constructor ──────────────────────────────────────────
TypeRegistry::TypeRegistry(Context* ctx)
: m_ctx(ctx) {
    registerBuiltinTypes();
}

// ── Register all builtin types ──────────────────────────
void TypeRegistry::registerBuiltinTypes() {
    // Concrete builtin types (Data ≠ 0 → TypeClass::kTrivial)
    registerBuiltinType(type::Void, Group::kVoid, 1);
    registerBuiltinType(type::None, Group::kVoid, 2);
    registerBuiltinType(type::Bool, Group::kLogical, 1, "bool");
    auto int8Id = registerBuiltinType(type::Int8, Group::kIntegers, 8, "int8_t", "#include <cstdint>");
    registerBuiltinType(type::Int16, Group::kIntegers, 16, "int16_t", "#include <cstdint>");
    registerBuiltinType(type::Int32, Group::kIntegers, 32, "int32_t", "#include <cstdint>");
    registerBuiltinType(type::Int64, Group::kIntegers, 64, "int64_t", "#include <cstdint>");
    auto uint8Id = registerBuiltinType(type::UInt8, Group::kUnsigned, 8, "uint8_t", "#include <cstdint>");
    auto uint16Id = registerBuiltinType(type::UInt16, Group::kUnsigned, 16, "uint16_t", "#include <cstdint>");
    auto uint32Id = registerBuiltinType(type::UInt32, Group::kUnsigned, 32, "uint32_t", "#include <cstdint>");
    auto uint64Id = registerBuiltinType(type::UInt64, Group::kUnsigned, 64, "uint64_t", "#include <cstdint>");
    registerBuiltinType(type::Float16, Group::kNumbers, 16, "float16_t", "#include <stdfloat>");
    auto float32Id = registerBuiltinType(type::Float32, Group::kNumbers, 32, "float32_t");
    auto float64Id = registerBuiltinType(type::Float64, Group::kNumbers, 64, "float64_t");
    registerBuiltinType(type::BFloat16, Group::kBFloat, 16);
    registerBuiltinType(type::Complex32, Group::kComplex, 32, "std::complex<float>", "#include <complex>");
    registerBuiltinType(type::Complex64, Group::kComplex, 64, "std::complex<double>", "#include <complex>");
    auto strCharId = registerBuiltinType(type::StrChar, Group::kStrChar, 1, "std::string", "#include <string>");
    registerBuiltinType(type::StrWide, Group::kStrWide, 1, "std::wstring", "#include <string>");
    registerBuiltinType(type::Rational, Group::kRationals, 1);

    // Ellipsis types
    registerBuiltinType(type_category::EllipsisAny, Group::kEllipsis, 1);
    registerBuiltinType(type_category::EllipsisTyped, Group::kEllipsis, 2);

    // Aliases — register as separate types based on existing concrete types
    registerType(type::Char, int8Id);
    registerType(type::Byte, uint8Id);
    registerType(type::Word, uint16Id);
    registerType(type::DWord, uint32Id);
    registerType(type::DDWord, uint64Id);
    registerType(type::Single, float32Id);
    registerType(type::Double, float64Id);

    // String alias for StrChar
    registerType("String", strCharId);

    // Integer alias for Int64 (generic integer → largest supported)
    auto int64Id = m_name_to_id.at(type::Int64);
    registerType("Integer", int64Id);

    // Abstract group types (Data = 0 → TypeClass::kComplex)
    registerBuiltinType(type_generic::Any, Group::kAny, 0, "std::any", "#include <any>");
    registerBuiltinType(type_category::Struct, Group::kStructured);
    registerBuiltinType(type_category::Enum, Group::kStructured);
    registerBuiltinType(type_category::Variant, Group::kStructured);
    registerBuiltinType(type_category::Tuple, Group::kStructured);
    registerBuiltinType(type_category::Optional, Group::kStructured);
    registerBuiltinType(type_category::Expected, Group::kStructured);
    registerBuiltinType(type_category::Function, Group::kCallable);
    registerBuiltinType(type_category::Closure, Group::kCallable);
    registerBuiltinType(type_category::Coroutine, Group::kCallable);
    registerBuiltinType(type_category::Generator, Group::kCallable);
    registerBuiltinType(type_category::Method, Group::kCallable);
    registerBuiltinType(type_category::Delegate, Group::kCallable);
    registerBuiltinType(type_category::Class, Group::kClasses);
    registerBuiltinType(type_category::Interface, Group::kClasses);
    registerBuiltinType(type_category::Range, Group::kRanges);
    registerBuiltinType(type_category::Slice, Group::kRanges);
    registerBuiltinType(type_category::View, Group::kRanges);
    registerBuiltinType(type_category::Forward, Group::kIterators);
    registerBuiltinType(type_category::Bidirectional, Group::kIterators);
    registerBuiltinType(type_category::RandomAccess, Group::kIterators);
    registerBuiltinType(type_category::Duration, Group::kDateTime);
    registerBuiltinType(type_category::TimePoint, Group::kDateTime);
    registerBuiltinType(type_category::Future, Group::kAsync);
    registerBuiltinType(type_category::Awaitable, Group::kAsync);
    registerBuiltinType(type_category::Thread, Group::kSync);
    registerBuiltinType(type_category::Mutex, Group::kSync);
    registerBuiltinType(type_category::Condition, Group::kSync);
    registerBuiltinType(type_category::Exception, Group::kExceptions);
    registerBuiltinType(type_category::Error, Group::kExceptions);
    registerBuiltinType(type_category::Arithmetics, Group::kArithmetics);
    registerBuiltinType(type_category::Native, Group::kNative);

    // Template parameter type
    registerBuiltinType(type_category::TemplateParam, Group::kTemplateParam);

    // Generalized types (Data = 0)
    registerBuiltinType(type_generic::Integers, Group::kIntegers);
    registerBuiltinType(type_generic::Numbers, Group::kNumbers);
    registerBuiltinType(type_generic::Strings, Group::kStrChar);
    registerBuiltinType(type_generic::Tensors, Group::kTensors);
}

// ── Register builtin type ────────────────────────────────
TypeId TypeRegistry::registerBuiltinType(std::string_view name, Group group, uint8_t data, std::string_view cpp_name, std::string_view preproc_include) {
    if (isAbstractGroup(group)) {
        EXPECT(data == 0 && "abstract group must have data=0");
    } else if (data == 0) {
        // Alias: data=0 for a normally-concrete group — allowed.
    } else {
        EXPECT(data != 0 && "concrete group must have data!=0");
    }

    EXPECT(m_name_to_id.find(name) == m_name_to_id.end() && "duplicate builtin type name");

    auto tc = (data != 0) ? TypeClass::kTrivial : TypeClass::kComplex;
    TypeKind kind = makeTypeKind(group, data, tc);
    kind = setBuiltinFlag(kind); // mark as builtin
    TypeId id = makeTypeId(kind, static_cast<uint32_t>(m_descriptors.size() + 1));
    m_name_to_id[name] = id;
    if (!cpp_name.empty()) {
        m_name_to_id[cpp_name] = id;
    }
    m_descriptors.push_back({
        name,                         // name
        {},                           // attrs (builtin have no attrs)
        {},                           // sourceRange (builtin have no source)
        std::string(cpp_name),        // cppName
        std::string(preproc_include), // preprocInclude
        INVALID_TYPE_ID,              // baseType — builtin types are not aliases
        SimpleTypeData{}              // data — builtin types are simple
    });
    return id;
}

// ── Lookup with error ───────────────────────────────────
TypeId TypeRegistry::getType(std::string_view name) const {
    auto it = m_name_to_id.find(name);
    if (it == m_name_to_id.end())
        FAULT("type '{}' not found", name);
    return it->second;
}

// ── Lookup with optional ────────────────────────────────
std::optional<TypeId> TypeRegistry::findType(std::string_view name) const noexcept {
    auto it = m_name_to_id.find(name);
    if (it == m_name_to_id.end())
        return std::nullopt;
    return it->second;
}

// ── Lookup by id ─────────────────────────────────────────
const TypeDescriptor* TypeRegistry::lookup(TypeId id) const {
    uint32_t idx = getIndexFromId(id);
    if (idx == 0)
        return nullptr;
    if (idx - 1 < m_descriptors.size())
        return &m_descriptors[idx - 1];
    return nullptr;
}

// ── Get full type name ───────────────────────────────────
std::string TypeRegistry::getFullTypeName(TypeId id) const {
    uint32_t idx = getIndexFromId(id);
    if (idx > 0 && idx - 1 < m_descriptors.size())
        return std::string(m_descriptors[idx - 1].name);
    return "Unknown";
}

// ── Get C++ type name ────────────────────────────────────
std::expected<std::string, std::string> TypeRegistry::getCppTypeName(TypeId id) const {
    auto idx = getIndexFromId(id);
    if (idx > 0 && idx - 1 < m_descriptors.size()) {
        auto& desc = m_descriptors[idx - 1];
        if (!desc.cppName.empty())
            return desc.cppName;
    }

    return std::unexpected(std::format("getCppTypeName: type '{}' has no C++ name", getFullTypeName(id)));
}

// ── Register user-defined type ──────────────────────────
TypeId TypeRegistry::registerType(std::string_view name, TypeId baseTypeId, std::vector<AttrId> attrs, MapperRange sourceRange,
                                  std::string_view preprocInclude) {
    auto it = m_name_to_id.find(name);
    if (it != m_name_to_id.end()) {
        MapperRange prevRange = getTypeSourceRange(it->second);
        if (!prevRange.isInvalid()) {
            m_ctx->report(prevRange, OptKind::ParseError, "previous definition of type '{}'", name);
        }
        m_ctx->report(sourceRange, OptKind::ParseError, "duplicate type name '{}'", name);
        return INVALID_TYPE_ID;
    }

    TypeKind kind = getKindFromId(baseTypeId);
    TypeId id = makeTypeId(kind, static_cast<uint32_t>(m_descriptors.size() + 1));
    m_descriptors.push_back({
        name,                        // name
        std::move(attrs),            // attrs
        sourceRange,                 // sourceRange
        {},                          // cppName (empty for aliases)
        std::string(preprocInclude), // preprocInclude
        baseTypeId,                  // baseType — points to the aliased type
        SimpleTypeData{}             // data — aliases are simple types
    });
    m_name_to_id[name] = id;
    return id;
}

// ── Structural uniquing ─────────────────────────────────
TypeId TypeRegistry::getOrCreateStructuralType(std::string_view name, TypeKind kind, std::vector<TypeId> children, std::optional<TypeData> data,
                                               std::string_view preprocInclude) {
    TypeKey key{kind, children};

    // Check if already exists via structural uniquing
    auto it = m_structural.find(key);
    if (it != m_structural.end())
        return it->second;

    // Check if name is already taken
    auto nameIt = m_name_to_id.find(name);
    if (nameIt != m_name_to_id.end()) {
        // If same TypeKey — return existing
        if (m_structural.find(key) != m_structural.end())
            return nameIt->second;

        // If name collision with different type — error
        m_ctx->report({}, OptKind::ParseError, "duplicate type name '{}'", name);
        return INVALID_TYPE_ID;
    }

    // Create new type
    TypeId id = makeTypeId(kind, static_cast<uint32_t>(m_descriptors.size() + 1));
    m_descriptors.push_back({
        name,                        // name
        {},                          // attrs (empty, caller should set after creation)
        {},                          // sourceRange
        {},                          // cppName
        std::string(preprocInclude), // preprocInclude
        INVALID_TYPE_ID,             // baseType — structural types are not aliases
        std::move(data)              // data
    });
    m_name_to_id[name] = id;
    m_structural[key] = id;
    return id;
}

// ── Get or create FunctionType ─────────────────────────
TypeId TypeRegistry::getOrCreateFunctionType(TypeId returnType, std::vector<TypeId> paramTypes, TypeId variadicType) {
    // Build children: [returnType, paramTypes..., variadicType (if variadic)]
    std::vector<TypeId> children;
    children.reserve(1 + paramTypes.size() + (variadicType != INVALID_TYPE_ID ? 1 : 0));
    children.push_back(returnType);
    children.insert(children.end(), paramTypes.begin(), paramTypes.end());
    if (variadicType != INVALID_TYPE_ID)
        children.push_back(variadicType);

    // Use a distinct TypeKind for function types: Group::kCallable, data=1
    TypeKind funcKind = makeTypeKind(Group::kCallable, 1);

    FunctionTypeData funcData{returnType, paramTypes, variadicType};

    // Use empty name — structural function types don't need a name
    return getOrCreateStructuralType("", funcKind, std::move(children), std::move(funcData));
}

// ── Get preprocessor include ────────────────────────────
std::string_view TypeRegistry::getPreprocInclude(TypeId id) const noexcept {
    uint32_t idx = getIndexFromId(id);
    if (idx > 0 && idx - 1 < m_descriptors.size())
        return m_descriptors[idx - 1].preprocInclude;
    return {};
}

// ── Get source range ────────────────────────────────────
MapperRange TypeRegistry::getTypeSourceRange(TypeId id) const {
    uint32_t idx = getIndexFromId(id);
    if (idx > 0 && idx - 1 < m_descriptors.size())
        return m_descriptors[idx - 1].sourceRange;
    return {};
}

// ── Canonical type id ───────────────────────────────────
TypeId TypeRegistry::getCanonicalTypeId(TypeId id) const noexcept {
    while (true) {
        uint32_t idx = getIndexFromId(id);
        if (idx == 0 || idx - 1 >= m_descriptors.size())
            return id;

        const auto& desc = m_descriptors[idx - 1];
        if (desc.baseType == INVALID_TYPE_ID)
            return id;

        id = desc.baseType; // follow alias chain
    }
}

// ── Accessors ────────────────────────────────────────────

TypeId TypeRegistry::getBaseType(TypeId id) const noexcept {
    uint32_t idx = getIndexFromId(id);
    if (idx > 0 && idx - 1 < m_descriptors.size())
        return m_descriptors[idx - 1].baseType;
    return INVALID_TYPE_ID;
}

const std::optional<TypeData>& TypeRegistry::getTypeData(TypeId id) const noexcept {
    static const std::optional<TypeData> kEmpty;

    uint32_t idx = getIndexFromId(id);
    if (idx > 0 && idx - 1 < m_descriptors.size())
        return m_descriptors[idx - 1].data;

    return kEmpty;
}

// ── Type checks ─────────────────────────────────────────

bool TypeRegistry::isTypeDataKind(TypeId id, TypeDataKind kind) const noexcept {
    const auto& data = getTypeData(id);
    if (!data)
        return false;

    switch (kind) {
    case TypeDataKind::kSimple:
        return std::holds_alternative<SimpleTypeData>(*data);
    case TypeDataKind::kFunction:
        return std::holds_alternative<FunctionTypeData>(*data);
    case TypeDataKind::kTemplate:
        return std::holds_alternative<TemplateTypeData>(*data);
    case TypeDataKind::kArray:
        return std::holds_alternative<ArrayTypeData>(*data);
    case TypeDataKind::kMemberPointer:
        return std::holds_alternative<MemberPointerTypeData>(*data);
    case TypeDataKind::kPackExpansion:
        return std::holds_alternative<PackExpansionTypeData>(*data);
    }
    return false;
}

bool TypeRegistry::isCompleteType(TypeId id) const noexcept {
    auto opt = getTypeData(id);
    return opt.has_value();
}

bool TypeRegistry::isForwardDecl(TypeId id) const noexcept {
    auto opt = getTypeData(id);
    return !opt.has_value();
}

} // namespace trust