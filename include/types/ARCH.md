# Архитектура реализации системы типов

## Обзор

Система типов построена как комбинация битовой структуры для быстрой идентификации (TypeKind как uint32_t) и реестра для хранения метаданных пользовательских и параметризованных типов.

## TypeKind (uint32_t)

32-битный идентификатор, кодирующий фундаментальные характеристики типа без обращения к реестру.

| Биты | Поле | Бит | Диапазон | Назначение |
|------|------|-----|----------|------------|
| 0–7 | Group | 8 | 0..255 | Идентификатор группы (плоский enum) |
| 8–15 | Data | 8 | 0..255 | Размер/код: 0 = абстрактный (группа), !=0 = конкретный встроенный тип |
| 16–19 | RefKind | 4 | 0..15 | Семантика владения: None/Shared/Weak/Unique |
| 20–21 | TypeClass | 2 | 0..3 | Класс жизненного цикла: Trivial/Relocatable/Complex/Polymorphic |
| 22 | SizeUnit | 1 | 0..1 | Единица измерения Data: 0 = bits, 1 = bytes |
| 23 | BuiltinFlag | 1 | 0..1 | Флаг "встроенный тип". Устанавливается registerBuiltinType() |
| 24–31 | Reserved | 8 | 0..255 | Будущие флаги |

**Data=0 → абстрактный тип (группа). Data≠0 → конкретный тип.**

Проверка "является ли тип конкретным встроенным": `isBuiltinConcrete(k) == (getData(k) != 0)`.

## Group (8 бит)

Плоское перечисление `enum class Group : uint8_t`:

- `kAny = 0` — корень всех типов
- `kVoid = 1` — Void(Data=1) и None(Data=2)
- `kLogical` (2)…`kStrWide` (10) — встроенные группы с Data≠0
- `kTensors` (11)…`kNative` (22) — группы для реестра (Data=0)
- `kEllipsis` (23) — группа Ellipsis (произвольное кол-во аргументов)
- `kArithmetics` (24) — арифметические типы (продвижение)
- `kTemplateParam` (25) — тип-параметр шаблона (Data=depth)

## RefKind (4 бита)

| Значение | Имя | Семантика владения |
|----------|-----|-------------------|
| 0 | kNone | Полное владение (значение) |
| 1 | kShared | Совместное владение |
| 2 | kWeak | Слабая (не владеющая) ссылка |
| 3 | kUnique | Исключительное владение |
| 4–15 | — | Зарезервировано |

Функция `withRefKind()` создаёт новый TypeKind с изменённым полем RefKind без изменения остальных.

## Category

Отдельное перечисление `enum class Category : uint8_t` (≤ 32 категорий).
Каждая категория имеет бит в `CategoryMask` (uint32_t).
Группа → категория: `kGroupCategoryMask[group] & (1u << category)`.

## TypeId (uint64_t)

```cpp
using TypeId = uint64_t;  // { TypeKind (upper 32) | registry_index (lower 32) }
```

- Для встроенных типов registry_index = 0.
- Для реестровых типов registry_index = позиция в TypeRegistry + 1.
- Хелперы: `makeTypeId(kind, index)`, `getKindFromId(id)`, `getIndexFromId(id)`.

## TypeData — variant для различных категорий типов

Тип может иметь дополнительные данные, хранящиеся в `TypeDescriptor::data` (тип `std::optional<TypeData>`):

```cpp
struct SimpleTypeData {};              // примитивы и алиасы

struct FunctionTypeData {              // сигнатура функции
    TypeId returnType;
    std::vector<TypeId> paramTypes;
    TypeId variadicType;               // INVALID = не variadic,
                                       // Any = variadic с любым типом,
                                       // иначе = variadic только этого типа
};

struct TemplateTypeData {              // инстанцирование шаблона
    TypeId templateTypeId;             // TypeId самого шаблона (e.g. Vector<T>)
    std::vector<TypeId> args;          // аргументы шаблона (e.g. Int32)
};

struct ArrayTypeData {                 // массив
    TypeId elementType;
    std::vector<uint64_t> dimensions;  // пусто = incomplete
    bool isVariadic;                   // срез/открытый массив
};

struct MemberPointerTypeData {         // указатель на член
    TypeId classType;
    TypeId memberType;
};

struct PackExpansionTypeData {         // variadic pack expansion
    TypeId pattern;
};

using TypeData = std::variant<SimpleTypeData, FunctionTypeData, TemplateTypeData,
                              ArrayTypeData, MemberPointerTypeData, PackExpansionTypeData>;
```

- `data = nullopt` → forward declaration (неполный тип)
- `data = SimpleTypeData{}` → примитив или алиас
- Для builtin-типов (`registry_index = 0`) TypeDescriptor не создаётся

## TypeDescriptor

Хранит метаданные, НЕ закодированные в битовой структуре TypeKind:

```cpp
struct TypeDescriptor {
    std::string_view name;             // canonical name
    std::vector<AttrId> attrs;         // атрибуты (const, align, volatile и т.д.)
    MapperRange sourceRange{};         // позиция объявления типа в исходном файле
    std::string cppName;               // C++ имя для codegen (e.g. "int32_t")
    std::string preprocInclude;        // директива препроцессора для включения в C++ код (e.g. "#include <cstdint>"), пусто если не требуется
    TypeId baseType{INVALID_TYPE_ID};  // алиас-цепочка: A → Byte → Int32
    std::optional<TypeData> data;      // дополнительные данные
};
```

## Роль SimpleTypeData

`SimpleTypeData` — это пустая структура-маркер, необходимая для различения состояний `std::optional<TypeData>`:

| Значение `data` | Семантика |
|----------------|-----------|
| `std::nullopt` | Forward declaration (тип объявлен, но не определён) |
| `SimpleTypeData{}` | Тип полностью определён, структурных данных нет |
| `FunctionTypeData{...}` | Функциональный тип |
| `TemplateTypeData{...}` | Инстанцированный шаблон |

Без `SimpleTypeData` было бы невозможно отличить "fully defined simple type" от "forward declaration" — пришлось бы вводить отдельный флаг `isComplete`.

## Type names (type_names.hpp)

Константы имён типов разделены на три namespace в одном файле:

- `trust::type` — конкретные встроенные типы (Data ≠ 0): Void, Bool, Int8..Int64, Float32, StrChar, Ellipsis и т.д.
- `trust::type_generic` — обобщённые типы (Any + group-aliases): Any, Integers, Numbers, Strings, Tensors
- `trust::type_category` — абстрактные категории (Data = 0): Struct, Function, Class, Range, Exception и т.д.

Использование: `type::Int32`, `type_generic::Any`, `type_category::Function`.

Ellipsis-константы (`EllipsisAny`, `EllipsisTyped`) отнесены к `type_category`, так как они являются маркерами для variadic параметров, а не конкретными типами данных.

- `cppName` — для простых типов заполняется при регистрации в `registerBuiltinType()`, для пользовательских — при анализе AST
- `baseType` — для алиасов: `type Byte = Int8` → `baseType = Int8Id`
- `data` — `nullopt` для forward declaration, иначе один из вариантов TypeData

## TypeKey — структурное интернирование (FoldingSet)

Для параметризованных типов (функции, шаблоны, массивы) требуется, чтобы два одинаковых по структуре типа давали один и тот же TypeId:

```cpp
struct TypeKey {
    TypeKind kind;
    std::vector<TypeId> children;  // все дочерние TypeId (params, args, returnType...)
};
```

Хранилище: `std::unordered_map<TypeKey, TypeId, TypeKeyHash> m_structural`

Метод: `TypeId getOrCreateStructuralType(name, kind, children, data)`

Проверяет:
1. Если `TypeKey` уже существует → возвращает существующий TypeId
2. Если `name` занят другим типом → ошибка
3. Иначе → создаёт новый TypeId, заполняет TypeDescriptor, добавляет в `m_structural`

## TypeRegistry

Глобальный реестр для:

- Регистрации всех встроенных типов в конструкторе (с маппингом на C++ тип)
- Универсальной регистрации пользовательских типов
- Структурного интернирования параметризованных типов (функции, шаблоны, массивы)
- Поиска типа по имени и полному имени
- Разрешения алиасов (Char → Int8, Byte → UInt8, Single → Float32, ...)
- Канонического разрешения: рекурсия по baseType через getCanonicalTypeId()

### Поле preprocInclude

Поле `preprocInclude` в `TypeDescriptor` хранит полную директиву препроцессора (например, `#include <cstdint>`), которая должна быть включена в генерируемый C++ код при использовании данного типа. Для builtin-типов (без `TypeDescriptor`) директива не сохраняется. Для пользовательских и структурных типов может быть передан при регистрации через соответствующий параметр.

Метод доступа: `TypeRegistry::getPreprocInclude(TypeId id)` — возвращает `std::string_view`, пустую строку если директива не задана или тип builtin.

### Регистрация пользовательских типов (алиасы)

`TypeRegistry::registerType()` регистрирует новый алиас на основе существующего `TypeId`:

```cpp
TypeId registerType(std::string_view name, TypeId baseTypeId,
                    std::vector<AttrId> attrs = {},
                    MapperRange sourceRange = {},
                    std::string_view preprocInclude = {});
```

- `name` — каноническое имя нового типа.
- `baseTypeId` — `TypeId` существующего типа, из которого извлекается `TypeKind`.
- `attrs` — атрибуты нового типа.
- `sourceRange` — позиция объявления типа в исходном файле.
- `Context` передаётся при конструировании `TypeRegistry` — ошибки выводятся через `m_ctx->report()`.

Алиас получает тот же `TypeKind`, что и `baseTypeId`, но с ненулевым `registry_index`. 
Метод `getCanonicalTypeId()` рекурсивно раскрывает цепочку алиасов через `baseType`.

Возвращает `TypeId` нового типа или `INVALID_TYPE_ID` при ошибке.

### Структурное интернирование

`TypeRegistry::getOrCreateStructuralType()` создаёт или возвращает существующий параметризованный тип:

```cpp
TypeId getOrCreateStructuralType(std::string_view name, TypeKind kind,
                                  std::vector<TypeId> children,
                                  std::optional<TypeData> data = std::nullopt,
                                  std::string_view preprocInclude = {});
```

Применяется для:
- Сигнатур функций: `getOrCreateStructuralType("(Int32)->Bool", funcKind, {retId, param1Id}, FunctionTypeData{...})`
- Инстанцирования шаблонов: `getOrCreateStructuralType("Vector<Int32>", vecKind, {templateId, int32Id}, TemplateTypeData{...})`
- Массивов: `getOrCreateStructuralType("[4]Int32", arrayKind, {elemId}, ArrayTypeData{...})`

## getCanonicalTypeId

```cpp
TypeId getCanonicalTypeId(TypeId id) const noexcept;
```

Рекурсивно раскрывает цепочку алиасов через `baseType`:

1. Если `isBuiltinTypeId(id)` → сам себе канонический
2. Если `baseType == INVALID_TYPE_ID` → структурный тип/forward decl — сам себе
3. Иначе → `id = baseType`, повтор (рекурсия)

Пример: `A → Byte → Int8` → `getCanonicalTypeId(AId)` = `Int8Id`

## Методы доступа

```cpp
// Универсальный типизированный доступ к TypeData
template<typename T>
const T* getTypeDataAs(TypeId id) const noexcept;

// Проверка на конкретный вариант
bool isTypeDataKind(TypeId id, TypeDataKind kind) const noexcept;
bool isCompleteType(TypeId id) const noexcept;   // data.has_value()
bool isForwardDecl(TypeId id) const noexcept;     // !data.has_value()
```

Пример использования:
```cpp
auto* funcData = registry.getTypeDataAs<FunctionTypeData>(typeId);
if (funcData) {
    // funcData->returnType, funcData->paramTypes, ...
}
```

Раньше был 6 отдельных методов (`getSimpleType`, `getFunctionType`, ...), которые заменены на один шаблонный `getTypeDataAs<T>()`. Это уменьшает дублирование кода и не требует изменений при добавлении новых вариантов TypeData.

Инициализация: конструктор `TypeRegistry(ctx)` однократно при старте — вызывает `registerBuiltinTypes()`.