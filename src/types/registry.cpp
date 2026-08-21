#include "types/registry.hpp"
#include "types/group.hpp"
#include "types/type_names.hpp"
#include "types/runtime_symbols.hpp"
#include "utils/error.hpp"
#include "utils/strings.hpp"
#include "diag/diag.hpp"
#include "diag/options.hpp"

#include <unordered_map>

namespace trust {

namespace {
// Отчёт диагностики через узкие сервисы (DiagnosticEngine + Options) вместо Context.
// Воспроизводит логику Context::report: severity берётся из Options, подавленная
// диагностика (severity отсутствует) не выводится.
template <typename... Args>
void reportTypeDiag(DiagnosticEngine& diag, const Options& opts, OptKind kind, MapperRange range, std::format_string<Args...> fmt, Args&&... args) {
    auto sev = opts.severity(kind);
    if (!sev.has_value()) {
        return;
    }
    diag.report(*sev, range, std::move(fmt), std::forward<Args>(args)...);
}
} // namespace

// -- Concrete groups (Data != 0) ----------------------------
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

// -- Общее иммутабельное ядро встроенных типов -------------
// Строится один раз (см. TypeRegistry::builtinCore) и разделяется всеми экземплярами
// TypeRegistry. Содержит только встроенные типы/алиасы, их методы и рантайм-символы;
// пользовательские типы живут в каждом экземпляре отдельно (m_descriptors).
struct TypeRegistry::BuiltinTypeCore {
    std::vector<TypeDescriptor> descriptors;            // встроенные дескрипторы (index = registry_index-1)
    std::unordered_map<std::string, TypeId> name_to_id; // встроенные имена (+ cpp-имена, алиасы)
    std::vector<RuntimeSymbol> runtimeSymbols;          // встроенные рантайм-символы
    size_t builtinCount = 0;                            // = descriptors.size()
};

const TypeRegistry::BuiltinTypeCore& TypeRegistry::builtinCore() {
    // thread-safe (C++11 magic static). Seed строит встроенные типы один раз; затем его
    // состояние копируется в иммутабельное ядро.
    static const BuiltinTypeCore core = []() -> BuiltinTypeCore {
        DiagnosticEngine diag;
        Options opts(diag);
        TypeRegistry seed(diag, opts, TypeRegistry::BuiltinSeedTag{});
        BuiltinTypeCore c;
        c.descriptors = std::move(seed.m_descriptors);
        c.name_to_id = std::move(seed.m_name_to_id);
        c.runtimeSymbols = std::move(seed.m_runtimeSymbols);
        c.builtinCount = c.descriptors.size();
        return c;
    }();
    return core;
}

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

// -- Constructor ------------------------------------------
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

// -- Register all builtin types --------------------------
void TypeRegistry::registerBuiltinTypes() {
    // Concrete builtin types (Data ≠ 0 → TypeClass::kTrivial)
    registerBuiltinType(type::Void, Group::kVoid, 1);
    registerBuiltinType(type::None, Group::kVoid, 2);
    auto boolId = registerBuiltinType(type::Bool, Group::kLogical, 1, "bool");
    auto int8Id = registerBuiltinType(type::Int8, Group::kIntegers, 8, "int8_t", {"#include <cstdint>"});
    registerBuiltinType(type::Int16, Group::kIntegers, 16, "int16_t", {"#include <cstdint>"});
    registerBuiltinType(type::Int32, Group::kIntegers, 32, "int32_t", {"#include <cstdint>"});
    registerBuiltinType(type::Int64, Group::kIntegers, 64, "int64_t", {"#include <cstdint>"});
    auto uint8Id = registerBuiltinType(type::UInt8, Group::kUnsigned, 8, "uint8_t", {"#include <cstdint>"});
    auto uint16Id = registerBuiltinType(type::UInt16, Group::kUnsigned, 16, "uint16_t", {"#include <cstdint>"});
    auto uint32Id = registerBuiltinType(type::UInt32, Group::kUnsigned, 32, "uint32_t", {"#include <cstdint>"});
    auto uint64Id = registerBuiltinType(type::UInt64, Group::kUnsigned, 64, "uint64_t", {"#include <cstdint>"});
    registerBuiltinType(type::Float16, Group::kNumbers, 16, "float16_t", {"#include <stdfloat>"});
    auto float32Id = registerBuiltinType(type::Float32, Group::kNumbers, 32, "float");
    auto float64Id = registerBuiltinType(type::Float64, Group::kNumbers, 64, "double");
    registerBuiltinType(type::BFloat16, Group::kBFloat, 16);
    registerBuiltinType(type::Complex32, Group::kComplex, 32, "std::complex<float>", {"#include <complex>"});
    registerBuiltinType(type::Complex64, Group::kComplex, 64, "std::complex<double>", {"#include <complex>"});
    auto strCharId = registerBuiltinType(type::StrChar, Group::kStrChar, 1, "std::string", {"#include <string>"});
    registerBuiltinType(type::StrWide, Group::kStrWide, 1, "std::wstring", {"#include <string>"});
    // CString - невладеющий указатель на C-строку (const char*): результат метода
    // StrChar.%c_str(), совместим с нативными C-функциями (printf %s).
    auto cstringId = registerBuiltinType(type::CString, Group::kStrChar, 2, "const char*", {});
    // Нативные методы StrChar (std::string) без параметров (в C++ вставляется идентификатор без '%').
    // Сигнатура метода - функциональный тип (метод и функция - одно и то же).
    addMethod(strCharId, "%c_str", getOrCreateFunctionType(cstringId, {}));
    addMethod(strCharId, "%data", getOrCreateFunctionType(cstringId, {}));
    addMethod(strCharId, "%size", getOrCreateFunctionType(uint64Id, {}));
    addMethod(strCharId, "%length", getOrCreateFunctionType(uint64Id, {}));
    addMethod(strCharId, "%empty", getOrCreateFunctionType(boolId, {}));
    // Rational: runtime-backed type. The preproc include starts with '@' = marker
    // "needs the trust-runtime library"; the rest is the header path (also the ELF
    // section name inside trust-runtime.so from which the header is extracted).
    registerBuiltinType(type::Rational, Group::kRationals, 1, "trust::Rational", {"@trust/rational.hpp"});
    // Dict: universal heterogeneous dictionary (runtime-backed type, header-only).
    // The preproc include starts with '@' = marker "needs the trust-runtime library";
    // the rest is the header path (also the ELF section name inside trust-runtime.so
    // from which the header is extracted). `Dictionary` is the canonical base name.
    auto dictId = registerBuiltinType(type::Dict, Group::kDicts, 1, "trust::Dict", {"@trust/dict.hpp", "@trust/rational.hpp"});
    registerType(type::Dictionary, dictId);

    // Runtime symbols: presence of these C++ symbols in generated code forces
    // linking the trust-runtime library and including its public headers.
    // Единый источник описания - types/runtime_symbols.hpp. Регистрируются ТОЛЬКО
    // не-типовые функции: типы Dict/Rational покрываются registerBuiltinType по-типу
    // (механизм №1 в транспиляторе) и не должны дублироваться как символы.
    for (size_t i = 0; i < static_cast<size_t>(RuntimeSymbolId::kCount); ++i) {
        registerRuntimeSymbol(static_cast<RuntimeSymbolId>(i));
    }

    // Ellipsis types
    registerBuiltinType(type_category::EllipsisAny, Group::kEllipsis, 1);
    registerBuiltinType(type_category::EllipsisTyped, Group::kEllipsis, 2);

    // Aliases - register as separate types based on existing concrete types
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
    auto int64Id = m_name_to_id.at(std::string(type::Int64));
    registerType("Integer", int64Id);

    // Abstract group types (Data = 0 → TypeClass::kComplex)
    auto anyId = registerBuiltinType(type_generic::Any, Group::kAny, 0, "std::any", {"#include <any>"});
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
    const TypeId rangeId = registerBuiltinType(type_category::Range, Group::kRanges);
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

    // Array: универсальный изменяемый массив (cpp-имя std::vector<Elem>). Структурные
    // `Array<Elem>` создаются getOrCreateArrayType (Group::kContainers, Data=1). Здесь -
    // абстрактный `:Array` (Data=0), на котором объявлены методы (как у `:Range`).
    registerBuiltinType(type::Array, Group::kContainers);

    // Template parameter type
    const TypeId templateParamId = registerBuiltinType(type_category::TemplateParam, Group::kTemplateParam);

    // Generalized types (Data = 0)
    registerBuiltinType(type_generic::Integers, Group::kIntegers);
    registerBuiltinType(type_generic::Numbers, Group::kNumbers);
    registerBuiltinType(type_generic::Strings, Group::kStrChar);
    registerBuiltinType(type_generic::Tensors, Group::kTensors);

    // -- Методы универсального диапазона `:Range` (рантайм trust::Range<Elem>) --
    // Ключ кодирует нативность ('%') и константность ('^'); все методы Range константные.
    // Сигнатуры объявлены ОДИН раз на абстрактном `:Range`; ЭЛЕМЕНТ-зависимые слоты помечены
    // типовым параметром T (Group::kTemplateParam) и подставляются (T→Elem) при резолве для
    // конкретного Range<Elem> (C++-модель шаблонов, см. getOrCreateRangeType/instantiateRangeMethod):
    //   start/stop/step → T, at(Int64) → T, contains(T) → Bool.
    // count/size/empty/toDict - конкретные типы (не зависят от элемента). toVector/toArray/toList/
    // reversed возвращают контейнер/диапазон, для которых элементная точность не моделируется → Any.
    // Алиас: trust `length` → нативное `count` (алиас повторяет семантику цели: нативный+константный).
    addMethod(rangeId, "%count^", getOrCreateFunctionType(int64Id, {}), {"%length^"});
    addMethod(rangeId, "%size^", getOrCreateFunctionType(int64Id, {}));
    addMethod(rangeId, "%empty^", getOrCreateFunctionType(boolId, {}));
    addMethod(rangeId, "%start^", getOrCreateFunctionType(templateParamId, {}));
    addMethod(rangeId, "%stop^", getOrCreateFunctionType(templateParamId, {}));
    addMethod(rangeId, "%step^", getOrCreateFunctionType(templateParamId, {}));
    addMethod(rangeId, "%at^", getOrCreateFunctionType(templateParamId, {int64Id}));
    addMethod(rangeId, "%contains^", getOrCreateFunctionType(boolId, {templateParamId}));
    addMethod(rangeId, "%toVector^", getOrCreateFunctionType(anyId, {}));
    addMethod(rangeId, "%toArray^", getOrCreateFunctionType(anyId, {}));
    addMethod(rangeId, "%toList^", getOrCreateFunctionType(anyId, {}));
    addMethod(rangeId, "%toDict^", getOrCreateFunctionType(dictId, {}));
    addMethod(rangeId, "%reversed^", getOrCreateFunctionType(anyId, {}));

    // -- Методы универсального массива `:Array` (std::vector<Elem> / std::array<Elem,N>) --
    // Модель как у `:Range`: сигнатуры объявлены ОДИН раз на абстрактном `:Array`; элемент-зависимые
    // слоты помечены типовым параметром T (Group::kTemplateParam) и подставляются (T→Elem) при
    // резолве для конкретного `Array<Elem>` (instantiateArrayMethod).
    //   at(Int64)→T, first/front/last/back→T, contains(T)→Bool, push_back(T)→Void, pop_back()→Void.
    // count/size/length/empty/clear/resize - не зависят от элемента (конкретные типы).
    // Универсальные операции (как в C++/Python/Rust/Go): count/size/length (size()),
    // empty (empty()), at(i) (at/bounds-check), first/front (front()), last/back (back()),
    // contains (поиск), push_back (push_back), pop_back (pop_back), clear (clear),
    // resize(n) (resize), reverse (std::reverse). Срезы/slice и insert/erase - следующая задача.
    const TypeId arrayId = getType(type::Array);
    addMethod(arrayId, "%count^", getOrCreateFunctionType(int64Id, {}), {"%length^"});
    addMethod(arrayId, "%size^", getOrCreateFunctionType(int64Id, {}));
    addMethod(arrayId, "%empty^", getOrCreateFunctionType(boolId, {}));
    addMethod(arrayId, "%at^", getOrCreateFunctionType(templateParamId, {int64Id}));
    addMethod(arrayId, "%first^", getOrCreateFunctionType(templateParamId, {}), {"%front^"});
    addMethod(arrayId, "%last^", getOrCreateFunctionType(templateParamId, {}), {"%back^"});
    addMethod(arrayId, "%contains^", getOrCreateFunctionType(boolId, {templateParamId}));
    addMethod(arrayId, "%push_back", getOrCreateFunctionType(INVALID_TYPE_ID, {templateParamId}));
    addMethod(arrayId, "%pop_back", getOrCreateFunctionType(INVALID_TYPE_ID, {}));
    addMethod(arrayId, "%clear", getOrCreateFunctionType(INVALID_TYPE_ID, {}));
    addMethod(arrayId, "%resize", getOrCreateFunctionType(INVALID_TYPE_ID, {int64Id}));
    addMethod(arrayId, "%reverse^", getOrCreateFunctionType(INVALID_TYPE_ID, {}));

    // Метод универсального словаря Dict (trust::Dict): size() → Int64 (рантайм Dict::size()).
    addMethod(dictId, "%size^", getOrCreateFunctionType(int64Id, {}));

    // Машинные (встроенные) типы зарегистрированы первыми; их количество фиксируется здесь.
    // Пользовательские алиасы и структурные типы всегда получают registry_index > m_builtinCount.
    m_builtinCount = m_descriptors.size();
}

// -- Register builtin type --------------------------------
TypeId TypeRegistry::registerBuiltinType(std::string_view name, Group group, uint8_t data, std::string_view cpp_name,
                                         std::vector<std::string> preprocIncludes) {
    if (isAbstractGroup(group)) {
        EXPECT(data == 0 && "abstract group must have data=0");
    } else if (data == 0) {
        // Alias: data=0 for a normally-concrete group - allowed.
    } else {
        EXPECT(data != 0 && "concrete group must have data!=0");
    }

    EXPECT(m_name_to_id.find(std::string(name)) == m_name_to_id.end() && "duplicate builtin type name");

    auto tc = (data != 0) ? TypeClass::kTrivial : TypeClass::kComplex;
    TypeKind kind = makeTypeKind(group, data, tc);
    kind = setBuiltinFlag(kind); // mark as builtin
    TypeId id = makeTypeId(kind, static_cast<uint32_t>(m_builtinCount + m_descriptors.size() + 1));
    m_name_to_id[std::string(name)] = id;
    if (!cpp_name.empty()) {
        m_name_to_id[std::string(cpp_name)] = id;
    }
    m_descriptors.push_back({
        std::string(name),          // name - владеющая копия (не висячий string_view)
        {},                         // attrs (builtin have no attrs)
        {},                         // sourceRange (builtin have no source)
        std::string(cpp_name),      // cppName
        std::move(preprocIncludes), // preprocIncludes - список директив (первый - основной)
        INVALID_TYPE_ID,            // baseType - builtin types are not aliases
        SimpleTypeData{}            // data - builtin types are simple
    });
    return id;
}

// -- Lookup with error -----------------------------------
TypeId TypeRegistry::getType(std::string_view name) const {
    if (auto id = findType(name)) {
        return *id;
    }
    FAULT("type '{}' not found", name);
}

// -- Lookup with optional --------------------------------
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

// -- Lookup by id -----------------------------------------
const TypeDescriptor* TypeRegistry::lookup(TypeId id) const {
    return descriptorOf(id);
}

// -- Iterate named types ----------------------------------
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

// -- Get full type name -----------------------------------
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

// -- Get C++ type name ------------------------------------
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

// -- Register user-defined type --------------------------
TypeId TypeRegistry::registerType(std::string_view name, TypeId baseTypeId, std::vector<AttrId> attrs, MapperRange sourceRange,
                                  std::string_view preprocInclude) {
    const std::string key(name);
    const bool inBuiltin = m_builtin && m_builtin->name_to_id.find(key) != m_builtin->name_to_id.end();
    auto it = m_name_to_id.find(key);
    if (it != m_name_to_id.end() || inBuiltin) {
        if (it != m_name_to_id.end()) {
            MapperRange prevRange = getTypeSourceRange(it->second);
            if (!prevRange.isInvalid()) {
                reportTypeDiag(m_diag, m_opts, OptKind::ParseError, prevRange, "previous definition of type '{}'", name);
            }
        }
        reportTypeDiag(m_diag, m_opts, OptKind::ParseError, sourceRange, "duplicate type name '{}'", name);
        return INVALID_TYPE_ID;
    }

    TypeKind kind = getKindFromId(baseTypeId);
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

// -- Регистрация enum-типа (Group::kEnums, EnumTypeData) ----
TypeId TypeRegistry::registerEnumType(std::string_view name, TypeId valueType, std::vector<EnumMemberData> members, MapperRange sourceRange) {
    const std::string key(name);
    const bool inBuiltin = m_builtin && m_builtin->name_to_id.find(key) != m_builtin->name_to_id.end();
    auto it = m_name_to_id.find(key);
    if (it != m_name_to_id.end() || inBuiltin) {
        if (it != m_name_to_id.end()) {
            MapperRange prevRange = getTypeSourceRange(it->second);
            if (!prevRange.isInvalid()) {
                reportTypeDiag(m_diag, m_opts, OptKind::ParseError, prevRange, "previous definition of type '{}'", name);
            }
        }
        reportTypeDiag(m_diag, m_opts, OptKind::ParseError, sourceRange, "duplicate type name '{}'", name);
        return INVALID_TYPE_ID;
    }

    // Group::kEnums, data=1 - конкретный enum-тип (аналог kStructured для кортежа).
    TypeKind kind = makeTypeKind(Group::kEnums, 1);
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

// -- Регистрация Variant-типа (Group::kVariants, VariantTypeData) ----
TypeId TypeRegistry::registerVariantType(std::string_view name, std::vector<VariantMemberData> members, MapperRange sourceRange) {
    const std::string key(name);
    const bool inBuiltin = m_builtin && m_builtin->name_to_id.find(key) != m_builtin->name_to_id.end();
    auto it = m_name_to_id.find(key);
    if (it != m_name_to_id.end() || inBuiltin) {
        if (it != m_name_to_id.end()) {
            MapperRange prevRange = getTypeSourceRange(it->second);
            if (!prevRange.isInvalid()) {
                reportTypeDiag(m_diag, m_opts, OptKind::ParseError, prevRange, "previous definition of type '{}'", name);
            }
        }
        reportTypeDiag(m_diag, m_opts, OptKind::ParseError, sourceRange, "duplicate type name '{}'", name);
        return INVALID_TYPE_ID;
    }

    // Group::kVariants, data=1 - конкретный вариант-тип.
    TypeKind kind = makeTypeKind(Group::kVariants, 1);
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
            reportTypeDiag(m_diag, m_opts, OptKind::ParseError, {}, "duplicate type name '{}'", name);
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
TypeId TypeRegistry::getOrCreateFunctionType(TypeId returnType, std::vector<TypeId> paramTypes, TypeId variadicType) {
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

    FunctionTypeData funcData{returnType, paramTypes, variadicType};

    // Use empty name - structural function types don't need a name
    return getOrCreateStructuralType("", funcKind, std::move(children), std::move(funcData));
}

// -- Get or create TupleType (структурный кортеж) ---------
// Структурный тип: дети = типы элементов, имена = имена элементов (входят в TypeKey::names,
// поэтому два кортежа с одинаковыми типами, но разными именами полей - разные типы).
// TupleTypeData хранит элементы (имя, тип) - источник для резолва `t.name`/`t.0`.
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
// Структурный тип: дети = [elementType] (входят в TypeKey), поэтому Range<Int64> и
// Range<Rational> - разные типы. Данные - TemplateTypeData{templateTypeId=:Range, args=[Elem]}
// (единый механизм параметризованных типов); элементный тип читается из args[0].
// Методы Range объявлены на абстрактном `:Range` (ключ с '%'/'^') и подставляются (T→Elem)
// в instantiateRangeMethod.
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
// funcType не-функциональный - возвращается как есть; без T - без изменений.
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
// Структурный тип: дети = [elementType] (входят в TypeKey); размерности и признак константности
// кодируются в TypeKey::names (дети - только типы, числа не являются TypeId). Данные -
// ArrayTypeData{elementType, dimensions, isConst}. Мутable-форма → std::vector<Elem>,
// константная/фиксированная → std::array<Elem,N> (кодогенерация по isConst).
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

void TypeRegistry::addMethod(TypeId type, std::string_view name, TypeId funcType, std::vector<std::string_view> aliases) {
    const TypeId canonical = getCanonicalTypeId(type);
    TypeDescriptor* desc = userDescriptorOf(canonical);
    EXPECT(desc != nullptr && "addMethod: unknown type (methods can only be added to user-defined types)");
    // Полный ключ - как передан (нативность '%' и константность '^' кодируются в имени).
    const std::string key(name);
    EXPECT(!utils::bare_name(key).empty() && "addMethod: empty method name");
    // Дубликат: то же bare-имя + та же константность (независимо от '%' - нативное и обычное
    // написание - один метод). const и не-const перегрузки с одинаковыми аргументами - разные.
    const bool isConst = utils::is_const_name(key);
    for (const auto& [k, ft] : desc->methods) {
        (void)ft;
        EXPECT((utils::bare_name(k) != utils::bare_name(key) || utils::is_const_name(k) != isConst) &&
               "addMethod: method already registered on type (same name+constness)");
    }
    desc->methods[key] = funcType;
    // Алиасы: новые доверенные имена этого метода. Обязаны полностью повторять семантику цели
    // (нативность и константность совпадают) и не конфликтовать с существующими именами.
    for (const auto& aliasName : aliases) {
        EXPECT(utils::is_native_name(aliasName) == utils::is_native_name(key) && "addMethod: alias and target must be both native or both non-native");
        EXPECT(utils::is_const_name(aliasName) == isConst && "addMethod: alias and target must be both const or both non-const");
        const std::string bare = utils::bare_name(aliasName);
        for (const auto& [k, ft] : desc->methods) {
            (void)ft;
            EXPECT(utils::bare_name(k) != bare && "addMethod: alias conflicts with an existing method");
        }
        EXPECT(!desc->methodAliases.count(bare) && "addMethod: alias already registered");
        desc->methodAliases[bare] = key;
    }
}

// Поиск метода в одном дескрипторе: алиас (bare → ключ цели) или метод с совпадающим bare-именем
// (предпочтительно с точным совпадением константности запроса, иначе первый).
static std::optional<TypeRegistry::MethodRef> findMethodInDescriptor(const TypeDescriptor& desc, std::string_view bare, bool wantConst) {
    if (auto it = desc.methodAliases.find(std::string(bare)); it != desc.methodAliases.end()) {
        if (auto m = desc.methods.find(it->second); m != desc.methods.end()) {
            return TypeRegistry::MethodRef{it->second, m->second};
        }
    }
    TypeRegistry::MethodRef fallback;
    bool have = false;
    for (const auto& [k, ft] : desc.methods) {
        if (utils::bare_name(k) == bare) {
            if (utils::is_const_name(k) == wantConst) {
                return TypeRegistry::MethodRef{k, ft};
            }
            if (!have) {
                fallback = {k, ft};
                have = true;
            }
        }
    }
    if (have) {
        return fallback;
    }
    return std::nullopt;
}

std::optional<TypeRegistry::MethodRef> TypeRegistry::findMethodInfo(TypeId type, std::string_view name) const {
    const TypeId canonical = getCanonicalTypeId(type);
    const std::string bare = utils::bare_name(name);
    if (bare.empty()) {
        return std::nullopt;
    }
    const bool wantConst = utils::is_const_name(name);
    // Собственный дескриптор типа.
    if (const TypeDescriptor* desc = descriptorOf(canonical)) {
        if (auto r = findMethodInDescriptor(*desc, bare, wantConst)) {
            return r;
        }
    }
    // Параметризованный Range<Elem> сам методов не несёт: они объявлены ОДИН раз на абстрактном
    // `:Range` (ключ с '%'/'^', типовой параметр T). Для структурного Range<Elem> ищем там
    // (сигнатуру с T подставит handleMethodCall/instantiateRangeMethod).
    if (isRangeType(canonical)) {
        if (const TypeDescriptor* rangeDesc = descriptorOf(getType(type_category::Range))) {
            if (auto r = findMethodInDescriptor(*rangeDesc, bare, wantConst)) {
                return r;
            }
        }
    }
    // Параметризованный Array<Elem> - аналогично: методы объявлены на абстрактном `:Array`.
    if (isArrayType(canonical)) {
        if (const TypeDescriptor* arrDesc = descriptorOf(getType(type::Array))) {
            if (auto r = findMethodInDescriptor(*arrDesc, bare, wantConst)) {
                return r;
            }
        }
    }
    return std::nullopt;
}

TypeId TypeRegistry::findMethod(TypeId type, std::string_view name) const {
    const auto m = findMethodInfo(type, name);
    return m ? m->funcType : INVALID_TYPE_ID;
}

// -- Get preprocessor includes ---------------------------
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

// -- Get source range ------------------------------------
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

// -- Canonical type id -----------------------------------
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

// -- Accessors --------------------------------------------

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

// -- Type checks -----------------------------------------

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