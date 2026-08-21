# MEMORY.md

> scope: include/types
> role: persistent-memory
> last_reviewed: 2026-08-19
> review_period: 30
> max_size: 55000

# Архитектура реализации системы типов

## Обзор

Система типов построена как комбинация битовой структуры для быстрой идентификации (TypeKind как uint32_t) и реестра для хранения метаданных пользовательских и параметризованных типов.

## TypeKind (uint32_t)

32-битный идентификатор, кодирующий фундаментальные характеристики типа без обращения к реестру.
Полная битовая раскладка (Group/Data/RefType/TypeClass/SizeUnit/BuiltinFlag/Reserved) и layout
`TypeId` - в разделе «Битовая структура TypeKind» документа [TYPE.md](TYPE.md) (единый источник).

Краткий инвариант: **`Data=0 → абстрактный тип (группа); Data≠0 → конкретный встроенный тип`.**
Проверка «является ли тип конкретным встроенным»: `isBuiltinConcrete(k) == (getData(k) != 0)`.

## Group (8 бит)

Плоское перечисление `enum class Group : uint8_t`:

- `kAny = 0` - корень всех типов
- `kVoid = 1` - Void(Data=1) и None(Data=2)
- `kLogical` (2)…`kStrWide` (10) - встроенные группы с Data≠0
- `kDicts` (11) - универсальный словарь (Data=1=Dict)
- `kTensors` (12)…`kNative` (23) - группы для реестра (Data=0)
- `kEllipsis` (24) - группа Ellipsis (произвольное кол-во аргументов)
- `kArithmetics` (25) - арифметические типы (продвижение)
- `kTemplateParam` (26) - тип-параметр шаблона (Data=depth)

## RefType (4 бита)

Плоский enum «вид ссылки» - **один признак ссылки на объявление** (осознанное решение,
см. [REFType.md](REFType.md)):

| Значение | Имя | Семантика |
|----------|-----|-----------|
| 0 | kValue | Полное владение (значение) |
| 1 | kShared | Совместное владение |
| 2 | kWeak | Слабая (не владеющая) ссылка |
| 3 | kUnique | Исключительное владение |
| 4 | kPtr | Сырой указатель (`*`) - только через атрибут |
| 5 | kMptr | Указатель на член (`::*`), MemberPointerTypeData |
| 6 | kRef | Ссылка (`&`) - только через атрибут |
| 7 | kRref | rvalue-ссылка (`&&`) - только через атрибут |
| 8 | kPtrPtr | Указатель на указатель (`**`) |
| 9 | kTake | Владеющая в рамках текущего скоупа (RAII-охранник, результат take) |
| 10–15 | - | Зарезервировано |

Функция `withRefType()` создаёт новый TypeKind с изменённым полем RefType без изменения остальных.
Первая ссылка на тип без признака - fast-path бит `withRefType`; ссылка на уже ссылочный тип
(вложенность) - составной узел `getOrCreateRefType` (`RefTypeData`, группа `kReftype`).
Строковые имена: `refTypeName`/`refTypeFromString` (для атрибута `@[reftype("...")]`).

## Category

Отдельное перечисление `enum class Category : uint8_t` (≤ 32 категорий).
Каждая категория имеет бит в `CategoryMask` (uint32_t).
Группа → категория: `kGroupCategoryMask[group] & (1u << category)`.

## TypeId (uint64_t)

`TypeId` = `uint64_t` - `{ TypeKind (upper 32) | registry_index (lower 32) }`.

> ⚠ trap: Признак «встроенный» - бит `BuiltinFlag` в TypeKind (верхние 32 бита), а **НЕ** `registry_index == 0`: встроенные (машинные) типы регистрируются первыми в `registerBuiltinTypes()` и занимают `registry_index` от 1 до N.
- Пользовательские алиасы и структурные типы регистрируются позже → `registry_index > N`.
- `TypeRegistry::isUserDefinedType(id)` == `index != 0 && index > m_builtinCount` - отделяет
  пользовательские алиасы от машинных и встроенных алиасов. Бит BuiltinFlag для этого НЕ подходит:
  `registerType()` копирует `kind` из `baseTypeId`, поэтому встроенный алиас (`Integer` ← `Int64`)
  и пользовательский (`MyInt` ← `Int32`) оба имеют BuiltinFlag.
- Хелперы: `makeTypeId(kind, index)`, `getKindFromId(id)`, `getIndexFromId(id)`.

### Флаг «тип выведен» (kInferredFlag)

Бит 31 младшей (registry_index) половины `TypeId` зарезервирован под признак «тип ВЫВЕДЕН
автоматически» (из литерала / inferred-переменной) - `kInferredFlag`, хелперы
`withInferred/typeIsInferred/clearInferred` в `types/type_id.hpp`.

- **Структурная идентичность интернируется на 63 битах** (TypeKind в старших 32 + registry_index
  в младших 31); флаг - ортогональный квалификатор, **не часть ключа интернирования**. Один и тот же
  дескриптор общий для `id` и `withInferred(id)`.
- **Все операции идентичности снимают флаг**: `getIndexFromId` (маскирует в одном месте - реестр
  `getTypeData*`/`getCppTypeName`/`isUserDefinedType`/`getTypeSourceRange` автоматически безопасны) и
  `getCanonicalTypeId` (возвращает структурный). Сравнения каноникой (`== getType(...)`, сужение,
  `isAnyType`) не замечают бит.
- `getKindFromId`/`getGroup`/`getData` работают со старшими 32 → флаг им не мешает (не утекает в
  рантайм-`TypedValue.kind`).
- Запрет утечки в идентичность: `getOrCreateStructuralType` выполняет `EXPECT(!typeIsInferred(child))`.
- Признак различает auto-Bool (продвигается в арифметике) и явный Bool (ошибка) - см. `semantic/MEMORY.md`.

### Флаг «константность значения/переменной» (kConstFlag)

Бит 30 младшей (registry_index) половины `TypeId` зарезервирован под признак константности
(неизменяемости) - `kConstFlag`, хелперы `withConst/typeIsConst/clearConst` в `types/type_id.hpp`.

- **Ортогональный квалификатор, не часть ключа интернирования** (как `kInferredFlag`): `const T` и `T`
  разделяют один дескриптор; каноника и индекс снимают бит. Это конвенция top-level const в C++
  (`const int` и `int` - один тип для сигнатуры/манглинга параметра по значению).
- **Учитывается в `getCppTypeName`**: при `typeIsConst(id)` → лидирующий `const ` (для любой категории
  типа: примитив, алиас, структура, класс). `resolveCppTypeId` (транспайлер) добавляет `const ` к
  базовому имени (каноника снимает бит, поэтому префикс добавляется отдельно).
- **Два способа установки** (см. документ `types/CONST.md`):
  1. «константность в типе» - бит ставится на сам тип (декларация `x^ := 42` → `const T`); влияет на
     `getCppTypeName` и прототипы функций;
  2. «пер-переменная константность» - бит ставится на `Symbol::type` по мере анализа узлов AST
     (аналог top-level const / Rust `let`); в структурную идентичность и сигнатуры НЕ входит,
     при кодогенерации выражается через `const_cast<>`, когда тип не константный.
- Запрет утечки в идентичность: `getOrCreateStructuralType` выполняет
  `EXPECT(!typeIsConst(child))` (наравне с inferred).
- Константность параметров функции НЕ входит в структурный тип сигнатуры (`clearConst` перед
  построением FunctionType) - она учитывается только на уровне кодогенерации/прототипа.
- **Структурный кортеж** (`getOrCreateTupleType`, группа `kStructured`, data=1): интернируется по
  `TypeKey{kind, children=типы элементов, names=имена элементов}` - имена входят в идентичность
  (два кортежа с одинаковыми типами, но разными именами полей - разные типы). Элементы (имя, тип)
  хранятся в `TupleTypeData` - источник резолва `t.name`/`t.0`. C++-представление - `auto`/`std::tuple`.

## TypeData - variant для различных категорий типов

Тип может иметь дополнительные данные, хранящиеся в `TypeDescriptor::data` (тип `std::optional<TypeData>`):

`TypeData` - `std::variant` вариантов: `SimpleTypeData{}` (примитив/алиас), `FunctionTypeData{returnType, paramTypes, variadicType}`, `TemplateTypeData{templateTypeId, args}`, `ArrayTypeData{elementType, dimensions, isVariadic}`, `MemberPointerTypeData{classType, memberType}`, `RefTypeData{pointeeType}`, `PackExpansionTypeData{pattern}`, `TupleTypeData{elements(name/type)}`. Полный состав полей - в `types/type_data.hpp`.

- `data = nullopt` → forward declaration (неполный тип)
- `data = SimpleTypeData{}` → примитив или алиас
- Для встроенных типов TypeDescriptor создаётся (в `registerBuiltinType`) с `data = SimpleTypeData{}`.

## TypeDescriptor

Хранит метаданные, НЕ закодированные в битовой структуре TypeKind:

`TypeDescriptor` хранит метаданные, не закодированные в битовой структуре `TypeKind`: `name` (canonical, владеющая копия), `attrs`, `sourceRange`, `cppName`, `preprocIncludes` (первый - основной заголовок), `baseType` (алиас-цепочка), `data` (`std::optional<TypeData>`). Полный состав полей - в `registry.hpp`.

## Роль SimpleTypeData

`SimpleTypeData` - это пустая структура-маркер, необходимая для различения состояний `std::optional<TypeData>`:

| Значение `data` | Семантика |
|----------------|-----------|
| `std::nullopt` | Forward declaration (тип объявлен, но не определён) |
| `SimpleTypeData{}` | Тип полностью определён, структурных данных нет |
| `FunctionTypeData{...}` | Функциональный тип |
| `TemplateTypeData{...}` | Инстанцированный шаблон |

Без `SimpleTypeData` было бы невозможно отличить "fully defined simple type" от "forward declaration" - пришлось бы вводить отдельный флаг `isComplete`.

## Type names (type_names.hpp)

Константы имён типов разделены на три namespace в одном файле:

- `trust::type` - конкретные встроенные типы (Data ≠ 0): Void, Bool, Int8..Int64, Float32, StrChar, Ellipsis и т.д.
- `trust::type_generic` - обобщённые типы (Any + group-aliases): Any, Integers, Numbers, Strings, Tensors
- `trust::type_category` - абстрактные категории (Data = 0): Struct, Function, Class, Range, Exception и т.д.

Использование: `type::Int32`, `type_generic::Any`, `type_category::Function`.

Ellipsis-константы (`EllipsisAny`, `EllipsisTyped`) отнесены к `type_category`, так как они являются маркерами для variadic параметров, а не конкретными типами данных.

- `cppName` - для простых типов заполняется при регистрации в `registerBuiltinType()`, для пользовательских - при анализе AST
- `baseType` - для алиасов: `type Byte = Int8` → `baseType = Int8Id`
- `data` - `nullopt` для forward declaration, иначе один из вариантов TypeData

## TypeKey - структурное интернирование (FoldingSet)

Для параметризованных типов (функции, шаблоны, массивы) требуется, чтобы два одинаковых по структуре типа давали один и тот же TypeId:

`TypeKey{kind, children}` - ключ структурного интернирования (FoldingSet): два одинаковых по структуре типа дают один TypeId.

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

### Поле preprocIncludes

Поле `preprocIncludes` в `TypeDescriptor` хранит **список** директив препроцессора (например, `#include <cstdint>`), которые должны быть включены в генерируемый C++ код при использовании данного типа. Первый элемент - основной заголовок типа (определяет C++-имя); остальные - файлы, которые он транзитивно требует. Это **единый список без понятия «зависимостей»**. Для builtin-типов (без `TypeDescriptor`) список пуст. Для пользовательских и структурных типов может быть передан при регистрации через соответствующий параметр (обычно пуст).

Методы доступа:
- `TypeRegistry::getPreprocInclude(TypeId id)` - возвращает первый (основной) заголовок, пустую строку если не задан.
- `TypeRegistry::getPreprocIncludes(TypeId id)` - возвращает полный список директив.

#### Маркер `@` - рантайм-заголовки

Для типов, реализация которых живёт в рантайм-библиотеке (`trust-runtime.so`), элемент списка начинается с `@`, за которым следует путь заголовка, например `"@trust/rational.hpp"`:

- Транслятор (`CppTranspiler::recordRequiredInclude`) срезает `@`, запоминает путь в `m_runtimeHeaders` (только реально использованные) и пишет в сгенерированный код настоящую директиву `#include "<path>"`.
- Путь заголовка совпадает с именем ELF-секции внутри `trust-runtime.so`, где лежит его содержимое (встроено через `#embed`).
- Pipeline извлекает эти секции из `.so` (как ELF) во временный каталог `<build_dir>/trust/` и добавляет в `build.conf` include-путь и линковку рантайм-библиотеки - только для тех заголовков, что действительно использовались программой.

Обычные (не рантайм) типы хранят полные директивы (`#include <cstdint>`) и обрабатываются без `@`.

Пример рантайм-типа по этому механизму - универсальный словарь `:Dict` (`Group::kDicts`,
`cppName="trust::Dict"`, `preprocIncludes={"@trust/dict.hpp", "@trust/rational.hpp"}` - словарь
транзитивно требует `rational.hpp`, так как `dict.hpp` его включает). `:Dictionary` - встроенный
алиас на `Dict` (канонизация через `getCanonicalTypeId` возвращает `Dict`). Литерал словаря
`(1, two=2, name='3',)` парсится в AST-узел `DictLiteral` (элементы: безымянные значения и
`name=value` как `AssignOp`); транспилятор (`visit_DictLiteral`) эмитит
`trust::Dict{ {"", expr}, {"two", expr}, ... }`.
Рантайм-заголовок объявляет `std::formatter<trust::TypedValue>` (печать одиночного элемента)
и `std::formatter<trust::Dict>` - печать целого словаря через `print(d)` как
`(имя=значение, ...)` (через `dictToString`).

Диапазон - параметризованный структурный тип `Range<Elem>` (`Group::kRanges`, Data=1,
`RangeTypeData{elementType}`, интернируется по elementType через `getOrCreateRangeType`, как
Tuple): литерал `start..stop[..step]` парсится в AST-узел `RangeExpr` (операнды
`start/stop/step`), анализатор вычисляет элементный тип `RangeExpr::elementType` join'ом типов
операндов (Int→Int64, любой float→Double, Rational→Rational при `:Rational`-аннотации операнда,
иначе Any) и ставит типом выражения `getOrCreateRangeType(elementType)`. Переменная `r := 1..10`
получает тип `Range<Int64>` → `resolveCppTypeId` эмитит конкретный `trust::Range<int64_t>` (в
отличие от абстрактного `:Range`, Data=0, который → `auto`). Рантайм-заголовок `@trust/range.hpp`
+ транзитивные `@trust/dict.hpp`/`@trust/rational.hpp`.

Методы Range объявлены ОДИН раз на абстрактном `:Range` в `registerBuiltinTypes` (C++-модель
шаблонов): элемент-зависимые слоты помечены типовым параметром `T` (`Group::kTemplateParam`) -
`start/stop/step → T`, `at(Int64) → T`, `contains(T) → Bool`; неэлементные - конкретными типами
(`count/size → Int64`, `empty → Bool`, `toDict → Dict`), контейнер-возвращающие (`toVector/
toArray/toList/reversed`) - `Any`. При резолве для конкретного `Range<Elem>` `findMethodInfo`
(через fallback к абстрактному `:Range`) находит сигнатуру, а `handleMethodCall`/registry
`instantiateRangeMethod` подставляет `T→Elem` (через интернированный `getOrCreateFunctionType`) -
`$a.at(0)` возвращает `Elem` (Int64/Rational), а не типовой параметр/Any.

Методы хранятся как **полный ключ-имя → интернированный функциональный тип** (`map<string, TypeId>`
в `TypeDescriptor::methods`); ключ кодирует нативность (ведущий `%`) и константность (хвостовой
`^`), напр. `"%count^"`. Нативность/константность ВЫВОДЯТСЯ из ключа (`utils::bare_name`/
`utils::is_native_name`/`utils::is_const_name` в `utils/strings.hpp`, `%`-срез - единый
`utils::strip_native_prefix`) - отдельные поля (`TypeMethod{nativeName,isConst}`) не хранятся.
Алиас - другое имя существующего метода (`TypeDescriptor::methodAliases`: bare-имя → ключ цели);
регистрируется единым `addMethod(type, name, funcType, aliases={})`, который EXPECT'ом проверяет,
что каждый алиас полностью повторяет семантику цели (нативность и константность совпадают);
`findMethodInfo` для алиаса возвращает ключ ЦЕЛИ - нативное имя берётся из него. const и не-const
перегрузки с одинаковыми аргументами - разные записи (ключи `%get^`/`%get`).

Формы вызова метода (все резолвятся к одному нативному члену; `%`-префикс - только хранение/
экспорт, нативный вызов работает):
- `obj.size()`, `obj.length()` (алиас → нативное `count`) - обычный вызов `(obj).size()`/`(obj).count()`
  (для const-only метода корректно и на не-const объекте);
- `obj.%size()`, `obj.%length()` - нативная форма, та же генерация;
- `obj.size^()`, `obj.%length^()` - const-вызов: `^` на калле - валидный immutable-квалификатор
  (`canHaveImmutableQualifier(CallExpr)=true`), `attr::ReadOnly` на ВЫЗОВЕ (ставит `convertAttrsToNode`);
  кодогенерация эмитит `const_cast<const T&>(obj).method(...)` - гарантированно константная
  перегрузка. Нативное имя в кодгене - из `findMethodInfo` по ключу (алиас → ключ цели); тип `T` -
  из `n.lhsType` (тип объекта, сохранён семантикой; кодген не может восстановить локальную
  переменную - скоуп-стек сброшен).


#### Реестр рантайм-символов

Механизм `@` покрывает рантайм-типы, но часть рантайм-функций не привязана к типу
(например `trust::formatMessage`, `trust::trust__abort__` из `trust/assert.hpp`).
Для них в `TypeRegistry` есть реестр рантайм-символов:

`TypeRegistry::registerRuntimeSymbol(RuntimeSymbolId id)` / `runtimeSymbols()` - реестр рантайм-символов, не привязанных к типу (`RuntimeSymbol{symbol, runtimeHeaders}`).

**Единый источник описания - `types/runtime_symbols.hpp`** (X-macro): имена и заголовки всех
рантайм-символов собраны в одном списке `TRUST_RUNTIME_SYMBOLS`, из которого автоматически
генерируются `RuntimeSymbolId`, массивы заголовков и функции доступа
(`constexpr runtimeSymbolName(id)` / `runtimeSymbolHeaders(id)`). `registerBuiltinTypes()`
регистрирует символы циклом по `RuntimeSymbolId::kCount` - без разрозненных строковых литералов.
Типизированный идентификатор исключает опечатку в имени символа на этапе компиляции (строковые call-сайты
`recordRuntimeSymbolHeaders("trust::any_to")` при опечатке молча пропускали бы инклуд).

**Инвариант: символ НЕ должен дублировать тип.** Рантайм-символы - ТОЛЬКО не-типовые функции.
Типы, чья реализация живёт в trust-runtime (Dict, Rational), регистрируются через `registerBuiltinType`:
их заголовки подключаются по-типу (механизм №1), добавлять их как символы запрещено.
`TypeRegistry::registerRuntimeSymbol` содержит EXPECT-проверку «ни один зарегистрированный тип не имеет
того же C++-имени» - попытка зарегистрировать `trust::Dict` как символ - явная ошибка при
инициализации реестра (в т.ч. в тестах). Сырая C++-вставка `{% trust::Dict d; %}` без типизированной
ссылки на Dict НЕ тянет заголовок - это ожидаемое поведение. Все списки заголовков - «просто список
файлов», без понятия «зависимостей».

Транспилятор разделяет подключение инклудов на **два независимых механизма** (без сканирования
выходного буфера):

**Механизм №1 - по типу (TypeRegistry).** Во время обхода AST собираются **только использованные
типы** (канонические `TypeId`) в `CppTranspiler::m_usedTypes` (`recordUsedType` вызывается из
`resolveCppTypeId`, т.е. любым `emitTypeName`/`emitTypeNameForNode`). Сами директивы инклудов из
этих типов формируются **ПОСЛЕ полного обхода AST**: `collectTypeIncludes` проходит `m_usedTypes`
и для каждого типа записывает весь `getPreprocIncludes`. Это покрывает типы Dict/Rational/целые:
`DictLiteral`, элементы словаря, типизированные переменные/параметры/касты автоматически попадают
в набор по своему типу.

**Механизм №2 - по рантайм-символу (RuntimeSymbol).** Используется там, где типа нет: в EMBED-узлах
(`{% %}`) - только текст, и поиск по тексту/имени является единственным способом определить
необходимые заголовки. ЕДИНСТВЕННЫЙ способ записи заголовков - `recordRuntimeSymbolHeaders(RuntimeSymbolId)`
(типизированный id, опечатка в имени невозможна); строковой перегрузки записи нет:
- `emitTypedConstruction` (для `trust::checked_cast`/`trust::any_to`) - напрямую `recordRuntimeSymbolHeaders(RuntimeSymbolId::kAnyTo)` /
  `(RuntimeSymbolId::kCheckedCast)`;
- `visit_CallExpr` - `findRuntimeSymbolByName(callee)` (точное совпадение с таблицей, ведущий `%` срезается)
  → найденный id передаётся в `recordRuntimeSymbolHeaders`;
- `visit_EmbedExpr` - отдельный хелпер `recordRuntimeSymbolsInText(text)` (substring-скан текста вставки
  на имена символов), который внутри вызывает `recordRuntimeSymbolHeaders(id)`.
Все пути берут заголовки из единой таблицы `types/runtime_symbols.hpp`. Список заголовков - **полное
транзитивное замыкание** (нужно для co-извлечения, см. pipeline `extractRuntimeHeader`).

В конце генерации `collectTypeIncludes()` + `emitCollectedIncludes` препендят все собранные директивы
в начало файла. Это позволяет линковать `trust-runtime` только для программ, реально использующих
такие типы/символы (no-assert → рантайм не линкуется).

### Регистрация пользовательских типов (алиасы)

`TypeRegistry::registerType()` регистрирует новый алиас на основе существующего `TypeId`:

`TypeId registerType(name, baseTypeId, attrs, sourceRange, preprocInclude)` - регистрирует алиас на основе существующего `TypeId`.

- `name` - каноническое имя нового типа.
- `baseTypeId` - `TypeId` существующего типа, из которого извлекается `TypeKind`.
- `attrs` - атрибуты нового типа.
- `sourceRange` - позиция объявления типа в исходном файле.
- `Context` передаётся при конструировании `TypeRegistry` (по ссылке `Context&`) - ошибки выводятся через `m_ctx.report()`.

Алиас получает тот же `TypeKind`, что и `baseTypeId`, но с ненулевым `registry_index`. 
Метод `getCanonicalTypeId()` рекурсивно раскрывает цепочку алиасов через `baseType`.

Возвращает `TypeId` нового типа или `INVALID_TYPE_ID` при ошибке.

### Структурное интернирование

`TypeRegistry::getOrCreateStructuralType()` создаёт или возвращает существующий параметризованный тип:

`TypeId getOrCreateStructuralType(name, kind, children, data, preprocInclude)` - создаёт или возвращает существующий параметризованный тип.

Применяется для:
- Сигнатур функций: `getOrCreateStructuralType("(Int32)->Bool", funcKind, {retId, param1Id}, FunctionTypeData{...})`
- Инстанцирования шаблонов: `getOrCreateStructuralType("Vector<Int32>", vecKind, {templateId, int32Id}, TemplateTypeData{...})`
- Массивов: `getOrCreateStructuralType("[4]Int32", arrayKind, {elemId}, ArrayTypeData{...})`

> ⚠ trap: Структурные типы с **пустым именем** (функциональные сигнатуры, `getOrCreateFunctionType(return, params)`) **НЕ регистрируются в `m_name_to_id`** - только по `TypeKey` в `m_structural`. Иначе разные сигнатуры давали бы «duplicate type name ''». Именованные структурные типы (`Vector<Int32>`, массивы) - попадают в `m_name_to_id`.

## Владение именами в m_name_to_id

> ⚠ trap: `m_name_to_id` и `TypeDescriptor::name` хранят **владеющие `std::string`**, а не `std::string_view`. Иначе имя из временной строки (напр. `std::string(left->text())` в семантике, `type_name` в `analyzeTypeDecl`) даёт висячий ключ: `findType`/`getFullTypeName(alias)` возвращали мусор. Поиск по `string_view` копирует (не горячий путь).

## Сброс реестра (reset)

`TypeRegistry::reset()` очищает `m_name_to_id`, `m_descriptors` и `m_structural` и повторно
регистрирует builtin-типы (`registerBuiltinTypes()`). Вызывается на каждый запуск семантики
(`SemanticPassRunner::run()`), чтобы пользовательские алиасы и структурные (в т.ч. функциональные)
типы не накапливались между run(): SymbolTable пер-ран, типы согласованы с ней.

## getCanonicalTypeId

`TypeId getCanonicalTypeId(TypeId id)` - рекурсивно раскрывает цепочку алиасов через `baseType`.

Рекурсивно раскрывает цепочку алиасов через `baseType`:

1. Если `isBuiltinTypeId(id)` → сам себе канонический
2. Если `baseType == INVALID_TYPE_ID` → структурный тип/forward decl - сам себе
3. Иначе → `id = baseType`, повтор (рекурсия)

Пример: `A → Byte → Int8` → `getCanonicalTypeId(AId)` = `Int8Id`

## Продвижение арифметических типов (promotion.hpp)

`types/promotion.hpp` - единый TypeId-aware источник правил продвижения числовых типов
для анализатора выражений (вызывается из `semantic/type_inference.hpp::resultTypeBinary`).
Работает с `TypeId`: канонизирует цепочки алиасов через `TypeRegistry::getCanonicalTypeId`
и учитывает группу и разрядность (`getData`):

- `floatTypeForData(reg, data)` - float-тип по разрядности (16/32/64).
- `promoteSingleNumeric(reg, id)` - продвижение одиночного конкретного числового операнда
  (для `std::any_cast`): малые целые → Int32, 64-бит → Int64; float → сам;
  беззнаковые → UInt32/UInt64.
- `commonArithmeticType(reg, lhs, rhs)` - общий арифметический тип двух операндов
  (usual arithmetic conversions C++): float-операнд → более широкая float-группа;
  иначе при 64-битном операнде → Int64, иначе → Int32.
  **Signedness:** результат для целых **не зависит** от знаковости операндов - `UInt8 + UInt8 → Int32`
  (по C++ integer promotion), беззнаковый результат `UInt32/UInt64` даёт только
  `promoteSingleNumeric` (одиночный операнд для `std::any_cast`). Это намеренно и согласовано
  с кодогенерацией; не путать с «беззнаковые → UInt*» из TYPE.md (там речь про single-промоцию).
  > ⚠ trap: «UInt32/UInt64» от беззнаковых - только в `promoteSingleNumeric`, а `commonArithmeticType`
  всегда даёт `Int32`/`Int64`; не переносить беззнаковое поведение из single-промоции в бинарную.

Операторная семантика (Compare/Logical → Bool, `//`/`//=` → Int64, std::any-операнды)
остаётся в `semantic/type_inference.hpp`; сюда уходит только числовая часть. Промоушен
реализован напрямую, без TypeKind-only API (`getPromotion`/`promoteInteger`/`promoteFloat`/
`isPromotableTo`) и без `types/type_traits.hpp`.

## Диапазоны целых литералов (int_literal.hpp)

`types/int_literal.hpp` - единый источник границ целых и соответствия ширина↔тип для литералов
(вынесен из `semantic/type_inference.hpp`; TypeRegistry хранит ширину `data`, но не границы и не
«тип по ширине»):

- `fitsIntegerValue(group, width, value)` - влезает ли беззнаковое значение в целый тип;
- `intTypeForWidth(reg, width)` - знаковый Int по ширине (default → Int64);
- `intTypeForLiteral(reg, value)` - минимальный вмещающий знаковый Int.

Используется `literalType` (выбор типа литерала) и сужением `intFitsTarget`/`checkAssignmentNarrowing`.

## Единые предикаты групп и any-типа

Два общих предиката устраняют дублирование классификации в семантике и транспиляторе:

- `isArithmeticGroup(Group)` (`types/group.hpp`) - истина для `kIntegers | kUnsigned | kNumbers`.
  Используется в `semantic/type_inference.hpp::resultTypeBinary` для выделения числовых операндов.
- `isAnyType(TypeId, const TypeRegistry&)` (`types/registry.hpp`) - истина, если тип после канонизации
  алиасов относится к `Group::kAny` (std::any). Единый источник проверки «any-операнд» для
  `resultTypeBinary` (lAny/rAny), `NameResolutionPass` и `CppTranspiler::emitBinaryOperand`
  (решение об `std::any_cast`).

## Методы доступа

`getTypeDataAs<T>(id)` - типизированный доступ к варианту `TypeData`; `isTypeDataKind(id, kind)` - проверка варианта; `isCompleteType(id)` = `data.has_value()`, `isForwardDecl(id)` = `!data.has_value()`.

Единый шаблонный метод `getTypeDataAs<T>()` вместо набора индивидуальных методов (`getSimpleType`, `getFunctionType`, ...) - уменьшает дублирование кода и не требует изменений при добавлении новых вариантов TypeData.

Инициализация: конструктор `TypeRegistry(ctx)` однократно при старте - вызывает `registerBuiltinTypes()`.
## Общее иммутабельное ядро встроенных типов (shared builtin core)

Встроенные типы (и их методы) одинаковы для всех экземпляров `TypeRegistry`, поэтому они
**не пересобираются на каждый экземпляр**, а строятся один раз в `TypeRegistry::builtinCore()`
(thread-safe magic static, seed-конструктор `TypeRegistry(diag, opts, BuiltinSeedTag)`) и
хранятся в иммутабельном `BuiltinTypeCore` (дескрипторы + `name_to_id` + рантайм-символы).
Каждый экземпляр `TypeRegistry` ссылается на ядро (`m_builtin`) и хранит **только**
пользовательские типы (`m_descriptors` = индексы `> m_builtinCount`).

- Роутинг дескриптора: `descriptorOf(id)`/`userDescriptorOf(id)` - встроенный (`index <= builtinCount`)
  → из ядра (только const), пользовательский → из `m_descriptors`.
- `addMethod` - только на пользовательских типах (`userDescriptorOf`, встроенный → EXPECT).
- `registerType`/`getOrCreateStructuralType` получают `registry_index = m_builtinCount + size + 1`
  и проверяют коллизии с ядром; `findType`/`forEachType` ищут и в ядре, и в пользовательских.
- Встроенные типы иммутабельны и разделяются - это устраняет дублирование (важно для LSP,
  где реестр хранится на файл). `Pipeline::releaseTypes()` отдаёт владение реестром вызывающему.


## Enum-типы (Group::kEnums, EnumTypeData)

Типобезопасное перечисление `Color ::= :Enum(RED=1, GREEN=2,)` / `(RED=1, GREEN=2,):Enum`
(как Tuple: TypeDecl(left=Ident, right=DictLiteral с аннотацией типа «Enum»); грамматика `set`
`<...>` удалена). Регистрируется `TypeRegistry::registerEnumType` (Group::kEnums,
`EnumTypeData{ valueType, members }`, НЕ алиас: baseType=INVALID, canonical = сам тип).
Предикат `isEnumType` - единый источник проверки для семантики и кодогенерации.

- **Тип значений** (`Color.Value`, вложенный алиас) - ЕДИНЫЙ для всех членов, выводится ПО ОБЩИМ
  ПРАВИЛАМ (как для коллекций): все значения одного типа → этот тип (Bool/Int32/Rational/StrChar/...);
  разные → JOIN (`joinElementTypes`: Bool+Int→Int64, float→Double); несовместимое (Int+Str и т.п.)
  → Any с **предупреждением `OptKind::WidenAny`** (`-Wwiden-any=error|warning|ignore`, default Warning).
  Ординальные (бесзначённые) члены → минимальный знаковый Int по максимальному ординалу (N-1).
- **Осознанное решение (типобезопасность):** члены enum не имеют методов (`x.name()` и т.п.).
  Вся работа идёт ТОЛЬКО через имя типа: `Color.RED` (член), тип-уровневые методы
  (`Color.count()`, `fromName`, `fromValue` - регистрируются через `addMethod`). Сравнение
  `<,>,<=,>=,==,!=` типобезопасно (только однотипные enum, проверка в `typeBinaryResult`) и идёт
  **ПО ЗНАЧЕНИЮ члена** (value), а НЕ по позиции (ordinal): позиция и значение - разные понятия,
  при произвольных значениях (RED=10, GREEN=2) порядок по значению и по позиции расходятся;
  алиасы с равными значениями сравниваются `==` (как IntEnum).
  > ⚠ trap: сравнение enum - по `value`, не по `ordinal`; порядок члена по значению и по позиции
  может расходиться при нестандартных значениях.
- **Рантайм-представление**: generic-логика - в рантайм-шаблоне `trust::Enum<ValueT,N>`
  (include/trust/enum.hpp: Value alias, value/ordinal, constexpr-конструкторы, операторы сравнения
  по значению, count()); кодогенерация эмитит `struct c_Color : trust::Enum<Value,N>` + `static const`
  члены + out-of-class определения (`static constexpr` собственного типа невозможен - неполный тип).
- Классические методы `count`/`fromName`/`fromValue` регистрируются в `analyzeEnumDecl`.

## Variant-типы (Group::kVariants, VariantTypeData)

Гетерогенный вариант `Value ::= :Variant(RED:Int64=0, GREEN='g',)` / `(RED=5, GREEN='g',):Variant`
(как Tuple: TypeDecl(left=Ident, right=DictLiteral с аннотацией «Variant»)). В отличие от Enum
(единый тип значений), **каждый член имеет СВОЙ тип** (`VariantMemberData{name, type}`); тип члена
выводится из его значения (resolvedType), ординальный (без значения) → минимальный знаковый Int
по позиции. Регистрируется `registerVariantType` (Group::kVariants, НЕ алиас). Предикат
`isVariantType` - единый источник для семантики и кодогенерации.

- **Доступ**: `Value.RED` → тип ЭТОГО члена (гетерогенный); `Value.count()` → Int64. Работа идёт
  только через имя типа (как и для enum).
- **Рантайм-представление**: `struct c_Value { using Variant = std::variant<...>; static const
  <T> c_MEMBER; ... }` + out-of-class определения членов (значения из DictLiteral RHS; StrChar
  значение → `"..."`); `#include <variant>`. Члены - константы своих типов.

## Array-типы (Group::kContainers, ArrayTypeData)

Универсальный изменяемый массив `:Array` (абстрактный, Data=0, cpp `std::vector<Elem>`) +
структурный `Array<Elem>` (`getOrCreateArrayType`, Data=1, `ArrayTypeData{elementType,
dimensions}`). Интернируется по (elementType, dims) - dims кодируются в `TypeKey::names`
(дети - только типы). **Константность (`:Array^` → std::array) НЕ хранится в данных типа** - это
kConstFlag-бит в TypeId (`withConst`), как у любых типов: mutable (std::vector) - без бита,
`getCanonicalTypeId` снимает бит. Методы объявлены ОДИН раз на абстрактном `:Array` (ключ с '%'/'^',
типовой параметр T) и подставляются (T→Elem) в `instantiateArrayMethod` (общий
`substituteElementParam` с Range). Определения типов `:Elem[N]`/`:Elem[N,M]` регистрируют
Array<Elem,dims> (N-D). Многомерные (`dims.size()>1` или элемент-массив) - предикат
`isMultiDimArray`; кодогенерация тензора не реализована («не реализовано»). Тип элемента
литерала/конструкции - узкая разрядность (`arrayElementJoin`): `[1,2,3,]` → Int8,
`[100,300,]` → Int16.


