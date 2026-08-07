# Система типов TrustLang

*TrustLang* имеет закрытую систему типов. Пользовательские типы данных создаются как экземпляры существующих групп, регистрируемые в TypeRegistry.

Система типов построена по иерархическому принципу с трёхуровневой структурой: **Категория → Группа → Конкретный тип**.

- **Категория** — самое общее семейство типов (Arithmetics, Strings, Containers, Callable и т.д.). Категория определяет допустимые операции с типами.
- **Группа** — детализация внутри категории (Integers, Unsigned, Numbers, Function, Closure и т.д.). Принадлежность типов к одной группе допускает возможность автоматического приведения типов (promotion).
- **Конкретный тип** — конкретный тип данных (Int8, Int64, Float64, StrChar, DenseTensor и т.д.).

Типы разделяются на два класса:

- **Встроенные (22 типа)** — закодированы в TypeKind (Data≠0). Integers, Unsigned, Numbers, BFloat, Complex, Rationals, Logical, Void, StrChar, StrWide.
- **Реестровые** — хранятся в TypeRegistry (Data=0). Tensors, Containers, Structured, Callable, Classes, Ranges, Iterators, DateTime, Async, Sync, Exceptions, Native. Конкретные экземпляры создаются при инициализации.

Корень всех типов — `:Any` (Group::kAny, Data=0). Кроме этого есть два служебных типа:

- `:Void` (Group::kVoid, Data=1) — отсутствие типа
- `:None` (Group::kVoid, Data=2) — неинициализированное значение

## Базовая иерархия типов

```
:Any
 ├─ :Void                                            — отсутствие типа
 ├─ :None                                            — неинициализированное значение
 │
 ├─ :Arithmetics                                     — арифметические типы
 │   ├─ :Logical            →  :Bool
 │   ├─ :Integers           →  :Int8 → :Int16 → :Int32 → :Int64
 │   │                        (:Char)
 │   ├─ :Unsigned           →  :UInt8 → :UInt16 → :UInt32 → :UInt64
 │   │                        (:Byte) (:Word) (:DWord) (:DDWord)
 │   ├─ :Numbers            →  :Float16 → :Float32 → :Float64
 │   │                                    (:Single)  (:Double)
 │   ├─ :BFloat             →  :BFloat16
 │   ├─ :Complex            →  :Complex32 → :Complex64
 │   └─ :Rationals          →  :Rational
 │
 ├─ :Strings                                         — строковые типы
 │   ├─ :StrChar             (:FmtChar — printf-формат)
 │   └─ :StrWide             (:FmtWide — printf-формат)
 │
 ├─ :Tensors                                         — многомерные массивы
 │   ├─ :DenseTensor
 │   └─ :SparseTensor
 │
 ├─ :Containers                                      — контейнеры
 │   ├─ :Vector              — динамический массив
 │   ├─ :List                — 
 │   ├─ :Map                 — ассоциативный массив
 │   ├─ :Set                 — множество
 │   ├─ :Deque               — двусторонняя очередь
 │   ├─ :MultiMap            — мульти-отображение
 │   ├─ :MultiSet            — мульти-множество
 │   ├─ :FixedArray          — статический массив фиксированного размера
 │   └─ :Dictionary          — универсальный словарь (:Dict — его алиас)
 │
 ├─ :Structured                                     — структурированные типы
 │   ├─ :Struct             — POD-структура
 │   ├─ :Enum               — перечисление
 │   ├─ :Tuple              — кортеж (времени компиляции)
 │   ├─ :Variant            — типизированное объединение (tagged union)
 │   ├─ :Optional           — опциональное значение
 │   └─ :Expected           — значение или ошибка
 │
 ├─ :Callable                                       — вызываемые типы
 │   ├─ :Function           — указатель на функцию
 │   ├─ :Closure            — функция + захват контекста
 │   ├─ :Coroutine          — сопрограмма
 │   ├─ :Generator          — генератор (coroutine с co_yield)
 │   ├─ :Method             — метод класса
 │   └─ :Delegate           — указатель на метод
 │
 ├─ :Classes                                        — классы и интерфейсы
 │   ├─ :Class              — класс с vtable, виртуальные методы
 │   └─ :Interface          — только методы, без данных
 │
 ├─ :Pointers (RefType)                              — модификатор ссылки
 │
 ├─ :Ranges                                         — диапазоны
 │   ├─ :Range              — числовой диапазон
 │   ├─ :Slice              — срез
 │   └─ :View               — view на данные
 │
 ├─ :Iterators                                      — итераторы
 │   ├─ :Forward
 │   ├─ :Bidirectional
 │   └─ :RandomAccess
 │
 ├─ :DateTime                                       — дата и время
 │   ├─ :Duration
 │   └─ :TimePoint
 │
 ├─ :Async                                          — асинхронные типы
 │   ├─ :Future
 │   └─ :Awaitable
 │
 ├─ :Sync                                           — синхронизация
 │   ├─ :Thread
 │   ├─ :Mutex
 │   └─ :Condition
 │
 └─ :Exceptions                                     — исключения
     ├─ :Exception
     └─ :Error

:Native                                              — нативные (FFI) типы
```

### Категории

| Категория | Назначение |
|-----------|------------|
| Any | Корень всех типов |
| Void | Отсутствие типа |
| Arithmetics | Числовые типы (с фиксированной точностью) |
| Strings | Строковые типы (UTF-8 и wide) |
| Tensors | Многомерные массивы |
| Containers | Контейнеры |
| Structured | Структурированные типы |
| Callable | Вызываемые типы |
| Classes | Классы с vtable и интерфейсы |
| Ranges | Диапазоны и срезы |
| Iterators | Итераторы |
| DateTime | Дата и время |
| Async | Асинхронные типы |
| Sync | Синхронизация |
| Exceptions | Исключения и ошибки |
| Native | Нативные (FFI) типы |

### Группы

Каждая группа принадлежит одной или нескольким категориям. Принадлежность к одной группе допускает автоматическое приведение типов (promotion) внутри группы.

| Группа | Категория(и) | Data встроенных | Описание |
|--------|-------------|-----------------|----------|
| Any | Any | — | Любой тип (корень иерархии, Data=0) |
| Void | Void | 1=Void, 2=None | Отсутствие / неинициализировано |
| Logical | Arithmetics | 1=Bool | Логический тип |
| Integers | Arithmetics | 8,16,32,64 | Целые знаковые |
| Unsigned | Arithmetics | 8,16,32,64 | Целые беззнаковые (FFI) |
| Numbers | Arithmetics | 16,32,64 | Числа с плавающей точкой |
| BFloat | Arithmetics | 16=BFloat16 | BFloat16 |
| Complex | Arithmetics | 32,64 | Комплексные числа |
| Rationals | Arithmetics | 1=Rational | Рациональные числа |
| StrChar | Strings | 1=StrChar | UTF-8 строки |
| StrWide | Strings | 1=StrWide | Широкие строки |
| Dicts | Containers | 1=Dict | Универсальный словарь (гетерогенный контейнер) |
| Tensors | Tensors | — | Тензоры (в реестре) |
| Containers | Containers | — | Контейнеры (в реестре) |
| Structured | Structured | — | Struct, Enum, Variant и др. (в реестре) |
| Callable | Callable | — | Function, Closure и др. (в реестре) |
| Classes | Classes | — | Class, Interface (в реестре) |
| Ranges | Ranges | — | Диапазоны (в реестре) |
| Iterators | Iterators | — | Итераторы (в реестре) |
| DateTime | DateTime | — | Дата и время (в реестре) |
| Async | Async | — | Future, Awaitable (в реестре) |
| Sync | Sync | — | Thread, Mutex (в реестре) |
| Exceptions | Exceptions | — | Exception, Error (в реестре) |
| Native | Native | — | FFI-типы (в реестре) |

Литерал диапазона `start..stop[..step]` (`1..10`, `0..$var..0.01`, `0..100:Rational`) парсится в
AST-узел `RangeExpr`; элементный тип (Int→Int64, Float→Double, Rational при `:Rational`-аннотации,
иначе Any) выводится join'ом типов операндов и параметризует **структурный тип `Range<Elem>`**
(интернируется по Elem, `getOrCreateRangeType`) → рантайм-шаблон `trust::Range<Elem>` (инклюзивная
семантика `a..b`, ленивый итератор, `count/contains/at/toVector/toDict/...`). Переменная диапазона
получает конкретный тип `Range<Int64>`/`Range<Rational>`/… → `trust::Range<...>`. Методы объявлены
один раз с типовым параметром `T` и подставляются (T→Elem) при резолве: `$a.at(0)` возвращает
`Elem`. Абстрактный `:Range` (Group=kRanges, Data=0) в C++ не имеет единого имени → `auto`.

## Алиасы (синонимы)

Алиас — альтернативное имя для существующего типа. Разрешаются через TypeRegistry.

| Алиас | Базовый тип | Назначение |
|-------|-------------|------------|
| Any | kAnyType (Group=Any, Data=0) | Обобщённый тип |
| Char | Int8 | Символ UTF-8 |
| Byte | UInt8 | Беззнаковый байт |
| Word | UInt16 | Беззнаковое слово |
| DWord | UInt32 | Беззнаковое двойное слово |
| DDWord | UInt64 | Беззнаковое четверное слово |
| Single | Float32 | Число с плавающей точкой (4 байта) |
| Double | Float64 | Число с плавающей точкой (8 байт) |
| FmtChar | StrChar | Строка с printf-форматом |
| FmtWide | StrWide | Широкая строка с printf-форматом |
| List | (Vector) | Упорядоченный список |

## Класс жизненного цикла (TypeClass)

Каждый тип (встроенный или реестровый) относится к одному из четырёх классов:

- **Trivial** — нет конструктора/деструктора/виртуальных функций. Допускает memcpy.
- **Relocatable** — есть конструктор и деструктор, нет виртуальных. Допускает relocation.
- **Complex** — полноценный конструктор/деструктор/move.
- **Polymorphic** — есть виртуальная таблица.

## Модификатор ссылки (RefType)

Модификатор, закодированный в TypeKind (биты 16–19), плоский enum. Один признак ссылки на объявление.

| Значение | Имя | Семантика |
|----------|-----|-----------|
| 0 | value | владение значением (без ссылки) |
| 1 | shared | совместное владение (shared_ptr) |
| 2 | weak | слабая (не владеющая) ссылка |
| 3 | unique | исключительное владение (unique_ptr) |
| 4 | ptr | сырой указатель (`*`), только через атрибут |
| 5 | mptr | указатель на член (`::*`) |
| 6 | ref | ссылка (`&`), только через атрибут |
| 7 | rref | rvalue-ссылка (`&&`), только через атрибут |
| 8 | ptrptr | указатель на указатель (`**`) |
| 9 | take | владеющая в рамках текущего скоупа (RAII-охранник, результат take) |

Вид задаётся атрибутом `@[reftype(имя)]` перед переменной/типом. Имя можно указывать **без кавычек**
(рекомендуется — как в Rust `#[repr(C)]`, D `@safe`) или **в строке** `@[reftype("имя")]`:
параметры атрибутов хранятся как текст, обе формы эквивалентны. Подробная модель ссылок, причины
выбора и ограничения — в [REFType.md](REFType.md).

## Битовая структура TypeKind

`TypeKind` — `uint32_t`, упакованные характеристики типа без обращения к реестру:

| Биты | Поле | Бит | Диапазон | Назначение |
|------|------|-----|----------|------------|
| 0–7 | Group | 8 | 0..255 | Идентификатор группы (плоский enum) |
| 8–15 | Data | 8 | 0..255 | Размер/код: 0 = абстрактный (группа), ≠0 = конкретный встроенный тип |
| 16–19 | RefType | 4 | 0..15 | Вид ссылки (плоский enum): value/shared/weak/unique/ptr/mptr/ref/rref/ptrptr |
| 20–21 | TypeClass | 2 | 0..3 | Класс жизненного цикла: Trivial/Relocatable/Complex/Polymorphic |
| 22 | SizeUnit | 1 | 0..1 | Единица Data: 0 = bits, 1 = bytes |
| 23 | BuiltinFlag | 1 | 0..1 | Флаг «встроенный тип» (устанавливает `registerBuiltinType()`) |
| 24–31 | Reserved | 8 | 0..255 | Будущие флаги |

- **Data=0 → абстрактный тип (группа); Data≠0 → конкретный встроенный тип.** Проверка:
  `isBuiltinConcrete(k) == (getData(k) != 0)`.
- **TypeId** (`uint64_t`) = { TypeKind (верхние 32) | registry_index (нижние 32) }. Нижняя половина
  дополнительно несёт ортогональные квалификаторы `kInferredFlag` (bit 31) и `kConstFlag` (bit 30),
  которые **не входят** в структурную идентичность и снимаются `getIndexFromId`/`getCanonicalTypeId`.

## Правила автоматического приведения (promotion)

- **Внутри группы:** автоматически к большему (Int8 → Int16 → Int32 → Int64; Float32 → Float64)
- **Между группами одной категории:** только явный Cast
- **Unsigned ↔ Integers:** только явный Cast
- **Bool + любой арифметический:** ошибка компиляции
- **Rational + любой:** ошибка компиляции
- **Complex + Numbers:** ошибка компиляции
- **Разные категории:** ошибка компиляции
- **Narrowing (с потерей точности):** запрещён, только явный каст

Детали реализации TypeKind (битовая структура, Group, Data, RefType, TypeClass) — в [MEMORY.md](MEMORY.md).
