#include "types/registry.hpp"
#include "types/ref_type.hpp"
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

// Заголовок C++-представления функционального значения (FunctionTypeData -> std::function<...>).
static constexpr std::string_view kFunctionalInclude = "#include <functional>";

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
    m_name_to_id.clear();      // пользовательские имена
    m_descriptors.clear();     // пользовательские дескрипторы
    m_declaredRecords.clear(); // объявленные-но-не-определённые record-типы
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
    // Ссылочный вид (fast-path бит ИЛИ структурный узел RefTypeData): показываем ВИД, а не только
    // базовый тип (напр. `shared<Int32>`, `unique<Int32, FreeDeleter>`). Для unique deleter
    // входит в тип и отображается вторым аргументом. Единый билдер - refTypeDisplayName
    // (types/ref_type.hpp), парный к refTypeCppName.
    const RefType rt = getRefType(getKindFromId(id));
    if (rt != RefType::kValue) {
        if (rt == RefType::kMptr) {
            if (const auto* mp = getTypeDataAs<MemberPointerTypeData>(id)) {
                return getFullTypeName(mp->memberType) + " " + getFullTypeName(mp->classType) + "::*";
            }
        }
        const std::string pointee = getFullTypeName(getPointeeType(id));
        std::string deleter;
        if (rt == RefType::kUnique) {
            if (const auto* rd = getTypeDataAs<RefTypeData>(id); rd && rd->deleterType != INVALID_TYPE_ID) {
                deleter = getFullTypeName(rd->deleterType);
            }
        }
        // Sync-backend: политика - второй аргумент отображаемого имени (`shared<T, Policy>`).
        if (rt == RefType::kShared || rt == RefType::kWeak) {
            if (const auto* rd = getTypeDataAs<RefTypeData>(id); rd && rd->accessPolicyType != INVALID_TYPE_ID) {
                return std::string(refTypeName(rt)) + "<" + pointee + ", " + getFullTypeName(rd->accessPolicyType) + ">";
            }
        }
        return refTypeDisplayName(rt, pointee, deleter);
    }
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
    // trust::Shared/trust::Weak/trust::Unique). Для вложенности (RefTypeData-узел)
    // базовое имя строится рекурсивно от pointee.
    const bool isConst = testFlag(id, SymbolFlag::Const);
    const RefType rt = getRefType(getKindFromId(id));

    // Функциональный тип (FunctionTypeData) - ЗНАЧЕНИЕ-функция: C++-представление `std::function<Ret(Args...)>`.
    // Встречается у значений-лямбд и переменных функционального типа. INVALID returnType = void;
    // INVALID тип параметра (не выведен) - `std::any`.
    if (getTypeDataAs<FunctionTypeData>(id) != nullptr) {
        const auto* ft = getTypeDataAs<FunctionTypeData>(id);
        std::string name = "std::function<";
        if (ft->returnType == INVALID_TYPE_ID) {
            name += "void";
        } else {
            auto ret = getCppTypeName(ft->returnType);
            if (!ret) {
                return std::unexpected(std::move(ret.error()));
            }
            name += *ret;
        }
        name += "(";
        for (size_t i = 0; i < ft->paramTypes.size(); ++i) {
            if (i) {
                name += ", ";
            }
            if (ft->paramTypes[i] == INVALID_TYPE_ID) {
                name += "std::any";
                continue;
            }
            auto p = getCppTypeName(ft->paramTypes[i]);
            if (!p) {
                return std::unexpected(std::move(p.error()));
            }
            name += *p;
        }
        name += ")>";
        if (isConst) {
            name = "const " + name;
        }
        return name;
    }

    // Базовое C++-имя pointee/значения.
    std::string base;
    if (const auto* data = getTypeDataAs<RefTypeData>(id)) {
        // Структурный ссылочный узел: имя рекурсивно от pointee (вложенность).
        auto inner = getCppTypeName(data->pointeeType);
        if (!inner) {
            return std::unexpected(std::move(inner.error()));
        }
        base = std::move(*inner);
    } else if (isNativeClassType(id)) {
        // Forward-объявление нативного класса: C++-имя живёт в NativeClassTypeData (cppName
        // дескриптора пуст). Нужно, например, для рендера deleter-типа `unique<T, D>`, где
        // D - пользовательский нативный класс.
        base = std::string(nativeClassCppName(id));
    } else if (const auto* tp = getTypeDataAs<TemplateParamTypeData>(getCanonicalTypeId(id)); tp != nullptr) {
        // Идентификатор типового параметра шаблона (`T`/`U`): C++-имя - само имя параметра
        // (внутри `template<typename T> struct ...` аргумент пишется как `T`).
        base = tp->name;
    } else {
        if (const TypeDescriptor* desc = descriptorOf(id)) {
            if (!desc->cppName.empty()) {
                base = desc->cppName;
            } else if (desc->baseType != INVALID_TYPE_ID) {
                // Простой алиас (`Integer`, `MyInt ::= :Int32`): C++-имя - «как написано».
                // Пользовательский алиас сохраняет trust-имя (манглинг, совпадает с `using c_...`),
                // встроенный алиас маппится на канонический C++-тип.
                if (isUserDefinedType(id)) {
                    base = utils::name_to_cpp(desc->name);
                } else if (auto canon = getCppTypeName(getCanonicalTypeId(id)); canon) {
                    base = std::move(*canon);
                } else {
                    return std::unexpected(std::move(canon.error()));
                }
            } else if (isRecordType(id)) {
                // Инстанциация пользовательского шаблон-класса (`Box<Int32>`): `c_Box<int32_t>`
                // (аргументы - рекурсивно; имя шаблона - из дескриптора шаблона). Определение
                // (template<...> struct) эмитит кодогенератор. Абстрактный шаблон/обычный record -
                // манглинг trust-имени (`c_Point`).
                const auto* rd = getTypeDataAs<RecordTypeData>(getCanonicalTypeId(id));
                if (rd != nullptr && rd->templateOf != INVALID_TYPE_ID) {
                    base = utils::name_to_cpp(getFullTypeName(rd->templateOf)) + "<";
                    for (size_t i = 0; i < rd->templateArgs.size(); ++i) {
                        if (i) {
                            base += ", ";
                        }
                        auto arg = getCppTypeName(rd->templateArgs[i]);
                        if (!arg) {
                            return std::unexpected(std::move(arg.error()));
                        }
                        base += *arg;
                    }
                    base += ">";
                } else {
                    base = utils::name_to_cpp(desc->name);
                }
            } else {
                return std::unexpected(std::format("getCppTypeName: type '{}' has no C++ name", getFullTypeName(id)));
            }
        } else {
            return std::unexpected(std::format("getCppTypeName: type '{}' has no C++ name", getFullTypeName(id)));
        }
    }

    // const применяется к базовому имени (pointee) перед обёрткой/суффиксом.
    if (isConst) {
        base = "const " + base;
    }

    if (rt == RefType::kMptr) {
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
    // Единый источник C++-имени вида ссылки (types/ref_type.hpp). Deleter внешнего ресурса
    // (`@[deleter(D)]`) - ЧАСТЬ типа `trust::Unique<T, D>`: рендерим D из RefTypeData::deleterType.
    std::string deleterCpp;
    if (rt == RefType::kUnique) {
        if (const auto* rd = getTypeDataAs<RefTypeData>(id); rd && rd->deleterType != INVALID_TYPE_ID) {
            auto del = getCppTypeName(rd->deleterType);
            if (!del) {
                return std::unexpected(std::move(del.error()));
            }
            deleterCpp = std::move(*del);
        }
    }
    // Sync-backend: shared/weak с политикой -> trust::AccessShared<T, Policy> (weak оборачивает его).
    // Композиция по РЕАЛЬНЫМ типам реестра: обёртка `type::AccessShared` и политика (RefTypeData::
    // accessPolicyType) - никакие строки-имена сюда не передаются. Политика всегда структурный узел,
    // поэтому RefTypeData тут присутствует.
    if (rt == RefType::kShared || rt == RefType::kWeak) {
        if (const auto* rd = getTypeDataAs<RefTypeData>(id); rd && rd->accessPolicyType != INVALID_TYPE_ID) {
            const auto syncId = findType(type::AccessShared);
            if (!syncId.has_value()) {
                return std::unexpected(std::format("getCppTypeName: sync wrapper type '{}' not found in the registry", type::AccessShared));
            }
            auto wrapper = getCppTypeName(*syncId);
            auto policy = getCppTypeName(rd->accessPolicyType);
            if (!wrapper) {
                return std::unexpected(std::move(wrapper.error()));
            }
            if (!policy) {
                return std::unexpected(std::move(policy.error()));
            }
            // Класс реализации механизма доступа (3-й аргумент reftype) - 3-й параметр AccessShared.
            std::string implArg;
            if (rd->implType != INVALID_TYPE_ID) {
                auto impl = getCppTypeName(rd->implType);
                if (!impl) {
                    return std::unexpected(std::move(impl.error()));
                }
                implArg = std::move(*impl);
            }
            const std::string strong = *wrapper + "<" + base + ", " + *policy + (implArg.empty() ? "" : (", " + implArg)) + ">";
            if (rt == RefType::kWeak) {
                return std::string(refTypeCppTemplateName(RefType::kWeak)) + "<" + strong + ">";
            }
            return strong;
        }
    }
    if (rt == RefType::kUnique) {
        // Внешний ресурс (`@[deleter(D)]`) требует указательной обёртки: trust::Unique<T, D>.
        if (!deleterCpp.empty()) {
            return refTypeCppName(RefType::kUnique, base, deleterCpp);
        }
        // Монопольное владение без deleter'а: обёртка StaticUnique (inline, zero-cost; имя из реестра).
        const auto wrapId = findType(type::StaticUnique);
        if (!wrapId.has_value()) {
            return std::unexpected(std::format("getCppTypeName: wrapper type '{}' not found in the registry", type::StaticUnique));
        }
        auto wrap = getCppTypeName(*wrapId);
        if (!wrap) {
            return std::unexpected(std::move(wrap.error()));
        }
        return *wrap + "<" + base + ">";
    }
    return refTypeCppName(rt, base, deleterCpp);
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
    if (!attrs.empty()) {
        kind = setAttrsFlag(kind); // признак «тип несёт атрибуты» (kHasAttrsFlag) -> сравнение через реестр
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

// Группа record-типа как человекочитаемый вид (для диагностик объявления/определения).
static constexpr std::string_view recordGroupName(Group g) noexcept {
    return (g == Group::kStructs) ? "Struct" : "Class";
}

TypeId TypeRegistry::declareRecord(std::string_view name, Group group, std::vector<TypeId> templateParams, MapperRange sourceRange, bool hasTrust) {
    EXPECT((group == Group::kStructs || group == Group::kClassDefs) && "declareRecord: group must be kStructs or kClassDefs");
    const std::string key(name);
    // Имя уже занято пользовательским типом?
    if (auto it = m_name_to_id.find(key); it != m_name_to_id.end()) {
        if (isRecordType(it->second)) {
            if (m_declaredRecords.find(it->second) == m_declaredRecords.end()) {
                MapperRange prevRange = getTypeSourceRange(it->second);
                if (!prevRange.isInvalid()) {
                    reportTypeDiag(m_diag, m_opts, diag::DiagId::ParseError, prevRange, "previous definition of type '{}'", name);
                }
                reportTypeDiag(m_diag, m_opts, diag::DiagId::ParseError, sourceRange, "duplicate type name '{}'", name);
                return INVALID_TYPE_ID;
            }
            // Идемпотентное повторное объявление неполного record (forward + определение).
            return it->second;
        }
        // Имя занято НЕ-record типом (алиас/enum/...): дубликат.
        MapperRange prevRange = getTypeSourceRange(it->second);
        if (!prevRange.isInvalid()) {
            reportTypeDiag(m_diag, m_opts, diag::DiagId::ParseError, prevRange, "previous definition of type '{}'", name);
        }
        reportTypeDiag(m_diag, m_opts, diag::DiagId::ParseError, sourceRange, "duplicate type name '{}'", name);
        return INVALID_TYPE_ID;
    }
    if (m_builtin && m_builtin->name_to_id.find(key) != m_builtin->name_to_id.end()) {
        reportTypeDiag(m_diag, m_opts, diag::DiagId::ParseError, sourceRange, "duplicate type name '{}'", name);
        return INVALID_TYPE_ID;
    }
    const bool isTemplate = !templateParams.empty();
    // Record-тип НЕ алиас (baseType=INVALID → canonical = сам тип). Шаблон - Data=0; обычный
    // record - Data=1. `data == nullopt` = объявлен, но не определён (defineRecord заполнит);
    // типовые параметры объявленного шаблона хранятся в m_declaredRecords (нужны для
    // self-инстанциации `Box<T>` внутри собственного тела ДО определения).
    TypeKind kind = makeTypeKind(group, isTemplate ? 0 : 1);
    if (hasTrust) {
        kind = setTrustFlag(kind);
    }
    TypeId id = makeTypeId(kind, static_cast<uint32_t>(m_builtinCount + m_descriptors.size() + 1));
    m_descriptors.push_back({
        std::string(name), // name - владеющая копия
        {},                // attrs
        sourceRange,       // sourceRange - позиция объявления
        {},                // cppName (манглинг в кодогенерации: name_to_cpp)
        {},                // preprocIncludes (struct самодостаточен; инклуды тянут типы полей/баз)
        INVALID_TYPE_ID,   // baseType - record НЕ алиас
        std::nullopt,      // data - объявлен, но НЕ определён (заполнит defineRecord)
        {},                // methods
        {},                // methodAliases
        {}                 // baseClasses (заполнит defineRecord)
    });
    m_declaredRecords.emplace(id, std::move(templateParams));
    m_name_to_id[key] = id;
    return id;
}
TypeId TypeRegistry::defineRecord(std::string_view name, Group group, std::vector<TupleElementData> fields, std::vector<TypeId> baseClasses,
                                  MapperRange sourceRange, bool hasTrust) {
    EXPECT((group == Group::kStructs || group == Group::kClassDefs) && "defineRecord: group must be kStructs or kClassDefs");
    const std::string key(name);
    auto it = m_name_to_id.find(key);
    if (it == m_name_to_id.end()) {
        // Не объявлен заранее: создаём сразу полностью определённый record (обычный путь).
        if (m_builtin && m_builtin->name_to_id.find(key) != m_builtin->name_to_id.end()) {
            reportTypeDiag(m_diag, m_opts, diag::DiagId::ParseError, sourceRange, "duplicate type name '{}'", name);
            return INVALID_TYPE_ID;
        }
        TypeKind kind = makeTypeKind(group, 1);
        if (hasTrust) {
            kind = setTrustFlag(kind);
        }
        TypeId newId = makeTypeId(kind, static_cast<uint32_t>(m_builtinCount + m_descriptors.size() + 1));
        RecordTypeData data{std::move(fields)};
        m_descriptors.push_back({std::string(name), {}, sourceRange, {}, {}, INVALID_TYPE_ID, std::move(data), {}, {}, std::move(baseClasses)});
        m_name_to_id[key] = newId;
        return newId;
    }
    const TypeId id = it->second;
    if (!isRecordType(id)) {
        reportTypeDiag(m_diag, m_opts, diag::DiagId::ParseError, sourceRange, "type name '{}' is already used by a non-record type", name);
        return INVALID_TYPE_ID;
    }
    if (getGroup(getKindFromId(id)) != group) {
        reportTypeDiag(m_diag, m_opts, diag::DiagId::ParseError, sourceRange, "record '{}' was declared as {} and cannot be defined as {}", name,
                       recordGroupName(getGroup(getKindFromId(id))), recordGroupName(group));
        return INVALID_TYPE_ID;
    }
    auto declared = m_declaredRecords.find(id);
    if (declared == m_declaredRecords.end()) {
        // Record уже определён (или имя занято - выше) - повторное определение.
        MapperRange prevRange = getTypeSourceRange(id);
        if (!prevRange.isInvalid()) {
            reportTypeDiag(m_diag, m_opts, diag::DiagId::ParseError, prevRange, "previous definition of type '{}'", name);
        }
        reportTypeDiag(m_diag, m_opts, diag::DiagId::ParseError, sourceRange, "duplicate type name '{}'", name);
        return INVALID_TYPE_ID;
    }
    TypeDescriptor* desc = userDescriptorOf(id);
    EXPECT(desc != nullptr && "defineRecord: declared record must be a user type");
    RecordTypeData data{std::move(fields)};
    data.templateParams = std::move(declared->second);
    desc->data = TypeData{std::move(data)};
    desc->baseClasses = std::move(baseClasses);
    m_declaredRecords.erase(declared);
    return id;
}

bool TypeRegistry::isStructType(TypeId id) const noexcept {
    return getGroup(getKindFromId(getCanonicalTypeId(id))) == Group::kStructs;
}

bool TypeRegistry::isClassType(TypeId id) const noexcept {
    return getGroup(getKindFromId(getCanonicalTypeId(id))) == Group::kClassDefs;
}

bool TypeRegistry::isRecordType(TypeId id) const noexcept {
    const Group g = getGroup(getKindFromId(getCanonicalTypeId(id)));
    return g == Group::kStructs || g == Group::kClassDefs;
}

const RecordTypeData* TypeRegistry::recordData(TypeId id) const noexcept {
    return getTypeDataAs<RecordTypeData>(getCanonicalTypeId(id));
}

const std::vector<TypeId>& TypeRegistry::baseClasses(TypeId id) const noexcept {
    static const std::vector<TypeId> kEmpty;
    if (const TypeDescriptor* desc = descriptorOf(getCanonicalTypeId(id))) {
        return desc->baseClasses;
    }
    return kEmpty;
}

TypeId TypeRegistry::findField(TypeId type, std::string_view name) const noexcept {
    const TypeId canonical = getCanonicalTypeId(type);
    if (const auto* rd = getTypeDataAs<RecordTypeData>(canonical)) {
        for (const auto& f : rd->fields) {
            if (f.name == name) {
                return f.type;
            }
        }
    }
    // Наследование: поле может быть унаследовано от базового класса (обход с защитой от циклов).
    for (const TypeId base : baseClasses(canonical)) {
        const TypeId bc = getCanonicalTypeId(base);
        if (bc == canonical) {
            continue;
        }
        if (const TypeId ft = findField(bc, name); ft != INVALID_TYPE_ID) {
            return ft;
        }
    }
    return INVALID_TYPE_ID;
}

std::string_view TypeRegistry::getPreprocInclude(TypeId id) const noexcept {
    if (getTypeDataAs<FunctionTypeData>(id) != nullptr) {
        return kFunctionalInclude; // функциональное значение: std::function<...>
    }
    if (const TypeDescriptor* desc = descriptorOf(id)) {
        if (!desc->preprocIncludes.empty()) {
            return desc->preprocIncludes.front(); // первый - основной заголовок типа
        }
    }
    return {};
}

const std::vector<std::string>& TypeRegistry::getPreprocIncludes(TypeId id) const noexcept {
    static const std::vector<std::string> kEmpty;
    static const std::vector<std::string> kFunctional{"#include <functional>"};
    if (getTypeDataAs<FunctionTypeData>(id) != nullptr) {
        return kFunctional;
    }
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
    // Структурная идентичность: снимаем ВСЕ поведенческие флаги (kSymbolFlagsMask) через единый
    // слой (structuralType) - они не часть ключа интернирования (см. types/type_id.hpp, MEMORY.md).
    id = clearSymbolFlags(id);
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

TypeId TypeRegistry::getPointeeType(TypeId id) const noexcept {
    // Структурный ссылочный узел (ссылку на уже ссылочный тип): pointee - ребёнок узла.
    if (const auto* rd = getTypeDataAs<RefTypeData>(id)) {
        return rd->pointeeType;
    }
    // Fast-path бит ссылки: pointee - тот же тип со снятыми битами RefType.
    if (getRefType(getKindFromId(id)) != RefType::kValue) {
        return makeTypeId(withRefType(getKindFromId(id), RefType::kValue), getIndexFromId(id));
    }
    return id;
}

bool TypeRegistry::isUserDefinedType(TypeId id) const noexcept {
    // Пользовательский тип = зарегистрирован позже машинных (registry_index > m_builtinCount).
    // INVALID (index==0) пользовательским не считается.
    uint32_t idx = getIndexFromId(id);
    return idx != 0 && idx > m_builtinCount;
}

bool TypeRegistry::isAccessPolicyType(TypeId id) const noexcept {
    // Встроенная политика доступа: группа kAccessPolicy, Data=1..3 (политики shared/weak;
    // Data=4 - обёртка AccessShared). См. registerBuiltinTypes.
    const TypeKind k = getKindFromId(getCanonicalTypeId(id));
    const uint8_t d = getData(k);
    return getGroup(k) == Group::kAccessPolicy && d >= 1 && d <= 3;
}

bool TypeRegistry::isAccessPolicyTypeFor(RefType kind, TypeId id) const noexcept {
    if (kind == RefType::kShared || kind == RefType::kWeak) {
        return isAccessPolicyType(id);
    }
    // `unique` (монопольное владение) политик доступа не имеет.
    return false;
}

bool TypeRegistry::isDeleterPolicyType(TypeId id) const noexcept {
    // Встроенный deleter-тип ресурса: группа kDeleterPolicy, Data>=1 (напр. trust::FreeDeleter).
    // См. registerBuiltinTypes.
    const TypeKind k = getKindFromId(getCanonicalTypeId(id));
    return getGroup(k) == Group::kDeleterPolicy && getData(k) >= 1;
}

bool TypeRegistry::typesEqual(TypeId a, TypeId b) const {
    if (a == b) {
        return true;
    }
    const TypeKind ka = getKindFromId(a);
    const TypeKind kb = getKindFromId(b);
    // Fast-path (признак наличия атрибутов, kHasAttrsFlag): если ни у одного типа флага нет,
    // они различны и сравнивать их достаточно по TypeId - без обращения к реестру.
    if (!hasAttrsFlag(ka) && !hasAttrsFlag(kb)) {
        return false;
    }
    // Тип несёт атрибуты (или оба): полное сравнение через реестр.
    if (getCanonicalTypeId(a) != getCanonicalTypeId(b)) {
        return false;
    }
    const TypeDescriptor* da = descriptorOf(a);
    const TypeDescriptor* db = descriptorOf(b);
    if (!da || !db) {
        return false;
    }
    if (ka != kb) {
        return false;
    }
    return da->attrs == db->attrs;
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
    case TypeDataKind::kNativeTemplate:
        return std::holds_alternative<NativeTemplateTypeData>(*data);
    case TypeDataKind::kNativeClass:
        return std::holds_alternative<NativeClassTypeData>(*data);
    case TypeDataKind::kRecord:
        return std::holds_alternative<RecordTypeData>(*data);
    case TypeDataKind::kTemplateParam:
        return std::holds_alternative<TemplateParamTypeData>(*data);
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