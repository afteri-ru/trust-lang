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
 │   └─ :FixedArray          — статический массив фиксированного размера
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
 ├─ :Pointers (RefKind)                              — модификатор ссылки
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

## Модификатор ссылки (RefKind)

Модификатор, закодированный в TypeKind (биты 16–17):

- **None** — не ссылка
- **Raw** — голый указатель (C-style, FFI)
- **Shared** — разделяемое владение (shared_ptr)
- **Unique** — уникальное владение (unique_ptr)

## Правила автоматического приведения (promotion)

- **Внутри группы:** автоматически к большему (Int8 → Int16 → Int32 → Int64; Float32 → Float64)
- **Между группами одной категории:** только явный Cast
- **Unsigned ↔ Integers:** только явный Cast
- **Bool + любой арифметический:** ошибка компиляции
- **Rational + любой:** ошибка компиляции
- **Complex + Numbers:** ошибка компиляции
- **Разные категории:** ошибка компиляции
- **Narrowing (с потерей точности):** запрещён, только явный каст

Детали реализации TypeKind (битовая структура, Group, Data, RefKind, TypeClass) — в [ARCH.md](ARCH.md).
