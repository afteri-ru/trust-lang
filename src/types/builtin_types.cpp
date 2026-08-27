// src/types/builtin_types.cpp
// Регистрация встроенных (машинных) типов и общее иммутабельное ядро BuiltinTypeCore.
// Модуль (декомпозиция registry.cpp).
#include "types/registry.hpp"
#include "types/group.hpp"
#include "types/type_names.hpp"
#include "types/runtime_symbols.hpp"
#include "utils/error.hpp"
#include "diag/diag.hpp"
#include "diag/options.hpp"
#include <unordered_map>
namespace trust {

namespace {
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
} // namespace

// -- Общее иммутабельное ядро встроенных типов -------------
// Определение структуры TypeRegistry::BuiltinTypeCore - в types/registry.hpp
// (её используют и registry.cpp, и builtin_types.cpp). Строится один раз и
// разделяется всеми экземплярами TypeRegistry; пользовательские типы живут
// в каждом экземпляре отдельно (m_descriptors).

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
} // namespace trust
