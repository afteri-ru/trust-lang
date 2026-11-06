#pragma once
// Payload-структуры TypeData (простые/функция/шаблон/массив/тензор/член/ссылка/pack/кортеж/enum/variant/native/record/типовой параметр).
// Часть модели данных типов: самостоятельным контрактом НЕ является (umbrella —
// types/type_data.hpp). Набор include повторяет umbrella, чтобы часть компилировалась
// независимо от порядка включения.

#include <cstdint>
#include <map>
#include <optional>
#include <string>
#include <string_view>
#include <utility>
#include <variant>
#include <vector>

#include "attrs/attr.hpp"
#include "location/decl_id.hpp"
#include "location/location.hpp"
#include "location/module_index.hpp"
#include "types/call/callable_info.hpp"
#include "types/group.hpp"
#include "types/type/ref_type.hpp"
#include "types/type_id.hpp"
#include "types/visibility.hpp"

namespace trust {

// 1. Простой тип (примитив/алиас) - дополнительных данных не нужно
struct SimpleTypeData {};

// 2. Функция / прототип вызова: return + параметры + variadic type
struct FunctionTypeData {
    // Все TypeId-поля — identity (getUnqualifiedType): occurrence-квалификаторы (const/inferred)
    // принадлежат QualType и не входят в функциональный тип.
    TypeId returnType;
    std::vector<TypeId> paramTypes;
    TypeId variadicType{INVALID_TYPE_ID}; // INVALID = не variadic,
                                          // AnyId = variadic с любым типом,
                                          // иначе = variadic только этого типа
    // Типовой пакет параметров (`f<T...>(xs: T...)`): pattern (обычно TemplateParam T).
    // INVALID - пакета нет. Отдельное поле, чтобы существующие потребители paramTypes
    // (перегрузка/методы/кодоген) не видели PackExpansion как конкретный тип.
    TypeId paramPackType{INVALID_TYPE_ID};
    // Позиция пакета: 0 = нет пакета, 1 = trailing (единственная поддерживаемая форма).
    uint8_t paramPackPosition{0};
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
    // Константность/фиксированность (`:Array^` → std::array) НЕ хранится здесь - это
    // qualifier QualType (withConst), как у любых типов; mutable (std::vector) - без бита.
};

// 4a. Тензор: тип элемента + статическая форма (пусто = динамическая/неизвестная).
// Вид хранения (dense/sparse) закодирован в Data TypeKind (1 = dense, 2 = sparse), поэтому
// отдельного поля нет - единственный источник истины.
struct TensorTypeData {
    TypeId elementType;
    std::vector<uint64_t> shape;
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
//    deleterType - deleter внешнего ресурса (`@[deleter(D)]`), только для unique
//    (`trust::Unique<T,D>`: D - часть типа, узел интернируется с D в детях). Для прочих
//    видов остаётся INVALID_TYPE_ID (для shared D стирается и в тип не входит).
//    accessPolicyType - РЕАЛЬНЫЙ тип политики доступа (Group::kAccessPolicy) для
//    backend shared/weak (`@[reftype("shared"/"weak", <Policy>)@]` -> trust::AccessShared<T,Policy>).
//    Часть ТИПА: входит в children/TypeKey (`shared<T,P1> != shared<T,P2>`). Иначе INVALID_TYPE_ID.
//    implType - класс реализации механизма доступа (3-й аргумент `@[reftype("<kind>", <policy>, <impl>)@]`):
//    механизм поверх политики-семантики. Часть ТИПА: входит в children/TypeKey
//    (`shared<T,P,I1> != shared<T,P,I2>`). Иначе INVALID_TYPE_ID.
struct RefTypeData {
    TypeId pointeeType;                        // тип, на который ссылается этот узел
    TypeId deleterType = INVALID_TYPE_ID;      // deleter ресурса (только unique; иначе INVALID)
    TypeId accessPolicyType = INVALID_TYPE_ID; // политика доступа (shared/weak); иначе INVALID
    TypeId implType = INVALID_TYPE_ID;         // класс реализации механизма доступа; иначе INVALID
};

// 7. Pack expansion (variadic template параметры)
struct PackExpansionTypeData {
    TypeId pattern; // тип, который повторяется
};

// Tuple/Enum/Variant хранят СИНТАКСИЧЕСКУЮ форму членов в AST: DictLiteralNode.m_body =
// std::vector<ArgNode> (имя в text(), явный тип в m_type, значение в m_value; строится
// term_to_ast::appendDictElementsFromArgs). Здесь - КАНОНИЧЕСКИЕ, нормализованные семантические
// данные: уже разрешённые TypeId (и, для Enum, вычисленные значения-скаляры, см. ниже). Данные
// НЕ расширяют поля AST-узлов по тем же причинам, что у Dict/Tuple: AST пер-файловый, изменяемый
// (lowering, setKind) и является ВХОДОМ семантики; реестр - интернированный (getOrCreateTupleType),
// иммутабельный, переживает границу модуля (ModuleApi сериализует реестр, а не AST) и на него
// ссылаются TypeId/символы/методы. Чтение члена (имя/тип/значение) - напрямую из ArgNode.

// 8. Кортеж (структурный тип): упорядоченные элементы (имя, тип). Имя "" - позиционный
// элемент. Имена входят в структурную идентичность (см. getOrCreateTupleType / TypeKeyChild::name).
// Значения-выражения элементов - в AST (dict.m_body), как у Dict; здесь только разрешённый тип.
// `type` - ОБЪЯВЛЕННЫЙ тип элемента/поля: QualType (identity + const/inferred), поэтому
// квалификатор объявленного члена (например `x:Int32^`) доезжает до типизации доступа и
// кодогенерации. Identity-проекция - `type.getTypeId()`; в TypeKey входит только она
// (квалификаторы не являются частью ключа интернирования).
struct TupleElementData {
    std::string name;
    QualType type;
    /// C++-доступ ЧЛЕНА класса (`class`/`struct` тела класса; Public по умолчанию). Для
    /// структурных кортежей/словарей/проекций всегда Public. Ставит семантика из
    /// `AstNodeBase::visibility` объявления поля; читает проверка доступа.
    Visibility access{Visibility::Public};
    /// Ключ связи формы с ОБЪЯВЛЕНИЕМ поля (R14/R23): `DeclId` члена AST. Ставит семантика при
    /// `defineRecord`; связывает запись формы с членом дерева ПО КЛЮЧУ, а не по индексу.
    /// Невалиден у структурных кортежей/словарей (у них объявления-члена нет).
    DeclId declId{};
};
struct TupleTypeData {
    std::vector<TupleElementData> elements;
};

// 8b. Параметризованный диапазон Range<Elem>: структурный тип, интернируемый по элементному
// типу Elem (TypeKey children = [Elem]), данные - TemplateTypeData{templateTypeId=:Range,
// args=[Elem]}. Элементный тип читается из args[0] (единый механизм параметризованных типов).
// Методы Range объявлены ОДИН раз на абстрактном `:Range` (структурный ключ метода, напр. {count, const, native}) и
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

// 11. Пользовательский нативный шаблон-тип (объявление `<T> %std::vector() := ...;` и его
//     инстанциация `vector<Int32>`). Идентичность — языковая: абстрактный шаблон (`templateOf`) +
//     типовые аргументы. C++-имя шаблона ("std::vector") — целевой факт (`CppTypeFacts`), в identity
//     НЕ входит (R37). Значение-аргументы (`array<Int32, 4>`) — в Этапе B2. Абстрактный шаблон
//     (объявление) — `templateOf == INVALID` и `args` пуст; конкретная инстанциация — валидный
//     `templateOf` и непустой `args`.
struct NativeTemplateTypeData {
    // Идентичность нативного шаблона — ЯЗЫКОВОЙ ключ: абстрактный шаблон (`templateOf`) + типовые
    // аргументы. C++-имя шаблона (`cppTemplate`) — целевой факт (`CppTypeFacts`), в identity НЕ входит
    // (R37, §4 ARCHITECTURE.md). `INVALID_TYPE_ID` — сам абстрактный шаблон (маркер `args.empty()`).
    TypeId templateOf{INVALID_TYPE_ID}; // абстрактный нативный шаблон; INVALID — этот узел и есть шаблон
    std::vector<TypeId> args;           // типовые аргументы (TypeId); пусто = абстрактный шаблон
};

// 12. Forward-объявление НАТИВНОГО класса (`String ::= %std::string { ... };`) - тип, отображаемый
//     на существующее C++-имя (cppName). Класс определён в C++-заголовке (инклуд из @[include],
//     хранится в `CppTypeFacts`); C++-struct НЕ генерируется. Члены-интерфейс (методы/поля/
//     конструкторы/статич-члены) - в TypeDescriptor::methods (как у встроенных типов). trust-имя
//     класса - в TypeDescriptor::name; отображается на cppName при использовании типа.
struct NativeClassTypeData {
    // Целевые C++-факты (cppName/include/libs) нативного класса — в `CppTypeFacts` (компонент `target`).
    // Здесь только маркер вида данных: C++-struct НЕ генерируется, класс определён в заголовке.
};

// 13. Пользовательский структурный тип (`Name ::= :Struct{...}` / `Name ::= :Class{...}`) -
//     единый record-тип для Struct и Class. Различаются ТОЛЬКО группой TypeKind
//     (Group::kStructs => POD + static_assert; Group::kClassDefs => Class); данные одинаковы.
//     Поля хранятся здесь (разрешённый тип поля - QualType, чтобы сохранялся `const`);
//     методы - в TypeDescriptor::methods; базовые классы (наследование) -
//     TypeDescriptor::baseClasses. C++-имя - манглинг trust-имени (nameToCpp), struct
//     эмитится провайдером-кодогенератором.
struct RecordTypeData {
    RecordTypeData(std::vector<TupleElementData> declaredFields, std::vector<TypeId> declaredTemplateParams = {}, TypeId instanceOfTemplate = INVALID_TYPE_ID,
                   std::vector<TypeId> instanceArgs = {})
    : fields(std::move(declaredFields))
    , templateParams(std::move(declaredTemplateParams))
    , templateOf(instanceOfTemplate)
    , templateArgs(std::move(instanceArgs)) {}

  private:
    /// Хранилище формы: читается ТОЛЬКО аксессорами владельца (`TypeStore::fieldsOf`/`basesOf`/
    /// `templateParamsOf`/`instanceOf`) снаружи `types` и `RecordTypeAccess` внутри зоны (E1/R1).
    friend struct RecordTypeAccess;

    std::vector<TupleElementData> fields; // поля в порядке объявления
    // Типовые параметры абстрактного шаблон-класса (`<T> :Box ::= :Class{...}`); пусто - НЕ шаблон.
    std::vector<TypeId> templateParams;
    // Инстанциация шаблон-класса: ссылка на абстрактный шаблон + типовые аргументы.
    // templateOf == INVALID_TYPE_ID - обычный record ИЛИ сам абстрактный шаблон (не инстанциация).
    TypeId templateOf{INVALID_TYPE_ID};
    std::vector<TypeId> templateArgs;
};

// 14. Идентификатор типового параметра пользовательского шаблона (`<T>`/`<T,U>` у record-шаблона).
//     Различает параметры по (владелец-шаблон, имя, позиция): поля/методы абстрактного шаблона несут
//     эти типы, при инстанциации они подставляются конкретными аргументами (getOrCreateRecordTemplateInstance).
//     `owner` — TypeId владельца-шаблона (record/класс; INVALID = не-scoped, напр. generic-функция);
//     `depth`/`index` — позиция по C++-модели TemplateTypeParmDecl. Имя — ТОЛЬКО для диагностик;
//     identity = kind + child(anchor) + name(TypeKeyChild::name) + TemplateParamTag{owner, depth, index}.
struct TemplateParamTypeData {
    std::string name;              // имя типового параметра (T/U/…) — только для диагностик
    TypeId owner{INVALID_TYPE_ID}; // владелец-шаблон; INVALID = не-scoped (интернируется по имени)
    uint32_t depth{0};             // глубина вложенных шаблонов (C++-модель)
    uint32_t index{0};             // позиция в списке параметров своего шаблона (C++-модель)
};

} // namespace trust
