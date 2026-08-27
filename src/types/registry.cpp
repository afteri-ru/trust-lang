#include "types/registry.hpp"
#include "types/group.hpp"
#include "types/type_names.hpp"
#include "types/runtime_symbols.hpp"
#include "utils/error.hpp"
#include "utils/strings.hpp"
#include "diag/diag.hpp"
#include "diag/options.hpp"
#include "diag/base_diags.hpp"
#include "types/type_diag.hpp"
#include <unordered_map>

namespace trust {

// -- Роутинг дескрипторов: встроенные - из ядра, пользовательские - из экземпляра --
const TypeDescriptor* TypeRegistry::descriptorOf(TypeId id) const noexcept {
    const uint32_t idx = getIndexFromId(id);
    if (idx == 0) {
        return nullptr;
    }
    if (idx <= m_builtinCount) {
        return &m_builtin->descriptors[idx - 1];
    }
    const size_t u = static_cast<size_t>(idx) - 1 - m_builtinCount;
    return (u < m_descriptors.size()) ? &m_descriptors[u] : nullptr;
}

TypeDescriptor* TypeRegistry::userDescriptorOf(TypeId id) noexcept {
    const uint32_t idx = getIndexFromId(id);
    if (idx == 0 || idx <= m_builtinCount) {
        return nullptr; // встроенный - иммутабелен
    }
    const size_t u = static_cast<size_t>(idx) - 1 - m_builtinCount;
    return (u < m_descriptors.size()) ? &m_descriptors[u] : nullptr;
}

TypeRegistry::TypeRegistry(DiagnosticEngine& diag, const Options& opts)
: m_diag(diag)
, m_opts(opts) {
    m_builtin = &TypeRegistry::builtinCore();
    m_builtinCount = m_builtin->builtinCount;
}

// Seed-конструктор: строит встроенное ядро В ЭТОМ экземпляре. Используется ровно один раз
// внутри builtinCore(); обычные экземпляры разделяют готовое ядро и встроенные не строят.
TypeRegistry::TypeRegistry(DiagnosticEngine& diag, const Options& opts, TypeRegistry::BuiltinSeedTag)
: m_diag(diag)
, m_opts(opts) {
    m_builtin = nullptr;
    m_builtinCount = 0;
    registerBuiltinTypes();
}

void TypeRegistry::reset() {
    // Иммутабельное ядро встроенных типов разделяется всеми экземплярами (BuiltinTypeCore).
    m_builtin = &TypeRegistry::builtinCore();
    m_builtinCount = m_builtin->builtinCount;
    m_name_to_id.clear();  // пользовательские имена
    m_descriptors.clear(); // пользовательские дескрипторы
    m_structural.clear();
    m_runtimeSymbols.clear(); // пер-инстансовых рантайм-символов нет (встроенные - в ядре)
}

TypeId TypeRegistry::getType(std::string_view name) const {
    if (auto id = findType(name)) {
        return *id;
    }
    FAULT("type '{}' not found", name);
}

std::optional<TypeId> TypeRegistry::findType(std::string_view name) const noexcept {
    const std::string key(name);
    if (auto it = m_name_to_id.find(key); it != m_name_to_id.end()) {
        return it->second;
    }
    if (m_builtin) {
        if (auto it = m_builtin->name_to_id.find(key); it != m_builtin->name_to_id.end()) {
            return it->second;
        }
    }
    return std::nullopt;
}

const TypeDescriptor* TypeRegistry::lookup(TypeId id) const {
    return descriptorOf(id);
}

void TypeRegistry::forEachType(const std::function<void(std::string_view, bool)>& cb) const {
    for (const auto& [name, id] : m_name_to_id) { // пользовательские
        cb(name, true);
    }
    if (m_builtin) {
        for (const auto& [name, id] : m_builtin->name_to_id) { // встроенные
            cb(name, false);
        }
    }
}

std::string TypeRegistry::getFullTypeName(TypeId id) const {
    if (const TypeDescriptor* desc = descriptorOf(id)) {
        // Параметризованный Range<Elem>: отображаем с элементным типом (структурный тип имеет
        // пустое имя, дети-типы не читаются из desc->name).
        if (isRangeType(id)) {
            return "Range<" + getFullTypeName(rangeElementType(id)) + ">";
        }
        if (isArrayType(id)) {
            return "Array<" + getFullTypeName(arrayElementType(id)) + ">";
        }
        return desc->name;
    }
    return "Unknown";
}

std::expected<std::string, std::string> TypeRegistry::getCppTypeName(TypeId id) const {
    // Константность - ортогональный квалификатор: лидирующий `const `. RefType (вид ссылки,
    // биты 16-19) применяется к базовому имени pointee (суффикс `*`/`&`/`&&` либо обёртка
    // std::shared_ptr/std::weak_ptr/std::unique_ptr). Для вложенности (RefTypeData-узел)
    // базовое имя строится рекурсивно от pointee.
    const bool isConst = typeIsConst(id);
    const RefType rt = getRefType(getKindFromId(id));

    // Базовое C++-имя pointee/значения.
    std::string base;
    if (const auto* data = getTypeDataAs<RefTypeData>(id)) {
        // Структурный ссылочный узел: имя рекурсивно от pointee (вложенность).
        auto inner = getCppTypeName(data->pointeeType);
        if (!inner) {
            return std::unexpected(std::move(inner.error()));
        }
        base = std::move(*inner);
    } else {
        if (const TypeDescriptor* desc = descriptorOf(id)) {
            if (desc->cppName.empty()) {
                return std::unexpected(std::format("getCppTypeName: type '{}' has no C++ name", getFullTypeName(id)));
            }
            base = desc->cppName;
        } else {
            return std::unexpected(std::format("getCppTypeName: type '{}' has no C++ name", getFullTypeName(id)));
        }
    }

    // const применяется к базовому имени (pointee) перед обёрткой/суффиксом.
    if (isConst) {
        base = "const " + base;
    }

    switch (rt) {
    case RefType::kValue:
        return base;
    case RefType::kPtr:
        return base + "*";
    case RefType::kPtrPtr:
        return base + "**";
    case RefType::kTake:
        // Владеющая в рамках текущего скоупа: RAII-охранник trust::Take<T> (runtime-тип -
        // отдельная задача; здесь зарезервировано имя эмиссии).
        return "trust::Take<" + base + ">";
    case RefType::kRef:
        return base + "&";
    case RefType::kRref:
        return base + "&&";
    case RefType::kShared:
        return "std::shared_ptr<" + base + ">";
    case RefType::kWeak:
        return "std::weak_ptr<" + base + ">";
    case RefType::kUnique:
        return "std::unique_ptr<" + base + ">";
    case RefType::kMptr: {
        // Указатель на член: `MemberType Class::*` из структурного MemberPointerTypeData.
        const auto* mp = getTypeDataAs<MemberPointerTypeData>(id);
        if (!mp) {
            return std::unexpected(std::format("getCppTypeName: mptr requires MemberPointerTypeData, type '{}'", getFullTypeName(id)));
        }
        auto cls = getCppTypeName(mp->classType);
        auto mem = getCppTypeName(mp->memberType);
        if (!cls || !mem) {
            return std::unexpected("getCppTypeName: cannot render member pointer");
        }
        return *mem + " " + *cls + "::*";
    }
    }
    return std::unexpected(std::format("getCppTypeName: unknown RefType for '{}'", getFullTypeName(id)));
}

TypeId TypeRegistry::registerType(std::string_view name, TypeId baseTypeId, std::vector<AttrId> attrs, MapperRange sourceRange, std::string_view preprocInclude,
                                  bool hasTrust) {
    const std::string key(name);
    const bool inBuiltin = m_builtin && m_builtin->name_to_id.find(key) != m_builtin->name_to_id.end();
    auto it = m_name_to_id.find(key);
    if (it != m_name_to_id.end() || inBuiltin) {
        if (it != m_name_to_id.end()) {
            MapperRange prevRange = getTypeSourceRange(it->second);
            if (!prevRange.isInvalid()) {
                reportTypeDiag(m_diag, m_opts, diag::DiagId::ParseError, prevRange, "previous definition of type '{}'", name);
            }
        }
        reportTypeDiag(m_diag, m_opts, diag::DiagId::ParseError, sourceRange, "duplicate type name '{}'", name);
        return INVALID_TYPE_ID;
    }

    TypeKind kind = getKindFromId(baseTypeId);
    if (hasTrust) {
        kind = setTrustFlag(kind); // trust-условия - семантический дифференциатор идентичности алиаса
    }
    // registry_index для пользовательского типа = после всех встроенных (m_builtinCount).
    TypeId id = makeTypeId(kind, static_cast<uint32_t>(m_builtinCount + m_descriptors.size() + 1));
    m_descriptors.push_back({
        std::string(name),                                                                                           // name - владеющая копия
        std::move(attrs),                                                                                            // attrs
        sourceRange,                                                                                                 // sourceRange
        {},                                                                                                          // cppName (empty for aliases)
        preprocInclude.empty() ? std::vector<std::string>{} : std::vector<std::string>{std::string(preprocInclude)}, // preprocIncludes
        baseTypeId,                                                                                                  // baseType - points to the aliased type
        SimpleTypeData{}                                                                                             // data - aliases are simple types
    });
    m_name_to_id[std::string(name)] = id;
    return id;
}

TypeId TypeRegistry::registerEnumType(std::string_view name, TypeId valueType, std::vector<EnumMemberData> members, MapperRange sourceRange, bool hasTrust) {
    const std::string key(name);
    const bool inBuiltin = m_builtin && m_builtin->name_to_id.find(key) != m_builtin->name_to_id.end();
    auto it = m_name_to_id.find(key);
    if (it != m_name_to_id.end() || inBuiltin) {
        if (it != m_name_to_id.end()) {
            MapperRange prevRange = getTypeSourceRange(it->second);
            if (!prevRange.isInvalid()) {
                reportTypeDiag(m_diag, m_opts, diag::DiagId::ParseError, prevRange, "previous definition of type '{}'", name);
            }
        }
        reportTypeDiag(m_diag, m_opts, diag::DiagId::ParseError, sourceRange, "duplicate type name '{}'", name);
        return INVALID_TYPE_ID;
    }

    // Group::kEnums, data=1 - конкретный enum-тип (аналог kStructured для кортежа).
    TypeKind kind = makeTypeKind(Group::kEnums, 1);
    if (hasTrust) {
        kind = setTrustFlag(kind);
    }
    TypeId id = makeTypeId(kind, static_cast<uint32_t>(m_builtinCount + m_descriptors.size() + 1));
    EnumTypeData data{valueType, std::move(members)};
    m_descriptors.push_back({
        std::string(name), // name - владеющая копия
        {},                // attrs (пусто - атрибуты enum пока не используются)
        sourceRange,       // sourceRange - позиция объявления enum
        {},                // cppName (пусто - манглинг в кодогенерации: name_to_cpp)
        {},                // preprocIncludes (enum-структура самодостаточна; инклуды тянут типы значений)
        INVALID_TYPE_ID,   // baseType - enum НЕ алиас (canonical = сам тип)
        std::move(data)    // data - EnumTypeData
    });
    m_name_to_id[std::string(name)] = id;
    return id;
}

TypeId TypeRegistry::registerVariantType(std::string_view name, std::vector<VariantMemberData> members, MapperRange sourceRange, bool hasTrust) {
    const std::string key(name);
    const bool inBuiltin = m_builtin && m_builtin->name_to_id.find(key) != m_builtin->name_to_id.end();
    auto it = m_name_to_id.find(key);
    if (it != m_name_to_id.end() || inBuiltin) {
        if (it != m_name_to_id.end()) {
            MapperRange prevRange = getTypeSourceRange(it->second);
            if (!prevRange.isInvalid()) {
                reportTypeDiag(m_diag, m_opts, diag::DiagId::ParseError, prevRange, "previous definition of type '{}'", name);
            }
        }
        reportTypeDiag(m_diag, m_opts, diag::DiagId::ParseError, sourceRange, "duplicate type name '{}'", name);
        return INVALID_TYPE_ID;
    }

    // Group::kVariants, data=1 - конкретный вариант-тип.
    TypeKind kind = makeTypeKind(Group::kVariants, 1);
    if (hasTrust) {
        kind = setTrustFlag(kind);
    }
    TypeId id = makeTypeId(kind, static_cast<uint32_t>(m_builtinCount + m_descriptors.size() + 1));
    VariantTypeData data{std::move(members)};
    m_descriptors.push_back({
        std::string(name), // name - владеющая копия
        {},                // attrs
        sourceRange,       // sourceRange
        {},                // cppName (манглинг в кодогенерации: name_to_cpp)
        {},                // preprocIncludes (заголовки тянут типы членов; <variant> - кодогенерация)
        INVALID_TYPE_ID,   // baseType - Variant НЕ алиас
        std::move(data)    // data - VariantTypeData
    });
    m_name_to_id[std::string(name)] = id;
    return id;
}

std::string_view TypeRegistry::getPreprocInclude(TypeId id) const noexcept {
    if (const TypeDescriptor* desc = descriptorOf(id)) {
        if (!desc->preprocIncludes.empty()) {
            return desc->preprocIncludes.front(); // первый - основной заголовок типа
        }
    }
    return {};
}

const std::vector<std::string>& TypeRegistry::getPreprocIncludes(TypeId id) const noexcept {
    static const std::vector<std::string> kEmpty;
    if (const TypeDescriptor* desc = descriptorOf(id)) {
        return desc->preprocIncludes;
    }
    return kEmpty;
}

MapperRange TypeRegistry::getTypeSourceRange(TypeId id) const {
    if (const TypeDescriptor* desc = descriptorOf(id)) {
        return desc->sourceRange;
    }
    return {};
}

// -- Runtime symbols -------------------------------------
void TypeRegistry::registerRuntimeSymbol(RuntimeSymbolId id) {
    const std::string_view sym = runtimeSymbolName(id);
    const auto headers = runtimeSymbolHeaders(id);

    // Инвариант: символ НЕ должен дублировать тип, зарегистрированный через
    // registerBuiltinType (его заголовки уже покрываются по-типу, механизм №1).
    // Пример: trust::Dict регистрируется ТОЛЬКО как тип; добавление его как
    // рантайм-символа - ошибка (явная, при инициализации реестра / в тестах).
    for (const auto& desc : m_descriptors) {
        EXPECT(desc.cppName != sym && "runtime symbol duplicates a builtin type; headers already come from the type");
    }

    std::vector<std::string> hs(headers.begin(), headers.end());
    m_runtimeSymbols.push_back({std::string(sym), std::move(hs)});
}

const std::vector<RuntimeSymbol>& TypeRegistry::runtimeSymbols() const noexcept {
    // Встроенные рантайм-символы живут в общем ядре; пер-инстансовых нет.
    return m_builtin ? m_builtin->runtimeSymbols : m_runtimeSymbols;
}

TypeId TypeRegistry::getCanonicalTypeId(TypeId id) const noexcept {
    // Структурная идентичность: снимаем флаги «тип выведен» (kInferredFlag) и «константность»
    // (kConstFlag) - они не часть ключа интернирования (см. types/type_id.hpp, types/MEMORY.md).
    id = clearInferred(id);
    id = clearConst(id);
    while (true) {
        const TypeDescriptor* desc = descriptorOf(id);
        if (!desc || desc->baseType == INVALID_TYPE_ID) {
            return id;
        }
        id = desc->baseType; // follow alias chain
    }
}

TypeId TypeRegistry::getBaseType(TypeId id) const noexcept {
    if (const TypeDescriptor* desc = descriptorOf(id)) {
        return desc->baseType;
    }
    return INVALID_TYPE_ID;
}

bool TypeRegistry::isUserDefinedType(TypeId id) const noexcept {
    // Пользовательский тип = зарегистрирован позже машинных (registry_index > m_builtinCount).
    // INVALID (index==0) пользовательским не считается.
    uint32_t idx = getIndexFromId(id);
    return idx != 0 && idx > m_builtinCount;
}

const std::optional<TypeData>& TypeRegistry::getTypeData(TypeId id) const noexcept {
    static const std::optional<TypeData> kEmpty;

    if (const TypeDescriptor* desc = descriptorOf(id)) {
        return desc->data;
    }

    return kEmpty;
}

bool TypeRegistry::isTypeDataKind(TypeId id, TypeDataKind kind) const noexcept {
    const auto& data = getTypeData(id);
    if (!data) {
        return false;
    }

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
    case TypeDataKind::kRefType:
        return std::holds_alternative<RefTypeData>(*data);
    case TypeDataKind::kPackExpansion:
        return std::holds_alternative<PackExpansionTypeData>(*data);
    case TypeDataKind::kTuple:
        return std::holds_alternative<TupleTypeData>(*data);
    case TypeDataKind::kEnum:
        return std::holds_alternative<EnumTypeData>(*data);
    case TypeDataKind::kVariant:
        return std::holds_alternative<VariantTypeData>(*data);
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