#pragma once

#include <expected>
#include <functional>
#include <initializer_list>
#include <map>
#include <optional>
#include <string>
#include <string_view>
#include <unordered_map>
#include <variant>
#include <vector>

#include "types/type_id.hpp"
#include "types/group.hpp"
#include "types/runtime_symbols.hpp"
#include "ast/attr.hpp"
#include "location/location.hpp"

namespace trust {

// Forward declarations
class DiagnosticEngine;
class Options;

// -- TypeData - дополнительные данные для разных категорий типов --

// 1. Простой тип (примитив/алиас) - дополнительных данных не нужно
struct SimpleTypeData {};

// 2. Функция / прототип вызова: return + параметры + variadic type
struct FunctionTypeData {
    TypeId returnType;
    std::vector<TypeId> paramTypes;
    TypeId variadicType{INVALID_TYPE_ID}; // INVALID = не variadic,
                                          // AnyId = variadic с любым типом,
                                          // иначе = variadic только этого типа
};

// 3. Параметризованный тип / Template: сам шаблон + аргументы
//    Например: Vector<Int32> → templateTypeId=Vector(T), args=[Int32]
struct TemplateTypeData {
    TypeId templateTypeId;    // TypeId самого шаблона (e.g. Vector<T>)
    std::vector<TypeId> args; // аргументы шаблона (e.g. Int32)
};

// 4. Массив: тип элемента + размерности
struct ArrayTypeData {
    TypeId elementType;
    std::vector<uint64_t> dimensions; // пусто = incomplete, [n] = constant
    bool isVariadic{false};           // срез/открытый массив
    // Константность/фиксированность (`:Array^` → std::array) НЕ хранится здесь - это бит
    // kConstFlag в TypeId (withConst), как у любых типов; mutable (std::vector) - без бита.
};

// 5. Указатель на член структуры
struct MemberPointerTypeData {
    TypeId classType;  // класс/структура
    TypeId memberType; // тип члена
};

// 6. Ссылочный/указательный составной тип (вложенность). Первая ссылка на тип без
//    признака - fast-path бит (withRefType); ссылка на уже ссылочный тип (вложенность,
//    например shared<ptr<Int32>>) - узел с pointeeType-ребёнком. Вид ссылки несёт
//    сам TypeKind (RefType узла), pointeeType - тип, на который ссылаются.
struct RefTypeData {
    TypeId pointeeType; // тип, на который ссылается этот узел
};

// 7. Pack expansion (variadic template параметры)
struct PackExpansionTypeData {
    TypeId pattern; // тип, который повторяется
};

// -- Элементные данные членов живут в РЕЕСТРЕ, а не в AST ---------------------
// Tuple/Enum/Variant хранят СИНТАКСИЧЕСКУЮ форму членов в AST: DictLiteralNode.m_body =
// std::vector<ArgNode> (имя в text(), явный тип в m_type, значение в m_value; строится
// term_to_ast::appendDictElementsFromArgs). Здесь - КАНОНИЧЕСКИЕ, нормализованные семантические
// данные: уже разрешённые TypeId (и, для Enum, вычисленные значения-скаляры, см. ниже). Данные
// НЕ расширяют поля AST-узлов по тем же причинам, что у Dict/Tuple: AST пер-файловый, изменяемый
// (lowering, setKind) и является ВХОДОМ семантики; реестр - интернированный (getOrCreateTupleType),
// иммутабельный, переживает границу модуля (ModuleApi сериализует реестр, а не AST) и на него
// ссылаются TypeId/символы/методы. Чтение члена (имя/тип/значение) - напрямую из ArgNode.

// 8. Кортеж (структурный тип): упорядоченные элементы (имя, тип). Имя "" - позиционный
// элемент. Имена входят в структурную идентичность (см. getOrCreateTupleType / TypeKey::names).
// Значения-выражения элементов - в AST (dict.m_body), как у Dict; здесь только разрешённый тип.
struct TupleElementData {
    std::string name;
    TypeId type;
};
struct TupleTypeData {
    std::vector<TupleElementData> elements;
};

// 8b. Параметризованный диапазон Range<Elem>: структурный тип, интернируемый по элементному
// типу Elem (TypeKey children = [Elem]), данные - TemplateTypeData{templateTypeId=:Range,
// args=[Elem]}. Элементный тип читается из args[0] (единый механизм параметризованных типов).
// Методы Range объявлены ОДИН раз на абстрактном `:Range` (ключ с '%'/'^', напр. "%count^") и
// подставляются (T→Elem) при резолве для конкретного Range<Elem> (C++-модель шаблонов).

// 9. Типобезопасное перечисление (enum): единый тип значений + упорядоченный список членов.
//    valueType - тип значений членов (может быть нечисловым, напр. StrChar); члены хранятся в
//    порядке объявления (порядок = ordinal). Универсальное имя типа значений - вложенный алиас
//    `Color.Value` (аналог underlying_type). Члены - static constexpr константы типа enum;
//    вся работа с enum идёт ТОЛЬКО через имя типа (см. MEMORY.md, осознанное решение).
//    В отличие от Variant/Dict/Tuple, значение члена Enum - ВЫЧИСЛЯЕМЫЙ скаляр (автоинкремент/
//    ординал/единый тип значений), он не привязан к AST-выражению и нормализуется сюда
//    (EnumMemberData.value). Синтаксическая форма членов - DictLiteralNode.m_body (см. принцип
//    выше, раздел 8). Реестр хранит тип значений и вычисленные значения; AST - только вход.
struct EnumMemberData {
    std::string name;  // имя члена (e.g. "RED")
    std::string value; // вычисленное значение члена (текст литерала/автоинкремент); пусто = нет значения
};
struct EnumTypeData {
    TypeId valueType;                    // тип значений членов (Color.Value)
    std::vector<EnumMemberData> members; // члены в порядке объявления (ordinal)
};

// 10. Гетерогенный вариант (Variant, → std::variant): каждый член имеет СВОЙ тип.
//    В отличие от Enum (единый тип значений), члены варианта разнотипны; тип члена выводится
//    из его значения (или аннотации `:Type`). Узел `:Variant(name:Type=value, ...)` (как Tuple).
//    Здесь хранится только РАЗРЕШЁННЫЙ ТИП члена; ЗНАЧЕНИЕ члена - AST-выражение своего типа
//    (источник истины - ArgNode.m_value, читается в emitVariantStruct напрямую из ArgNode),
//    т.к. в отличие от Enum оно не вычисляется, а задаётся в исходнике.
struct VariantMemberData {
    std::string name; // имя члена (e.g. "RED")
    TypeId type;      // тип значения члена (свой для каждого)
};
struct VariantTypeData {
    std::vector<VariantMemberData> members; // члены в порядке объявления
};

// -- Объединение вариантов --
using TypeData = std::variant<SimpleTypeData, FunctionTypeData, TemplateTypeData, ArrayTypeData, MemberPointerTypeData, RefTypeData, PackExpansionTypeData,
                              TupleTypeData, EnumTypeData, VariantTypeData>;

// -- TypeDataKind - идентификатор варианта TypeData ----------
enum class TypeDataKind : uint8_t {
    kSimple,
    kFunction,
    kTemplate,
    kArray,
    kMemberPointer,
    kRefType,
    kPackExpansion,
    kTuple,
    kEnum,
    kVariant,
};

// -- TypeKey для структурного интернирования --------------
struct TypeKey {
    TypeKind kind;                  // TypeKind без registry_index
    std::vector<TypeId> children;   // все дочерние TypeId для сравнения
    std::vector<std::string> names; // имена (для Tuple - имена элементов; у прочих пусто)

    bool operator==(const TypeKey& other) const noexcept { return kind == other.kind && children == other.children && names == other.names; }
};

struct TypeKeyHash {
    size_t operator()(const TypeKey& key) const noexcept {
        size_t h = std::hash<uint32_t>{}(static_cast<uint32_t>(key.kind));
        for (auto child : key.children) {
            h ^= std::hash<uint64_t>{}(child) + 0x9e3779b9 + (h << 6) + (h >> 2);
        }
        for (const auto& n : key.names) {
            h ^= std::hash<std::string>{}(n) + 0x9e3779b9 + (h << 6) + (h >> 2);
        }
        return h;
    }
};

// -- Метод типа --------------------------------------------
// Описание метода `obj.method(...)` хранится как полный ключ-имя → интернированный
// функциональный тип (TypeId). Ключ кодирует нативность и константность: ведущий '%' -
// нативный член, хвостовой '^' - константный (напр. "%count^"). Нативность/константность
// ВЫВОДЯТСЯ из ключа, отдельно не хранятся (TypeId - интернированная сигнатура). Алиас -
// просто другое имя существующего метода: доверенное имя → целевой ключ (обязан повторять
// семантику цели: нативность и константность совпадают, проверяется EXPECT при регистрации).

// -- Type descriptor --------------------------------------
struct TypeDescriptor {
    std::string name;                         // canonical name (e.g., "Int32", "Vector") - владеющая копия
                                              // (для пользовательских алиасов имя из временной строки analyzeTypeDecl)
    std::vector<AttrId> attrs;                // атрибуты
    MapperRange sourceRange{};                // position of type declaration in source file
    std::string cppName;                      // C++ имя для codegen (e.g. "int32_t")
    std::vector<std::string> preprocIncludes; // директивы препроцессора для C++ кода; первый - основной
                                              // (e.g. "#include <cstdint>" / "@trust/dict.hpp"), пусто если не требуется
    TypeId baseType{INVALID_TYPE_ID};         // для алиас-цепочки: A → Byte → Int32
    std::optional<TypeData> data;             // nullopt = forward declaration (incomplete type)
    // Методы типа (obj.method): полный ключ (native '%', const '^') → интернированный
    // функциональный тип (FunctionTypeData). Нативность/константность - из ключа.
    std::map<std::string, TypeId> methods;
    // Алиасы методов: bare-имя алиаса → полный ключ цели (напр. "length" → "%count^").
    // Алиас обязан повторять семантику цели (native/const) - проверяется при регистрации.
    std::map<std::string, std::string> methodAliases;
};

// -- Runtime symbol -----------------------------------------
// C++-символ, присутствие которого в сгенерированном коде требует линковки
// trust-runtime библиотеки (и, как правило, инклуда её публичного заголовка).
// Символ ищется как подстрока в тексте сгенерированного файла; runtimeHeader -
// директива препроцессора с ведущим '@' (путь заголовка, совпадающий с именем
// ELF-секции внутри trust-runtime.so/.a).
struct RuntimeSymbol {
    // подстрока C++-кода, напр. "trust__abort__"
    std::string symbol;
    // список директив препроцессора с ведущим '@' (пути заголовков / ELF-секций trust-runtime)
    std::vector<std::string> runtimeHeaders;
};

// -- TypeRegistry -----------------------------------------
class TypeRegistry {
  public:
    explicit TypeRegistry(DiagnosticEngine& diag, const Options& opts);

    /// Сбрасывает реестр к начальному состоянию (только builtin-типы), очищая все
    /// пользовательские алиасы и структурные типы (в т.ч. функциональные сигнатуры).
    /// Вызывается на каждый запуск семантики, чтобы типы не накапливались между run().
    void reset();

    // -- Основные операции --

    TypeId getType(std::string_view name) const;
    std::optional<TypeId> findType(std::string_view name) const noexcept;
    const TypeDescriptor* lookup(TypeId id) const;
    std::string getFullTypeName(TypeId id) const;

    /// Итерация всех именованных типов (встроенные, пользовательские алиасы и именованные
    /// структурные, напр. Tuple/Dict). Анонимные типы (функциональные сигнатуры и др.) не
    /// включены. Порядок обхода не гарантирован. callback(name, isUserDefined).
    void forEachType(const std::function<void(std::string_view name, bool userDefined)>& cb) const;

    /// Returns the C++ name for a given TypeId (e.g. "int32_t" for Int32).
    /// Returns std::unexpected with an error message if no C++ name is registered.
    std::expected<std::string, std::string> getCppTypeName(TypeId id) const;

    /// Register a user-defined type based on an existing TypeId.
    /// Extracts TypeKind from baseTypeId, assigns a new registry_index,
    /// stores name + attrs + sourceRange + preprocInclude in m_descriptors.
    /// @return TypeId of the new type, or INVALID_TYPE_ID on duplicate.
    TypeId registerType(std::string_view name, TypeId baseTypeId, std::vector<AttrId> attrs = {}, MapperRange sourceRange = {},
                        std::string_view preprocInclude = {});

    /// Регистрирует пользовательский enum-тип (Group::kEnums, EnumTypeData).
    /// valueType - единый тип значений членов (Color.Value); members - члены в порядке
    /// объявления. НЕ алиас (baseType=INVALID, canonical = сам тип); имя уникально.
    /// @return TypeId нового типа, или INVALID_TYPE_ID при дубликате имени.
    TypeId registerEnumType(std::string_view name, TypeId valueType, std::vector<EnumMemberData> members, MapperRange sourceRange = {});

    /// Регистрирует пользовательский Variant-тип (Group::kVariants, VariantTypeData).
    /// Каждый член имеет СВОЙ тип (гетерогенный, → std::variant). НЕ алиас; имя уникально.
    /// @return TypeId нового типа, или INVALID_TYPE_ID при дубликате имени.
    TypeId registerVariantType(std::string_view name, std::vector<VariantMemberData> members, MapperRange sourceRange = {});

    /// Structural uniquing: create or retrieve a structural type identified by
    /// kind + children (+ имена для Tuple). Used for FunctionType, TemplateType, ArrayType, etc.
    TypeId getOrCreateStructuralType(std::string_view name, TypeKind kind, std::vector<TypeId> children, std::optional<TypeData> data = std::nullopt,
                                     std::string_view preprocInclude = {}, std::vector<std::string> names = {});

    /// Create or retrieve a structural Tuple type by elements (name, type).
    /// Имена элементов входят в структурную идентичность (TypeKey::names): два кортежа с
    /// одинаковыми типами, но разными именами полей - разные типы. Имена входят и в
    /// TupleTypeData (для резолва `t.name`). C++-представление - std::tuple (auto в коде).
    TypeId getOrCreateTupleType(std::vector<std::pair<std::string, TypeId>> elements);

    /// Create or retrieve a structural parameterized Range type `Range<Elem>` (Group::kRanges,
    /// Data=1) by element type. Интернируется по elementType (как Tuple): Range<Int64> и
    /// Range<Rational> - разные типы. Методы Range объявлены на абстрактном `:Range` с типовым
    /// параметром T и подставляются при резолве (см. instantiateRangeMethod). Заголовки
    /// `@trust/range.hpp` + транзитивные dict/rational.
    TypeId getOrCreateRangeType(TypeId elementType);

    /// true для параметризованного структурного типа `Range<Elem>` (TemplateTypeData с
    /// templateTypeId == :Range).
    bool isRangeType(TypeId id) const noexcept;
    /// Тип элемента диапазона (TemplateTypeData::args[0]); INVALID - не Range-тип.
    TypeId rangeElementType(TypeId id) const noexcept;
    /// Подставляет элементный тип в сигнатуру метода Range: для конкретного `Range<Elem>` -
    /// элементный тип Elem; для абстрактного `:Range` - Any. Возвращает интернированный
    /// функциональный тип с T→Elem (интернирование мутирует реестр → метод не const).
    /// funcType не-функциональный - возвращается как есть.
    TypeId instantiateRangeMethod(TypeId objType, TypeId templateFuncType);

    /// Create or retrieve a structural parameterized Array type `Array<Elem>` (Group::kContainers,
    /// Data=1) by element type. dims - известная размерность ([] = динамическая/неизвестная).
    /// Интернируется по (elementType, dims). Константная (фиксированная) форма `:Array^` - это
    /// kConstFlag-бит в TypeId (withConst), как у любых типов; mutable (std::vector) - без бита.
    /// Методы Array объявлены на абстрактном `:Array` (см. registerBuiltinTypes) и подставляются
    /// при резолве (instantiateArrayMethod).
    TypeId getOrCreateArrayType(TypeId elementType, std::vector<uint64_t> dimensions = {});

    /// true для параметризованного структурного типа `Array<Elem>` (ArrayTypeData).
    bool isArrayType(TypeId id) const noexcept;
    /// Тип элемента массива (ArrayTypeData::elementType); INVALID - не Array-тип.
    TypeId arrayElementType(TypeId id) const noexcept;
    /// Размерности массива (ArrayTypeData::dimensions); пусто - динамический/неизвестный.
    const std::vector<uint64_t>& arrayDimensions(TypeId id) const noexcept;
    /// Подставляет элементный тип в сигнатуру метода Array: для конкретного `Array<Elem>` -
    /// элементный тип Elem; для абстрактного `:Array` - Any. Возвращает интернированный
    /// функциональный тип с T→Elem (мутирует реестр → метод не const).
    TypeId instantiateArrayMethod(TypeId objType, TypeId templateFuncType);

    /// Create or retrieve a FunctionType by structural uniquing.
    /// @param returnType  TypeId of the return type (INVALID_TYPE_ID = Void).
    /// @param paramTypes  List of parameter TypeIds.
    /// @param variadicType INVALID_TYPE_ID = not variadic.
    /// @return TypeId of the created FunctionType.
    TypeId getOrCreateFunctionType(TypeId returnType, std::vector<TypeId> paramTypes, TypeId variadicType = INVALID_TYPE_ID);

    /// Get or create a structural reference/pointer type: node with a given RefType and a
    /// single pointee child. Used for nested references (a reference to an already-referenced
    /// type). Первая ссылка на тип без признака - fast-path бит withRefType (без узла).
    TypeId getOrCreateRefType(RefType kind, TypeId pointee);

    /// Применяет вид ссылки к типу: первая ссылка на тип без признака - fast-path бит
    /// withRefType (сохраняет нижние 32 бита TypeId: registry_index + флаги kConst/kInferred);
    /// ссылка на уже ссылочный тип - составной узел getOrCreateRefType. Единый источник
    /// применения @[reftype(...)] для семантики и транспилятора.
    TypeId applyRefType(TypeId base, RefType kind);

    // -- Методы типов (obj.method(...)) --

    /// Результат поиска метода: совпавший полный ключ (native '%'/const '^') + интернированный
    /// функциональный тип. Нативность/константность выводятся из key (не хранятся отдельно).
    struct MethodRef {
        std::string key; // полный ключ цели (напр. "%count^"); для алиаса - ключ цели
        TypeId funcType; // интернированная сигнатура (FunctionTypeData)
    };

    /// Регистрирует метод на типе. name - полный ключ: нативность ('%' в начале), константность
    /// ('^' в конце), напр. "%count^". funcType - интернированная сигнатура. aliases - доп.
    /// доверенные имена этого метода (полные ключи, напр. {"%length^"}); каждый алиас обязан
    /// повторять семантику цели (нативность и константность совпадают - иначе EXPECT с явной
    /// диагностикой); нативное C++-имя для кодгена берётся из ЦЕЛЕВОГО ключа (name). Дубликат
    /// (то же bare-имя + та же константность, независимо от '%') - ошибка EXPECT.
    void addMethod(TypeId type, std::string_view name, TypeId funcType, std::vector<std::string_view> aliases = {});

    /// Ищет метод по доверенному имени (нормализация срезом '%'/'^'), возвращает полный ключ +
    /// интернированный функциональный тип. Для алиаса возвращается ключ ЦЕЛИ (нативное имя из него).
    /// Для параметризованного Range<Elem> методы ищутся на абстрактном `:Range` (fallback).
    /// nullopt - метод не найден.
    [[nodiscard]] std::optional<MethodRef> findMethodInfo(TypeId type, std::string_view name) const;

    /// Ищет метод по имени и возвращает его интернированный функциональный тип (funcType из
    /// findMethodInfo). INVALID_TYPE_ID - метод не найден.
    [[nodiscard]] TypeId findMethod(TypeId type, std::string_view name) const;

    /// Returns the primary preprocInclude (первый из списка) for a registered type (by TypeId).
    /// For builtin types (no descriptor) returns empty string_view.
    std::string_view getPreprocInclude(TypeId id) const noexcept;

    /// Returns the full list of preproc includes (директив) for a registered type.
    const std::vector<std::string>& getPreprocIncludes(TypeId id) const noexcept;

    // -- Runtime symbols (линьковка рантайм-библиотеки) --

    /// Регистрирует рантайм-символ по типизированному идентификатору (единый источник
    /// имён/заголовков - types/runtime_symbols.hpp).
    /// Инвариант: символ НЕ должен дублировать тип, зарегистрированный через
    /// registerBuiltinType (заголовки типа уже покрываются по-типу, механизм №1).
    /// Нарушение - явная ошибка EXPECT при инициализации реестра (в т.ч. в тестах).
    void registerRuntimeSymbol(RuntimeSymbolId id);

    /// Список зарегистрированных рантайм-символов.
    const std::vector<RuntimeSymbol>& runtimeSymbols() const noexcept;

    /// Returns the sourceRange for a registered type (by TypeId).
    MapperRange getTypeSourceRange(TypeId id) const;

    /// Returns the canonical TypeId, i.e. resolves alias chains via baseType.
    /// For builtin types returns id unchanged.
    /// For aliases follows baseType until a non-alias type is found.
    /// For structural types (no baseType) returns id unchanged.
    TypeId getCanonicalTypeId(TypeId id) const noexcept;

    // -- Type info accessors --

    /// Returns the baseType from TypeDescriptor, or INVALID_TYPE_ID if not an alias.
    TypeId getBaseType(TypeId id) const noexcept;

    /// True для пользовательского типа (алиас, зарегистрированный семантикой), false для
    /// машинных типов и встроенных алиасов (Integer, String, Char...). Различие по registry_index:
    /// машинные типы регистрируются первыми (слоты 1..m_builtinCount), пользовательские - позже (>N).
    bool isUserDefinedType(TypeId id) const noexcept;

    /// Safely access TypeData from TypeDescriptor.
    const std::optional<TypeData>& getTypeData(TypeId id) const noexcept;

    /// Typed accessor - returns pointer to the requested TypeData variant,
    /// or nullptr if the type is not of the requested kind.
    template <typename T>
    const T* getTypeDataAs(TypeId id) const noexcept {
        const auto& data = getTypeData(id);
        if (data && std::holds_alternative<T>(*data)) {
            return &std::get<T>(*data);
        }
        return nullptr;
    }

    /// Check if the type data holds a specific variant kind.
    bool isTypeDataKind(TypeId id, TypeDataKind kind) const noexcept;

    /// Check if the type has non-nullopt data (complete type).
    bool isCompleteType(TypeId id) const noexcept;

    /// Check if the type has nullopt data (forward declaration).
    bool isForwardDecl(TypeId id) const noexcept;

  private:
    TypeId registerBuiltinType(std::string_view name, Group group, uint8_t data = 0, std::string_view cpp_name = {},
                               std::vector<std::string> preprocIncludes = {});
    void registerBuiltinTypes();

    // -- Общее иммутабельное ядро встроенных типов (полное определение - в registry.cpp) --
    struct BuiltinTypeCore;
    enum class BuiltinSeedTag {};
    /// Seed-конструктор: строит встроенное ядро В ЭТОМ экземпляре (один раз, внутри builtinCore()).
    TypeRegistry(DiagnosticEngine& diag, const Options& opts, BuiltinSeedTag);
    /// Возвращает общее иммутабельное ядро встроенных типов (ленивый, thread-safe).
    static const BuiltinTypeCore& builtinCore();
    /// Роутинг дескриптора по TypeId: встроенный - из ядра, пользовательский - из m_descriptors.
    const TypeDescriptor* descriptorOf(TypeId id) const noexcept;
    /// Мутабельный дескриптор ПОЛЬЗОВАТЕЛЬСКОГО типа; для встроенного - nullptr (иммутабельно).
    TypeDescriptor* userDescriptorOf(TypeId id) noexcept;

    DiagnosticEngine& m_diag;
    const Options& m_opts;

    // -- Lookup by name --
    std::unordered_map<std::string, TypeId> m_name_to_id;
    std::vector<TypeDescriptor> m_descriptors;

    /// Общее иммутабельное ядро встроенных типов (невладеющая ссылка; создаётся builtinCore()).
    const BuiltinTypeCore* m_builtin = nullptr;
    /// Кол-во встроенных дескрипторов (= m_builtin->builtinCount); пользовательские типы всегда > N.
    size_t m_builtinCount = 0;

    // -- Structural uniquing --
    std::unordered_map<TypeKey, TypeId, TypeKeyHash> m_structural;

    // -- Runtime symbols --
    std::vector<RuntimeSymbol> m_runtimeSymbols;
};

// -- Проверка «std::any»-типа ------------------------------
// Истина, если TypeId после раскрытия цепочки алиасов (канонизации) относится к
// Group::kAny (std::any). Единый источник проверки «any-операнд» для семантики
// выражений и кодогенерации (std::any_cast). INVALID_TYPE_ID → false.
inline bool isAnyType(TypeId id, const TypeRegistry& reg) noexcept {
    return id != INVALID_TYPE_ID && getGroup(getKindFromId(reg.getCanonicalTypeId(id))) == Group::kAny;
}

// -- Проверка «enum-тип» -------------------------------------
// Истина, если TypeId после канонизации относится к Group::kEnums (типобезопасное
// перечисление, EnumTypeData). Единый источник для семантики (резолв Color.RED / Color[k],
// типизация CompareOp) и кодогенерации (эмиссия enum-структуры и операторов). INVALID → false.
inline bool isEnumType(TypeId id, const TypeRegistry& reg) noexcept {
    return id != INVALID_TYPE_ID && getGroup(getKindFromId(reg.getCanonicalTypeId(id))) == Group::kEnums;
}

// -- Проверка «Variant-тип» ----------------------------------
// Истина, если TypeId после канонизации относится к Group::kVariants (гетерогенный вариант,
// VariantTypeData; каждый член своего типа). Единый источник для семантики (резолв Value.RED →
// тип члена) и кодогенерации (эмиссия std::variant<...>). INVALID → false.
inline bool isVariantType(TypeId id, const TypeRegistry& reg) noexcept {
    return id != INVALID_TYPE_ID && getGroup(getKindFromId(reg.getCanonicalTypeId(id))) == Group::kVariants;
}

// -- Проверка «многомерный массив» ---------------------------
// Истина, если TypeId - структурный Array-тип с НЕСКОЛЬКИМИ размерностями (`:Bool[3,4]`) ИЛИ
// элементный тип сам является массивом (вложенный `[[1,2],[3,4]]`). Многомерные массивы
// регистрируются в реестре, но генерация C++ (тензоры LibTorch) не реализована → диагностика.
inline bool isMultiDimArray(TypeId id, const TypeRegistry& reg) noexcept {
    if (!reg.isArrayType(id)) {
        return false;
    }
    const auto& dims = reg.arrayDimensions(id);
    const TypeId elem = reg.arrayElementType(id);
    return dims.size() > 1 || (elem != INVALID_TYPE_ID && reg.isArrayType(elem));
}

} // namespace trust