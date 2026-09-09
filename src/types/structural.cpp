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
        EXPECT(!testFlag(c, SymbolFlag::Inferred) && "structural type child must not carry the inferred bit");
        EXPECT(!testFlag(c, SymbolFlag::Const) && "structural type child must not carry the const bit");
        EXPECT(!testFlag(c, SymbolFlag::Uninit) && "structural type child must not carry the uninit bit");
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
        EXPECT(!testFlag(type, SymbolFlag::Inferred) && "tuple element type must not carry the inferred bit");
        EXPECT(!testFlag(type, SymbolFlag::Const) && "tuple element type must not carry the const bit");
        EXPECT(!testFlag(type, SymbolFlag::Uninit) && "tuple element type must not carry the uninit bit");
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
    EXPECT(!testFlag(elementType, SymbolFlag::Inferred) && "range element type must not carry the inferred bit");
    EXPECT(!testFlag(elementType, SymbolFlag::Const) && "range element type must not carry the const bit");
    EXPECT(!testFlag(elementType, SymbolFlag::Uninit) && "range element type must not carry the uninit bit");
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
    EXPECT(!testFlag(elementType, SymbolFlag::Inferred) && "array element type must not carry the inferred bit");
    EXPECT(!testFlag(elementType, SymbolFlag::Const) && "array element type must not carry the const bit");
    EXPECT(!testFlag(elementType, SymbolFlag::Uninit) && "array element type must not carry the uninit bit");
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

// -- Пользовательские нативные шаблоны-типы (`<T> %std::vector() := ...;`) ----------------

TypeId TypeRegistry::registerNativeTemplate(std::string_view name, std::string_view cppTemplate, MapperRange sourceRange, std::string_view preprocInclude) {
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
    // Абстрактный шаблон: Group::kNativeTemplate, Data=0, name = trust-имя. NativeTemplateTypeData
    // с пустым args - маркер шаблона (cppTemplate хранит C++-имя). children пуст.
    TypeKind kind = makeTypeKind(Group::kNativeTemplate, 0);
    TypeId id = makeTypeId(kind, static_cast<uint32_t>(m_builtinCount + m_descriptors.size() + 1));
    NativeTemplateTypeData data{std::string(cppTemplate), {}};
    m_descriptors.push_back({
        std::string(name), // name - владеющая копия
        {},                // attrs
        sourceRange,       // sourceRange
        {},                // cppName (манглинг в кодогенерации; C++-имя - в NativeTemplateTypeData)
        preprocInclude.empty() ? std::vector<std::string>{} : std::vector<std::string>{std::string(preprocInclude)},
        INVALID_TYPE_ID, // baseType - не алиас
        std::move(data)  // data - NativeTemplateTypeData
    });
    m_name_to_id[std::string(name)] = id;
    return id;
}

TypeId TypeRegistry::getOrCreateNativeTemplateType(std::string_view cppTemplate, std::vector<TypeId> args, std::string_view preprocInclude) {
    for (const TypeId c : args) {
        EXPECT(!testFlag(c, SymbolFlag::Inferred) && "native template arg must not carry the inferred bit");
        EXPECT(!testFlag(c, SymbolFlag::Const) && "native template arg must not carry the const bit");
        EXPECT(!testFlag(c, SymbolFlag::Uninit) && "native template arg must not carry the uninit bit");
    }
    // Конкретная инстанциация: Data=1, children = типовые аргументы (интернирование по структуре).
    TypeKind kind = makeTypeKind(Group::kNativeTemplate, 1);
    NativeTemplateTypeData data{std::string(cppTemplate), args};
    return getOrCreateStructuralType("", kind, args, std::move(data), preprocInclude);
}

bool TypeRegistry::isNativeTemplateType(TypeId id) const noexcept {
    return getGroup(getKindFromId(getCanonicalTypeId(id))) == Group::kNativeTemplate;
}

std::string_view TypeRegistry::nativeTemplateCppName(TypeId id) const noexcept {
    const auto* td = getTypeDataAs<NativeTemplateTypeData>(getCanonicalTypeId(id));
    return td ? std::string_view(td->cppTemplate) : std::string_view{};
}

const std::vector<TypeId>& TypeRegistry::nativeTemplateArgs(TypeId id) const noexcept {
    static const std::vector<TypeId> kEmpty;
    const auto* td = getTypeDataAs<NativeTemplateTypeData>(getCanonicalTypeId(id));
    return td ? td->args : kEmpty;
}

// -- Forward-объявления нативных классов (`String ::= %std::string { ... };`) ----------------

TypeId TypeRegistry::registerNativeClass(std::string_view name, std::string_view cppName, MapperRange sourceRange, std::string_view preprocInclude) {
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
    // Нативный класс: Group::kNativeClass, Data=0, name = trust-имя, C++-имя - в
    // NativeClassTypeData{cppName}. C++-struct НЕ генерируется (класс определён в заголовке).
    TypeKind kind = makeTypeKind(Group::kNativeClass, 0);
    TypeId id = makeTypeId(kind, static_cast<uint32_t>(m_builtinCount + m_descriptors.size() + 1));
    NativeClassTypeData data{std::string(cppName)};
    m_descriptors.push_back({
        std::string(name), // name - владеющая копия
        {},                // attrs
        sourceRange,       // sourceRange
        {},                // cppName (манглинг НЕ используется; C++-имя - в NativeClassTypeData)
        preprocInclude.empty() ? std::vector<std::string>{} : std::vector<std::string>{std::string(preprocInclude)},
        INVALID_TYPE_ID, // baseType - не алиас
        std::move(data)  // data - NativeClassTypeData
    });
    m_name_to_id[std::string(name)] = id;
    return id;
}

bool TypeRegistry::isNativeClassType(TypeId id) const noexcept {
    return getGroup(getKindFromId(getCanonicalTypeId(id))) == Group::kNativeClass;
}

std::string_view TypeRegistry::nativeClassCppName(TypeId id) const noexcept {
    const auto* td = getTypeDataAs<NativeClassTypeData>(getCanonicalTypeId(id));
    return td ? std::string_view(td->cppName) : std::string_view{};
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

TypeId TypeRegistry::getOrCreateRefType(RefType kind, TypeId pointee, TypeId deleter, TypeId accessPolicy, TypeId impl) {
    // Составной ссылочный узел: отдельная группа kReftype (Data=1, RefType=вид), дети - pointee
    // (+ deleter для unique: D входит в тип; + accessPolicy: политика входит в тип;
    // + impl: класс реализации механизма доступа входит в тип).
    // Интернируется структурно по TypeKey{kind, children} - ссылка на уже ссылочный тип
    // (shared<ptr<Int32>>) получает свой узел, а не перезаписывает бит; unique<T,D1> != unique<T,D2>.
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

TypeId TypeRegistry::applyRefType(TypeId base, RefType kind) {
    // Fast-path бит допустим ТОЛЬКО когда base - не алиас: иначе канонизация (getCanonicalTypeId)
    // разворачивает baseType и теряет вид ссылки, а C++-имя pointee обязано остаться «как написано»
    // (`shared<MyInt>` → trust::Shared<c_MyInt>, а не канонический int32_t). Алиас (baseType != INVALID)
    // и вложенность - составной узел RefTypeData с pointee = base как написан.
    const bool alreadyRef = getRefType(getKindFromId(base)) != RefType::kValue;
    const bool isAlias = getBaseType(base) != INVALID_TYPE_ID;
    if (!alreadyRef && !isAlias) {
        // withRefType работает с TypeKind (uint32) и обнуляет registry_index, поэтому пересобираем
        // TypeId через replaceKind, сохраняя нижние 32 бита (registry_index + kSymbolFlagsMask-флаги).
        return replaceKind(base, withRefType(getKindFromId(base), kind));
    }
    // Структурный узел: child обязан быть без поведенческих флагов (инвариант интернирования,
    // getOrCreateStructuralType); флаги исходного base переносим на результат (как в fast-path).
    const TypeId node = getOrCreateRefType(kind, clearSymbolFlags(base));
    return (node & ~kSymbolFlagsMask) | (base & kSymbolFlagsMask);
}

TypeId TypeRegistry::applyRefType(TypeId base, RefType kind, TypeId deleter, TypeId accessPolicy, TypeId impl) {
    // С deleter/policy/impl тип ВСЕГДА структурный узел: TypeKind (uint32) не хранит TypeId D/Policy/Impl,
    // а для unique D и для sync Policy/Impl входят в тип. Без них - обычный путь (fast-path/вложенность).
    if (deleter == INVALID_TYPE_ID && accessPolicy == INVALID_TYPE_ID && impl == INVALID_TYPE_ID) {
        return applyRefType(base, kind);
    }
    // Структурный узел: child обязан быть без поведенческих флагов (инвариант интернирования);
    // флаги исходного base (const/inferred/uninit) переносим на результат (как в 2-арг пути).
    const TypeId node = getOrCreateRefType(kind, clearSymbolFlags(base), deleter, accessPolicy, impl);
    return (node & ~kSymbolFlagsMask) | (base & kSymbolFlagsMask);
}

// -- Пользовательские шаблон-классы (`<T> :Box ::= :Class{...}`) -----------------------------

TypeId TypeRegistry::getOrCreateTemplateParamType(std::string_view name) {
    // Структурная идентичность параметра - по имени (TypeKey::names); якорный child - встроенный
    // TemplateParam (getOrCreateStructuralType требует names.size()==children.size()).
    // Data=1 отличает идентификатор параметра от абстрактного встроенного :TemplateParam (Data=0).
    TypeKind kind = makeTypeKind(Group::kTemplateParam, 1);
    TemplateParamTypeData data{std::string(name)};
    return getOrCreateStructuralType("", kind, {getType(type_category::TemplateParam)}, std::move(data), "", {std::string(name)});
}

bool TypeRegistry::isTemplateParamType(TypeId id) const noexcept {
    return getGroup(getKindFromId(getCanonicalTypeId(id))) == Group::kTemplateParam;
}

std::string_view TypeRegistry::templateParamName(TypeId id) const noexcept {
    const auto* td = getTypeDataAs<TemplateParamTypeData>(getCanonicalTypeId(id));
    return td ? std::string_view(td->name) : std::string_view{};
}

bool TypeRegistry::isRecordTemplate(TypeId id) const noexcept {
    const auto* rd = getTypeDataAs<RecordTypeData>(getCanonicalTypeId(id));
    if (rd != nullptr) {
        return !rd->templateParams.empty();
    }
    // Объявленный, но не определённый шаблон: параметры живут в m_declaredRecords.
    if (auto it = m_declaredRecords.find(id); it != m_declaredRecords.end()) {
        return !it->second.empty();
    }
    return false;
}

const std::vector<TypeId>& TypeRegistry::recordTemplateParams(TypeId id) const noexcept {
    static const std::vector<TypeId> kEmpty;
    if (const auto* rd = getTypeDataAs<RecordTypeData>(getCanonicalTypeId(id))) {
        return rd->templateParams;
    }
    if (auto it = m_declaredRecords.find(id); it != m_declaredRecords.end()) {
        return it->second;
    }
    return kEmpty;
}

// Единая подстановка типовых параметров в произвольный тип (поля/базы/сигнатуры методов).
TypeId TypeRegistry::substituteTypeParams(const std::vector<std::pair<TypeId, TypeId>>& mapping, TypeId type) {
    if (type == INVALID_TYPE_ID || mapping.empty()) {
        return type;
    }
    for (const auto& [from, to] : mapping) {
        if (type == from) {
            return to;
        }
    }
    const TypeId canonical = getCanonicalTypeId(type);
    // Fast-path ref-вид (первая ссылка на тип без структурного узла): вид лежит в TypeKind, а
    // TypeData отсутствует, поэтому ветки ниже не сработали бы и `shared<T>` в шаблоне НЕ
    // подставился бы. Снимаем вид, подставляем pointee, применяем вид заново.
    {
        const RefType fastKind = getRefType(getKindFromId(canonical));
        if (fastKind != RefType::kValue && getTypeDataAs<RefTypeData>(canonical) == nullptr) {
            const TypeId pointee = getPointeeType(canonical);
            return applyRefType(substituteTypeParams(mapping, pointee), fastKind);
        }
    }
    // ВАЖНО: рекурсивные подстановки мутируют реестр (getOrCreate* пушат в m_descriptors →
    // реаллокация), поэтому указатели getTypeDataAs нельзя держать между вызовами - снимаем
    // нужные поля в ЛОКАЛЬНЫЕ копии ДО рекурсии.
    // Функциональная сигнатура (метод): возврат/параметры/variadic.
    if (const auto* fd = getTypeDataAs<FunctionTypeData>(canonical)) {
        const TypeId ret0 = fd->returnType;
        const TypeId var0 = fd->variadicType;
        const std::vector<TypeId> p0 = fd->paramTypes;
        std::vector<TypeId> params;
        params.reserve(p0.size());
        for (const TypeId p : p0) {
            params.push_back(substituteTypeParams(mapping, p));
        }
        const TypeId ret = substituteTypeParams(mapping, ret0);
        const TypeId var = substituteTypeParams(mapping, var0);
        return getOrCreateFunctionType(ret, std::move(params), var);
    }
    // Ссылочный узел (shared<T>/unique<T,...> и пр.).
    if (const auto* rd = getTypeDataAs<RefTypeData>(canonical)) {
        const RefType rk = getRefType(getKindFromId(canonical));
        const TypeId pointee0 = rd->pointeeType;
        const TypeId deleter0 = rd->deleterType;
        const TypeId policy0 = rd->accessPolicyType;
        const TypeId impl0 = rd->implType;
        return getOrCreateRefType(rk, substituteTypeParams(mapping, pointee0), substituteTypeParams(mapping, deleter0), substituteTypeParams(mapping, policy0),
                                  substituteTypeParams(mapping, impl0));
    }
    // Array<Elem> (размерности не зависят от параметра).
    if (const auto* ad = getTypeDataAs<ArrayTypeData>(canonical)) {
        const TypeId elem0 = ad->elementType;
        const std::vector<uint64_t> dims = ad->dimensions;
        return getOrCreateArrayType(substituteTypeParams(mapping, elem0), dims);
    }
    // Range<Elem> (TemplateTypeData).
    if (const auto* td = getTypeDataAs<TemplateTypeData>(canonical)) {
        if (!td->args.empty()) {
            const TypeId elem0 = td->args[0];
            return getOrCreateRangeType(substituteTypeParams(mapping, elem0));
        }
    }
    // Нативный шаблон-тип (`std::pair<T,U>`).
    if (const auto* nt = getTypeDataAs<NativeTemplateTypeData>(canonical)) {
        const std::string cppTpl = nt->cppTemplate;
        const std::vector<TypeId> a0 = nt->args;
        std::vector<TypeId> args;
        args.reserve(a0.size());
        for (const TypeId a : a0) {
            args.push_back(substituteTypeParams(mapping, a));
        }
        return getOrCreateNativeTemplateType(cppTpl, std::move(args), getPreprocInclude(canonical));
    }
    // Вложенная инстанциация record-шаблона (напр. база `Base<:T>`).
    if (const auto* rec = getTypeDataAs<RecordTypeData>(canonical)) {
        if (rec->templateOf != INVALID_TYPE_ID) {
            const TypeId tmplOf0 = rec->templateOf;
            const std::vector<TypeId> a0 = rec->templateArgs;
            std::vector<TypeId> args;
            args.reserve(a0.size());
            for (const TypeId a : a0) {
                args.push_back(substituteTypeParams(mapping, a));
            }
            return getOrCreateRecordTemplateInstance(tmplOf0, std::move(args));
        }
    }
    return type;
}

TypeId TypeRegistry::getOrCreateRecordTemplateInstance(TypeId templateId, std::vector<TypeId> args) {
    const TypeId tmpl = getCanonicalTypeId(templateId);
    const std::vector<TypeId> params = recordTemplateParams(tmpl);
    EXPECT(!params.empty() && "getOrCreateRecordTemplateInstance: not a record template");
    EXPECT(params.size() == args.size() && "getOrCreateRecordTemplateInstance: template arity mismatch");
    for (const TypeId a : args) {
        EXPECT(!testFlag(a, SymbolFlag::Inferred) && "record template arg must not carry the inferred bit");
        EXPECT(!testFlag(a, SymbolFlag::Const) && "record template arg must not carry the const bit");
        EXPECT(!testFlag(a, SymbolFlag::Uninit) && "record template arg must not carry the uninit bit");
    }
    std::vector<std::pair<TypeId, TypeId>> mapping;
    mapping.reserve(args.size());
    for (size_t i = 0; i < args.size(); ++i) {
        mapping.emplace_back(params[i], args[i]);
    }
    // Идентичность инстанциации: kind (группа шаблона, Data=1) + children = {templateId} + args.
    // templateId в детях обязателен: иначе Box<Int32> и Pair<Int32> совпали бы по (kind, children).
    std::vector<TypeId> children;
    children.reserve(args.size() + 1);
    children.push_back(tmpl);
    children.insert(children.end(), args.begin(), args.end());

    std::vector<TupleElementData> fields;
    // Копия полей ДО подстановки: substituteTypeParams мутирует реестр (реаллокация m_descriptors).
    // У объявленного (ещё не определённого) шаблона данных нет - поля пусты (self-инстанциация
    // внутри собственного тела; анализатор схлопывает её на сам шаблон).
    const RecordTypeData* trd = getTypeDataAs<RecordTypeData>(tmpl);
    const std::vector<TupleElementData> rawFields = trd != nullptr ? trd->fields : std::vector<TupleElementData>{};
    fields.reserve(rawFields.size());
    for (const auto& f : rawFields) {
        fields.push_back(TupleElementData{f.name, substituteTypeParams(mapping, f.type)});
    }
    // Базы/методы/имя шаблона - копируем ДО создания инстанциации: `substituteTypeParams` ниже
    // мутирует реестр (getOrCreate* пушат в m_descriptors → реаллокация) и указатели на
    // дескрипторы становятся висячими.
    std::vector<TypeId> tmplBases = baseClasses(tmpl); // копия (ссылка станет висячей)
    std::map<std::string, std::vector<TypeId>> tmplMethods;
    std::map<std::string, std::string> tmplAliases;
    std::string tmplName;
    if (const TypeDescriptor* tdesc = descriptorOf(tmpl)) {
        tmplMethods = tdesc->methods;
        tmplAliases = tdesc->methodAliases;
        tmplName = tdesc->name;
    }
    std::vector<TypeId> bases;
    bases.reserve(tmplBases.size());
    for (const TypeId b : tmplBases) {
        bases.push_back(substituteTypeParams(mapping, b));
    }
    std::map<std::string, std::vector<TypeId>> methods;
    for (const auto& [key, sigs] : tmplMethods) {
        std::vector<TypeId> substituted;
        substituted.reserve(sigs.size());
        for (const TypeId ft : sigs) {
            substituted.push_back(substituteTypeParams(mapping, ft));
        }
        methods[key] = std::move(substituted);
    }

    RecordTypeData data{std::move(fields)};
    data.templateOf = tmpl;
    data.templateArgs = std::move(args);
    TypeKind kind = makeTypeKind(getGroup(getKindFromId(tmpl)), 1);
    const TypeId id = getOrCreateStructuralType("", kind, std::move(children), std::move(data));
    TypeDescriptor* desc = userDescriptorOf(id);
    if (desc == nullptr) {
        return id; // инстанциация - пользовательский структурный тип; сюда попасть не должны
    }
    // desc НЕ держим через вызовы реестра - заполняем локальными копиями (реестр уже не мутируем).
    desc->name = tmplName; // имя для диагностик (в m_name_to_id не регистрируется)
    desc->baseClasses = std::move(bases);
    desc->methods = std::move(methods);
    desc->methodAliases = std::move(tmplAliases);
    return id;
}
} // namespace trust
