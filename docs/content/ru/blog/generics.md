---
title: Обобщённое программирование в разных языках
slug: generics
date: 2026-03-24
---

Обобщённое программирование (generic programming) — парадигма, позволяющая описывать алгоритмы и структуры данных в абстракции от конкретных типов. Несмотря на единую концепцию, каждый язык реализует её по-своему: через шаблоны, параметрический полиморфизм, стирание типов или утиную типизацию. В данной статье проводится детальный анализ механизмов обобщённого программирования в восьми языках — C++, Java, C#, Python, Rust, Haskell, TypeScript, Julia — с оценкой сильных и слабых сторон каждого подхода и сравнительным итогом.

---

## 1. Введение

Идея обобщённого программирования восходит к работам Александра Степанова и Дэвида Мусера в конце 1980-х годов. Её суть — отделение алгоритмов от конкретных типов данных при сохранении типовой безопасности и управляемой производительности. На практике разные языки решают эту задачу через разные механизмы:

| Подход | Представители |
|---|---|
| Шаблоны (compile-time code generation) | C++, Rust |
| Стирание типов (type erasure) | Java, TypeScript |
| Реификация (reified generics) | C# |
| Параметрический полиморфизм + типклассы | Haskell |
| Аннотации + структурная типизация | Python |
| Multiple dispatch + JIT | Julia |

Каждый механизм рассматривается по следующим критериям: модель реализации, система ограничений на типы, типовая безопасность, производительность, выразительность, ограничения и известные проблемы.

---

## 2. C++ — Шаблоны и концепты

### 2.1 Модель реализации

C++ реализует обобщённое программирование через шаблоны (templates) — механизм метапрограммирования времени компиляции. Шаблон представляет собой инструкцию компилятору по генерации кода для каждого конкретного набора аргументов. Этот механизм называется мономорфизацией: для каждой инстанциации шаблона генерируется отдельная копия кода.

```cpp
// Шаблон функции
template<typename T>
T max_value(T a, T b) {
    return (a > b) ? a : b;
}

// Шаблон класса с параметром по умолчанию
template<typename T, typename Allocator = std::allocator<T>>
class Stack {
    std::vector<T, Allocator> data_;
public:
    void push(T value) { data_.push_back(std::move(value)); }

    T pop() {
        T val = std::move(data_.back());
        data_.pop_back();
        return val;
    }

    bool empty() const { return data_.empty(); }
};

// Полная специализация шаблона для bool
template<>
class Stack<bool> {
    std::vector<uint8_t> data_; // побитовое хранение
    // ...
};
```

### 2.2 Концепты (C++20)

До C++20 ограничения на типы выражались через SFINAE (Substitution Failure Is Not An Error) и `std::enable_if`. C++20 ввёл концепты — именованные предикаты над типами, проверяемые на этапе компиляции.

```cpp
#include <concepts>
#include <numeric>

// Определение концепта
template<typename T>
concept Numeric = std::integral<T> || std::floating_point<T>;

template<typename T>
concept Sortable = requires(T container) {
    { container.begin() } -> std::input_iterator;
    { container.end() }   -> std::input_iterator;
    requires std::totally_ordered<typename T::value_type>;
};

// Использование концепта
template<Numeric T>
T sum(const std::vector<T>& v) {
    return std::accumulate(v.begin(), v.end(), T{});
}

// Составные ограничения
template<typename T>
requires Sortable<T> && (!std::same_as<typename T::value_type, bool>)
void sort_container(T& container) {
    std::sort(container.begin(), container.end());
}

// Сокращённый синтаксис (abbreviated function template)
void print_numeric(Numeric auto value) {
    std::cout << value << '\n';
}
```

### 2.3 Шаблонное метапрограммирование

C++ предоставляет механизм вычислений на этапе компиляции:

```cpp
// Вычисление числа Фибоначчи на этапе компиляции
template<int N>
struct Fibonacci {
    static constexpr int value =
        Fibonacci<N-1>::value + Fibonacci<N-2>::value;
};

template<> struct Fibonacci<0> { static constexpr int value = 0; };
template<> struct Fibonacci<1> { static constexpr int value = 1; };

// Variadic templates + fold expressions (C++17)
template<typename... Args>
auto sum_all(Args&&... args) {
    return (... + args);
}

// Ветвление на уровне типов (if constexpr, C++17)
template<typename T>
void process(T value) {
    if constexpr (std::is_integral_v<T>) {
        // ветвь для целых чисел
    } else if constexpr (std::is_floating_point_v<T>) {
        // ветвь для чисел с плавающей точкой
    }
}
```

### 2.4 Преимущества и недостатки

**Преимущества:**
- Нулевые накладные расходы в рантайме — весь полиморфизм разрешается на этапе компиляции.
- Шаблоны Тьюринг-полны на этапе компиляции, что открывает широкие возможности метапрограммирования.
- Частичные и полные специализации позволяют предоставлять оптимизированные реализации для конкретных типов.
- Концепты (C++20) делают ошибки компиляции читаемыми, а ограничения — частью документации.
- Стандартная библиотека (STL) построена на шаблонах и является эталонной реализацией обобщённого программирования.

**Недостатки:**
- Раздувание бинарного файла (code bloat): каждая инстанциация генерирует отдельный машинный код.
- Медленная компиляция: шаблонный код компилируется в каждой единице трансляции заново.
- Реализация шаблонов обязана находиться в заголовочных файлах, что нарушает инкапсуляцию.
- До C++20 диагностика ошибок была крайне нечитаемой; с концептами ситуация улучшилась, но не стала тривиальной.
- SFINAE и частичные специализации остаются сложными даже для опытных разработчиков.

---

## 3. Java — Дженерики и стирание типов

### 3.1 Модель реализации

Java добавила дженерики в версии 1.5 (2004). Ключевое проектное решение — стирание типов (type erasure): параметры типов существуют только во время компиляции и полностью удаляются из байткода. Все инстанциации `List<String>` и `List<Integer>` в скомпилированном коде превращаются в `List` (raw type), а параметры типов заменяются их верхними ограничениями (обычно `Object`).

```java
// Обобщённый класс
public class Pair<A, B> {
    private final A first;
    private final B second;

    public Pair(A first, B second) {
        this.first = first;
        this.second = second;
    }

    public A getFirst()  { return first; }
    public B getSecond() { return second; }

    public static <X, Y> Pair<X, Y> of(X x, Y y) {
        return new Pair<>(x, y);
    }
}

// Ограниченный параметр типа
public class SortedList<T extends Comparable<T>> {
    private final List<T> items = new ArrayList<>();

    public void add(T item) { items.add(item); }

    public T min() {
        return items.stream()
                    .min(Comparator.naturalOrder())
                    .orElseThrow();
    }
}
```

### 3.2 Wildcards и вариантность

Java использует use-site variance через wildcards:

```java
// ? extends T — ковариантность (чтение)
public double sumList(List<? extends Number> list) {
    return list.stream()
               .mapToDouble(Number::doubleValue)
               .sum();
}

// ? super T — контравариантность (запись)
public <T extends Comparable<T>> void sortInto(
    List<T> source,
    List<? super T> destination
) {
    List<T> sorted = new ArrayList<>(source);
    Collections.sort(sorted);
    destination.addAll(sorted);
}

// PECS: Producer Extends, Consumer Super
public <T> void copy(List<? extends T> src, List<? super T> dst) {
    for (T item : src) dst.add(item);
}
```

### 3.3 Ограничения стирания типов

```java
public class TypeErasureProblems<T> {

    // Ошибка компиляции: нельзя создать массив параметризованного типа
    // T[] array = new T[10];

    // Ошибка компиляции: нельзя проверить принадлежность к параметризованному типу
    // if (obj instanceof List<String>) { }

    // Ошибка компиляции: нельзя создать экземпляр параметра типа
    // T instance = new T();

    // Обходной путь: передать Class<T> явно
    public T createInstance(Class<T> clazz) throws Exception {
        return clazz.getDeclaredConstructor().newInstance();
    }

    public boolean isInstanceOf(Object obj, Class<T> clazz) {
        return clazz.isInstance(obj);
    }
}
```

### 3.4 Преимущества и недостатки

**Преимущества:**
- Обратная совместимость: raw types позволяют использовать новые generic-классы в коде без параметров.
- Один экземпляр байткода для всех инстанциаций — нет раздувания бинарного файла.
- Wildcards обеспечивают выразительную use-site вариантность.
- Интеграция с reflection позволяет инструментальным средствам работать с generic-кодом.

**Недостатки:**
- Стирание типов исключает возможность использования параметров типа в рантайме.
- Примитивные типы не могут быть аргументами типа: `List<int>` невозможен, только `List<Integer>` с накладными расходами на boxing/unboxing.
- `? extends` и `? super` — источник постоянных ошибок у разработчиков.
- Нет reified generics: информация о реальном параметре типа в рантайме недоступна.
- Перегрузка методов по параметрам типа невозможна: `void f(List<String>)` и `void f(List<Integer>)` конфликтуют после стирания.
- Нет специализаций: невозможно предоставить оптимизированную реализацию для конкретного типа.

---

## 4. C# — Дженерики с реификацией

### 4.1 Модель реализации

C# реализует reified generics: параметры типов сохраняются и в скомпилированном коде, и в рантайме. CLR генерирует отдельный нативный код для каждой инстанциации с value-типом и одну общую реализацию для всех инстанциаций с reference-типами.

```csharp
// Обобщённый класс с несколькими ограничениями
public class Repository<TEntity, TKey>
    where TEntity : class, IEntity<TKey>, new()
    where TKey    : struct, IComparable<TKey>
{
    private readonly Dictionary<TKey, TEntity> _store = new();

    public void Add(TEntity entity) =>
        _store[entity.Id] = entity;

    public TEntity? Find(TKey id) =>
        _store.TryGetValue(id, out var entity) ? entity : null;

    // Создание экземпляра возможно благодаря ограничению new()
    public TEntity CreateAndAdd(TKey id) {
        var entity = new TEntity { Id = id };
        _store[id] = entity;
        return entity;
    }
}

// Обобщённый метод расширения
public static class Extensions {
    public static TResult Pipe<TInput, TResult>(
        this TInput value,
        Func<TInput, TResult> transform
    ) => transform(value);
}
```

### 4.2 Система ограничений

```csharp
// Полный перечень ограничений C#
public class Constraints<T>
    where T : class              // reference type
    // where T : struct           // value type (несовместимо с class)
    where T : new()              // публичный конструктор без параметров
    where T : SomeBaseClass      // наследование от класса
    where T : ISomeInterface     // реализация интерфейса
    where T : unmanaged          // unmanaged type (C# 7.3)
    where T : notnull            // non-nullable (C# 8.0)
{ }

// Статические абстрактные члены в интерфейсах (C# 11)
// позволяют обобщать операторы и статические фабрики
public interface IAddable<T> where T : IAddable<T> {
    static abstract T operator+(T left, T right);
    static abstract T Zero { get; }
}

public static T Sum<T>(IEnumerable<T> source)
    where T : IAddable<T>
{
    return source.Aggregate(T.Zero, (acc, x) => acc + x);
}
```

### 4.3 Реификация в рантайме

```csharp
using System.Reflection;

public class RuntimeGenericDemo {
    public void ShowReification<T>() {
        // Реальный тип доступен в рантайме
        Type type = typeof(T);
        Console.WriteLine(type.Name);

        // Проверка в рантайме
        if (typeof(T) == typeof(string)) {
            // специфичная ветвь для строк
        }

        // Создание закрытого generic-типа динамически
        Type listType = typeof(List<>).MakeGenericType(type);
        var list = Activator.CreateInstance(listType)!;

        // Получение generic-параметров через reflection
        if (type.IsGenericType) {
            Type[] args = type.GetGenericArguments();
            foreach (var arg in args)
                Console.WriteLine(arg.Name);
        }
    }
}
```

### 4.4 Вариантность в C#

```csharp
// Declaration-site variance на интерфейсах и делегатах

// Ковариантность (out): IEnumerable<Derived> совместим с IEnumerable<Base>
public interface IProducer<out T> {
    T Produce();
}

// Контравариантность (in): Action<Base> совместим с Action<Derived>
public interface IConsumer<in T> {
    void Consume(T item);
}

// Применение: безопасное присваивание
IEnumerable<string>  strings = new List<string>();
IEnumerable<object>  objects = strings;  // работает, т.к. out
```

### 4.5 Преимущества и недостатки

**Преимущества:**
- Реификация обеспечивает полную информацию о типе в рантайме.
- Value-типы (int, double, struct) используются без boxing: `List<int>` хранит `int`, а не `object`.
- Система ограничений включает `new()`, `unmanaged`, `notnull` — возможности, недоступные в Java.
- Статические абстрактные члены интерфейсов (C# 11) позволяют обобщать операторы и другие статические контракты.
- Declaration-site variance через `in`/`out` на интерфейсах и делегатах.

**Недостатки:**
- Вариантность доступна только для интерфейсов и делегатов, но не для классов.
- Система ограничений менее выразительна, чем концепты C++ или типклассы Haskell: нельзя выразить произвольные предикаты над типом.
- Нет специализаций: невозможно предоставить иную реализацию для конкретного аргумента типа.
- Для value-типов генерируется отдельный код каждой инстанциации, что увеличивает размер сборки.
- Синтаксис ограничений становится громоздким при нескольких параметрах типа.

---

## 5. Python — Обобщения через аннотации и структурную типизацию

### 5.1 Модель реализации

Python — динамически типизированный язык. Параметры типов существуют только для статических анализаторов (mypy, pyright, pytype) и полностью игнорируются интерпретатором. Реальная гибкость обеспечивается утиной типизацией: объект подходит везде, где он поддерживает требуемые операции.

```python
from typing import TypeVar, Generic
from collections.abc import Iterator, Sequence

T = TypeVar('T')

# Обобщённый класс
class Stack(Generic[T]):
    def __init__(self) -> None:
        self._items: list[T] = []

    def push(self, item: T) -> None:
        self._items.append(item)

    def pop(self) -> T:
        if not self._items:
            raise IndexError("Stack is empty")
        return self._items.pop()

    def peek(self) -> T:
        return self._items[-1]

    def __len__(self) -> int:
        return len(self._items)

    def __iter__(self) -> Iterator[T]:
        return reversed(self._items)  # type: ignore[arg-type]

# Обобщённая функция
def first(sequence: Sequence[T]) -> T:
    if not sequence:
        raise ValueError("Empty sequence")
    return sequence[0]
```

### 5.2 Protocol — структурная типизация

```python
from typing import Protocol, runtime_checkable

# Структурный интерфейс: тип совместим, если обладает нужными атрибутами
@runtime_checkable
class Comparable(Protocol):
    def __lt__(self, other: 'Comparable') -> bool: ...
    def __le__(self, other: 'Comparable') -> bool: ...

# TypeVar с ограничением через Protocol
T_Comparable = TypeVar('T_Comparable', bound=Comparable)

def max_element(items: Sequence[T_Comparable]) -> T_Comparable:
    if not items:
        raise ValueError("Empty sequence")
    result = items[0]
    for item in items[1:]:
        if item > result:
            result = item
    return result

# TypeVar с явным перечислением допустимых типов
Numeric = TypeVar('Numeric', int, float, complex)

def add(a: Numeric, b: Numeric) -> Numeric:
    return a + b  # type: ignore
```

### 5.3 Синтаксис Python 3.12 (PEP 695)

```python
# Новый встроенный синтаксис параметров типа
def first[T](lst: list[T]) -> T:
    return lst[0]

class Pair[T, U]:
    def __init__(self, first: T, second: U) -> None:
        self.first = first
        self.second = second

# Псевдоним типа
type Vector[T] = list[T]

# Ограничение в новом синтаксисе
def max_val[T: (int, float)](a: T, b: T) -> T:
    return a if a > b else b
```

### 5.4 Преимущества и недостатки

**Преимущества:**
- Утиная типизация обеспечивает реальную гибкость без объявлений совместимости.
- Protocol реализует структурную типизацию: тип совместим с протоколом без явного наследования.
- Постепенная типизация: аннотации опциональны и могут добавляться инкрементально.
- Синтаксис Python 3.12 делает параметры типа частью синтаксиса языка, а не библиотечной конструкцией.

**Недостатки:**
- Никакой типовой безопасности в рантайме: `Stack[int]` идентичен `Stack[str]` с точки зрения интерпретатора.
- Статические анализаторы (mypy, pyright) по-разному трактуют одни и те же конструкции.
- Нет оптимизаций на основе типов: тип аргумента не влияет на генерируемый байткод.
- Все объекты хранятся как ссылки; нет эквивалента unboxed value types.
- Разница между `bound=` и перечислением типов в `TypeVar` неочевидна и ведёт к ошибкам.

---

## 6. Rust — Трейты и мономорфизация

### 6.1 Модель реализации

Rust реализует обобщённое программирование через трейты (traits) и мономорфизацию. Трейты описывают поведение (набор методов и ассоциированных типов), а обобщённые функции параметризуются ограничениями на трейты. При компиляции для каждой комбинации конкретных типов генерируется отдельная реализация.

```rust
use std::fmt::Display;

// Обобщённая структура
#[derive(Debug, Clone)]
struct Pair<T> {
    first: T,
    second: T,
}

// Реализация методов с ограничениями
impl<T: Display + PartialOrd> Pair<T> {
    fn new(first: T, second: T) -> Self {
        Self { first, second }
    }

    fn cmp_display(&self) {
        if self.first >= self.second {
            println!("{}", self.first);
        } else {
            println!("{}", self.second);
        }
    }
}

// Обобщённая функция с ограничением трейта
fn largest<T: PartialOrd + Copy>(list: &[T]) -> T {
    let mut result = list[0];
    for &item in list.iter() {
        if item > result {
            result = item;
        }
    }
    result
}
```

### 6.2 Трейты, where-clauses, статический и динамический dispatch

```rust
use std::fmt::{Debug, Display};

// where-clause для читаемости при нескольких ограничениях
fn print_pair<T, U>(pair: (T, U))
where
    T: Display + Debug,
    U: Display + Clone,
{
    println!("({}, {})", pair.0, pair.1);
}

// Статический dispatch: компилятор генерирует отдельный код для каждого T
fn static_dispatch<T: Display>(value: T) {
    println!("{}", value);
}

// Динамический dispatch: один код, выбор метода через vtable в рантайме
fn dynamic_dispatch(value: &dyn Display) {
    println!("{}", value);
}

// impl Trait как opaque return type
fn make_adder(x: i32) -> impl Fn(i32) -> i32 {
    move |y| x + y
}

// Ассоциированные типы в трейтах
trait Container {
    type Item;
    fn first(&self)    -> Option<&Self::Item>;
    fn last(&self)     -> Option<&Self::Item>;
    fn len(&self)      -> usize;
    fn is_empty(&self) -> bool { self.len() == 0 }
}
```

### 6.3 Const Generics и Generic Associated Types

```rust
// Const generics (стабильны с Rust 1.51)
struct Matrix<T, const ROWS: usize, const COLS: usize> {
    data: [[T; COLS]; ROWS],
}

impl<T, const N: usize> Matrix<T, N, N>
where
    T: Default + Copy + std::ops::Add<Output = T>,
{
    fn trace(&self) -> T {
        let mut sum = T::default();
        for i in 0..N {
            sum = sum + self.data[i][i];
        }
        sum
    }
}

// Generic Associated Types (GAT, стабильны с Rust 1.65)
trait StreamingIterator {
    type Item<'a> where Self: 'a;
    fn next<'a>(&'a mut self) -> Option<Self::Item<'a>>;
}

// PhantomData для типовых маркеров без хранения значений
use std::marker::PhantomData;

struct Wrapper<T, State> {
    value: T,
    _state: PhantomData<State>,
}

struct Locked;
struct Unlocked;

impl<T> Wrapper<T, Locked> {
    fn unlock(self) -> Wrapper<T, Unlocked> {
        Wrapper { value: self.value, _state: PhantomData }
    }
}
```

### 6.4 Преимущества и недостатки

**Преимущества:**
- Мономорфизация обеспечивает производительность, сопоставимую с C++, без накладных расходов на dynamic dispatch.
- Трейты — выразительная система ограничений с поддержкой ассоциированных типов и методов по умолчанию.
- Const generics позволяют параметризоваться по значениям констант (размеры массивов, числовые параметры).
- Borrow checker работает в полную силу внутри обобщённого кода, сохраняя гарантии безопасности памяти.
- GAT позволяют выражать сложные зависимости между временами жизни и ассоциированными типами.
- Чёткое разграничение статического (`T: Trait`) и динамического (`dyn Trait`) dispatch — выбор явен.

**Недостатки:**
- Мономорфизация приводит к раздуванию бинарного файла при большом числе инстанциаций.
- Сочетание дженериков с lifetime-аннотациями быстро увеличивает сложность сигнатур.
- Специализация трейтов (feature `specialization`) нестабильна и недоступна в stable Rust.
- Медленная компиляция: borrow checking + мономорфизация + LLVM-оптимизации.
- GAT имеют известные ограничения: ряд паттернов невозможно выразить из-за ограничений системы вывода типов.

---

## 7. Haskell — Параметрический полиморфизм и типклассы

### 7.1 Модель реализации

Haskell реализует параметрический полиморфизм, основанный на системе типов Хиндли–Милнера. Компилятор автоматически выводит типы; явные аннотации опциональны. Типклассы (typeclasses) — механизм ad-hoc полиморфизма: они описывают набор операций, которые должен поддерживать тип, и позволяют определять инстансы ретроспективно.

```haskell
-- Параметрический полиморфизм: работает для любого типа a
identity :: a -> a
identity x = x

-- Типкласс с несколькими методами
class Container f where
    empty  :: f a
    insert :: a -> f a -> f a
    toList :: f a -> [a]

-- Инстанс для списка
instance Container [] where
    empty  = []
    insert = (:)
    toList = id

-- Обобщённая функция с ограничением типкласса
fromList :: Container f => [a] -> f a
fromList = foldr insert empty

-- Пользовательский тип с инстансами стандартных типклассов
data Tree a = Leaf | Node (Tree a) a (Tree a)

instance Functor Tree where
    fmap _ Leaf         = Leaf
    fmap f (Node l x r) = Node (fmap f l) (f x) (fmap f r)

instance Foldable Tree where
    foldr _ z Leaf         = z
    foldr f z (Node l x r) = foldr f (f x (foldr f z r)) l
```

### 7.2 Многопараметрические типклассы и типы высшего рода

```haskell
{-# LANGUAGE MultiParamTypeClasses  #-}
{-# LANGUAGE FunctionalDependencies #-}
{-# LANGUAGE FlexibleInstances      #-}

-- Многопараметрический типкласс с функциональной зависимостью
-- a -> b означает: тип a однозначно определяет тип b
class Convert a b | a -> b where
    convert :: a -> b

instance Convert String Int    where convert = read
instance Convert Int    Double where convert = fromIntegral

-- Типы высшего рода (kind * -> *): обобщение над конструкторами типов
class Monad m where
    return :: a -> m a
    (>>=)  :: m a -> (a -> m b) -> m b

-- Монадный трансформер: параметризация по конструктору типа m
newtype StateT s m a = StateT { runStateT :: s -> m (a, s) }

instance Monad m => Monad (StateT s m) where
    return a = StateT $ \s -> return (a, s)
    m >>= f  = StateT $ \s -> do
        (a, s') <- runStateT m s
        runStateT (f a) s'
```

### 7.3 Семейства типов (Type Families)

```haskell
{-# LANGUAGE TypeFamilies #-}

-- Ассоциированные семейства типов
class Collection c where
    type Element c
    insert'  :: Element c -> c -> c
    member   :: Element c -> c -> Bool
    toList'  :: c -> [Element c]

data IntSet = IntSet [Int]

instance Collection IntSet where
    type Element IntSet = Int
    insert' x (IntSet xs) = IntSet (x : xs)
    member  x (IntSet xs) = x `elem` xs
    toList' (IntSet xs)   = xs

-- Замкнутые семейства типов для вычислений на уровне типов
type family If (b :: Bool) (t :: *) (f :: *) :: * where
    If 'True  t _ = t
    If 'False _ f = f
```

### 7.4 Преимущества и недостатки

**Преимущества:**
- Параметрический полиморфизм гарантирует корректность через теоремы параметричности: по сигнатуре функции можно вывести все возможные реализации.
- Вывод типов устраняет необходимость явно указывать параметры типа в большинстве случаев.
- Типклассы поддерживают ретроспективные инстансы: можно добавить реализацию для стороннего типа.
- Типы высшего рода позволяют обобщаться по конструкторам типов (`Functor`, `Monad`, `Traversable`).
- Семейства типов предоставляют вычисления на уровне типов, выраженные декларативно.
- Типклассы допускают формальные законы, которые документируют инварианты поведения.

**Недостатки:**
- Для одного типа может существовать только один инстанс данного типкласса в области видимости. Конфликты инстансов (orphan instances) — источник проблем при работе с несколькими библиотеками.
- Ошибки компилятора при использовании многопараметрических типклассов и семейств типов трудночитаемы.
- Нет механизма специализации: нельзя предоставить оптимизированный инстанс для конкретного типа в обход общего.
- Параметры типа высшего рода практически не встречаются в других распространённых языках, что затрудняет перенос паттернов.
- Ленивые вычисления (lazy evaluation) взаимодействуют с обобщённым кодом непредсказуемо с точки зрения потребления памяти.

---

## 8. TypeScript — Структурная типизация и дженерики

### 8.1 Модель реализации

TypeScript добавляет статическую типизацию к JavaScript через стирание типов: все типовые аннотации удаляются при компиляции в JavaScript. Основа системы — структурная типизация: совместимость определяется набором свойств и методов, а не именем типа.

```typescript
// Обобщённый интерфейс
interface Repository<T, ID> {
    findById(id: ID): Promise<T | null>;
    findAll(): Promise<T[]>;
    save(entity: T): Promise<T>;
    delete(id: ID): Promise<void>;
}

// Обобщённый класс
class Stack<T> {
    private items: T[] = [];

    push(item: T): void {
        this.items.push(item);
    }

    pop(): T {
        const item = this.items.pop();
        if (item === undefined) throw new Error('Stack is empty');
        return item;
    }

    peek(): T {
        if (this.items.length === 0) throw new Error('Stack is empty');
        return this.items[this.items.length - 1];
    }

    get size(): number { return this.items.length; }
}

// Обобщённые функции
function identity<T>(arg: T): T { return arg; }

function zip<A, B>(as: A[], bs: B[]): [A, B][] {
    return as.map((a, i) => [a, bs[i]]);
}
```

### 8.2 Условные типы и mapped types

```typescript
// Условные типы
type IsArray<T> = T extends any[] ? true : false;
type Flatten<T> = T extends Array<infer U> ? U : T;

// Стандартные утилитарные типы через mapped types
type Readonly<T>  = { readonly [K in keyof T]: T[K] };
type Partial<T>   = { [K in keyof T]?: T[K] };
type Required<T>  = { [K in keyof T]-?: T[K] };

// Рекурсивный mapped type
type DeepReadonly<T> = {
    readonly [K in keyof T]: T[K] extends object
        ? DeepReadonly<T[K]>
        : T[K];
};

// Template literal types
type EventName<T extends string> = `on${Capitalize<T>}`;
type ClickHandler = EventName<'click'>; // 'onClick'

// infer в условных типах
type ReturnType<T extends (...args: any[]) => any> =
    T extends (...args: any[]) => infer R ? R : never;

type Parameters<T extends (...args: any[]) => any> =
    T extends (...args: infer P) => any ? P : never;

// Рекурсивный тип
type DeepPartial<T> = T extends object
    ? { [K in keyof T]?: DeepPartial<T[K]> }
    : T;
```

### 8.3 Ограничения и вариантность

```typescript
// Ограничение через extends
function getProperty<T, K extends keyof T>(obj: T, key: K): T[K] {
    return obj[key];
}

// Структурная типизация: достаточно совпадения структуры
interface Printable {
    toString(): string;
}

function print<T extends Printable>(value: T): void {
    console.log(value.toString());
}

// Вариантность — структурная, выводится компилятором автоматически
// Дискриминированные объединения в сочетании с дженериками
type Result<T, E> =
    | { success: true;  value: T }
    | { success: false; error: E };

function map<T, U, E>(
    result: Result<T, E>,
    f: (value: T) => U
): Result<U, E> {
    return result.success
        ? { success: true, value: f(result.value) }
        : result;
}
```

### 8.4 Преимущества и недостатки

**Преимущества:**
- Структурная типизация устраняет необходимость явно объявлять совместимость типов.
- Условные типы и mapped types позволяют выражать сложные преобразования на уровне типов.
- Template literal types дают возможность работать со строковыми паттернами как с типами.
- Вывод параметров типа работает в большинстве контекстов без явных аннотаций.
- Постепенная типизация: дженерики вводятся в существующий JavaScript-код без его переписывания.

**Недостатки:**
- Стирание типов: никакой информации о дженерик-параметрах в рантайме.
- Структурная типизация допускает случайную совместимость несвязанных типов.
- Условные типы становятся нечитаемыми при нескольких уровнях вложенности.
- Тип `any` полностью отключает проверку для всей цепочки вызовов.
- Вариантность выводится компилятором автоматически и не всегда совпадает с ожиданиями разработчика.
- Все вычисления ограничены возможностями JavaScript-рантайма.

---

## 9. Julia — Multiple Dispatch и параметрические типы

### 9.1 Модель реализации

Julia использует механизм multiple dispatch в сочетании с JIT-компиляцией (LLVM) и параметрическими типами. Функции не являются дженерик-шаблонами в традиционном смысле: компилятор автоматически специализирует каждый метод под конкретные типы аргументов при первом вызове. Выбор метода определяется типами всех аргументов одновременно.

```julia
# Параметрический составной тип
struct Stack{T}
    items::Vector{T}
    Stack{T}() where T = new(T[])
end

function push!(s::Stack{T}, item::T) where T
    push!(s.items, item)
    return s
end

function pop!(s::Stack{T}) where T
    isempty(s.items) && throw(ArgumentError("Stack is empty"))
    return pop!(s.items)
end

# Multiple dispatch: метод выбирается по типам всех аргументов
combine(a::Int,    b::Int)    = a + b
combine(a::String, b::String) = a * b
combine(a::Vector, b::Vector) = vcat(a, b)

# Параметрическая функция с ограничением
function dot_product(a::Vector{T}, b::Vector{T}) where T <: Number
    length(a) == length(b) || throw(DimensionMismatch())
    return sum(a .* b)
end
```

### 9.2 Параметрические типы и вариантность

```julia
# Параметрические типы в Julia инвариантны:
# Vector{Float64} не является подтипом Vector{Number}

# Для ковариантного поведения используется синтаксис <:
function sum_numbers(v::AbstractVector{<:Number})
    return sum(v)
end

# Union типов
function process(x::Union{Int, Float64})
    return x * 2
end

# Параметрические абстрактные типы
abstract type Shape{N} end

struct Circle{T<:Real} <: Shape{2}
    center::NTuple{2, T}
    radius::T
end

struct Sphere{T<:Real} <: Shape{3}
    center::NTuple{3, T}
    radius::T
end

volume(c::Circle{T}) where T = π * c.radius^2
volume(s::Sphere{T}) where T = (4/3) * π * s.radius^3
```

### 9.3 Сгенерированные функции и макросы

```julia
# @generated — функция, код которой генерируется на этапе компиляции
# в зависимости от типов аргументов
@generated function sum_fields(x::T) where T
    fields = fieldnames(T)
    isempty(fields) && return :(zero(eltype(T)))
    expr = :(x.$(fields[1]))
    for field in fields[2:end]
        expr = :($expr + x.$(field))
    end
    return expr
end

struct Point3D
    x::Float64
    y::Float64
    z::Float64
end

# sum_fields(Point3D(1.0, 2.0, 3.0)) вернёт 6.0
# без итерации по полям в рантайме

# Макрос для генерации методов
macro define_ops(T)
    quote
        Base.:+(a::$T, b::$T) = $T(a.value + b.value)
        Base.:-(a::$T, b::$T) = $T(a.value - b.value)
        Base.:*(a::$T, b::Number) = $T(a.value * b)
    end
end
```

### 9.4 Преимущества и недостатки

**Преимущества:**
- Multiple dispatch — наиболее гибкий механизм выбора реализации: метод определяется типами всех аргументов одновременно.
- JIT-специализация автоматически генерирует оптимизированный код для каждой комбинации типов.
- Производительность числовых вычислений сопоставима с C при использовании конкретных типов.
- Параметрические типы и `@generated`-функции позволяют выражать сложные зависимости между типами.
- Полная информация о типах доступна в рантайме.

**Недостатки:**
- Длительная задержка при первом запуске (time-to-first-execution) из-за JIT-компиляции.
- Параметрические типы инвариантны, что требует явного использования `<:` для ковариантного поведения.
- Отсутствие формальных интерфейсов: «протоколы» определяются соглашениями и документацией, но не проверяются компилятором.
- Ошибки несовпадения методов (MethodError) обнаруживаются только в рантайме.
- Язык ориентирован на численные вычисления; применение в других доменах ограничено экосистемой.

---

## 10. Сравнительный анализ

<a name="compare"></a>

### 10.1 Сводная таблица характеристик

| Критерий | C++ | Java | C# | Python | Rust | Haskell | TypeScript | Julia |
|---|---|---|---|---|---|---|---|---|
| Модель | Шаблоны / мономорфизация | Стирание типов | Реификация | Аннотации | Трейты / мономорфизация | Типклассы / параметрический полиморфизм | Стирание типов | Multiple dispatch / JIT |
| Типовая безопасность в рантайме | Нет | Частичная | Полная | Нет | Частичная | Нет | Нет | Полная |
| Производительность с value-типами | Без потерь | Boxing overhead | Без потерь | Всегда boxing | Без потерь | Зависит от реализации | JS-рантайм | Без потерь (JIT) |
| Вариантность | Через концепты | Use-site (wildcards) | Declaration-site (in/out) | Через TypeVar | Через трейты | Параметрическая | Структурная (автоматическая) | Инвариантная |
| Метапрограммирование | TMP, if constexpr | Reflection | Reflection, source generators | Метаклассы, декораторы | Процедурные макросы | Type families, Template Haskell | Conditional types | @generated, макросы |
| Специализация для конкретных типов | Полная | Нет | Нет | Нет | Частичная (нестабильна) | Нет | Нет | Через multiple dispatch |
| Ограничения на параметры типа | Концепты | Bounds, wildcards | Constraints | Protocol, TypeVar bounds | Trait bounds | Typeclass constraints | extends | where T <: |
| Проверка ограничений | Компиляция | Компиляция | Компиляция | Статический анализатор | Компиляция | Компиляция | Компиляция | Компиляция / рантайм |

### 10.2 Мономорфизация против стирания типов

Два доминирующих подхода к реализации дженериков имеют противоположные компромиссы:

**Мономорфизация (C++, Rust):**
- Для каждой комбинации конкретных типов генерируется отдельный машинный код.
- Компилятор применяет полную оптимизацию с учётом конкретного типа: инлайнинг, устранение ветвлений, векторизация.
- Накладные расходы в рантайме отсутствуют.
- Размер бинарного файла растёт пропорционально числу инстанциаций.
- Компиляция замедляется при большом числе шаблонных инстанциаций.

**Стирание типов (Java, TypeScript):**
- Один экземпляр байткода обслуживает все инстанциации.
- Размер бинарного файла не зависит от числа параметров типа.
- В Java работа с примитивными типами требует boxing; value-типы не могут быть аргументами.
- Информация о реальном параметре типа недоступна в рантайме.
- Ряд операций над параметрами типа запрещён компилятором (создание экземпляра, instanceof).

**Реификация (C#):**
- CLR генерирует отдельный нативный код для каждой инстанциации с value-типом.
- Все инстанциации с reference-типами разделяют одну реализацию.
- Информация о реальных параметрах типа доступна через reflection.
- Компромисс: код растёт только для value-типов, что практически выгоднее полной мономорфизации.

### 10.3 Номинальная против структурной типизации в контексте дженериков

**Номинальная типизация (Java, C#, Rust, Haskell):**
Тип совместим с ограничением только если он явно объявлен как реализующий соответствующий интерфейс, трейт или типкласс. Это исключает случайную совместимость и делает намерения автора явными. Ретроспективное добавление совместимости (для стороннего типа) возможно в Rust и Haskell, но ограничено в Java и C#.

**Структурная типизация (TypeScript, Python):**
Тип совместим с ограничением если он обладает требуемой структурой (набором полей и методов). Явное объявление не требуется. Это максимизирует гибкость, но допускает случайную совместимость несвязанных типов с одинаковой структурой.

### 10.4 Выразительность систем ограничений

Системы ограничений языков различаются по тому, что именно можно потребовать от параметра типа:

| Что можно потребовать | C++ | Java | C# | Rust | Haskell |
|---|---|---|---|---|---|
| Реализация интерфейса / трейта | Концепт | Bound | Constraint | Trait bound | Typeclass |
| Наличие конкретного конструктора | Через концепт | Нет | `new()` | Через трейт | Нет |
| Принадлежность к value / reference типу | Нет | Нет | `struct` / `class` | Через трейты | Нет |
| Отсутствие указателей (unmanaged) | Нет | Нет | `unmanaged` | `Copy` | Нет |
| Произвольный предикат над типом | Через концепт | Нет | Нет | Нет | Частично через type families |
| Ограничение на ассоциированный тип | Через концепт | Нет | Через интерфейс | `where T::Item: Trait` | Через type families |

### 10.5 Производительность: качественное сравнение

Производительность обобщённого кода определяется двумя факторами: возможностью применять оптимизации с учётом конкретного типа и накладными расходами на работу со значениями.

Языки с мономорфизацией (C++, Rust) и Julia (JIT) дают наилучшую производительность для числовых алгоритмов, поскольку компилятор видит конкретный тип и может применить векторизацию, устранение ветвлений и инлайнинг. C# занимает промежуточное положение: value-типы оптимизируются, reference-типы — нет. Java проигрывает из-за неизбежного boxing при работе с примитивами через дженерик-контейнеры. TypeScript и Python ограничены возможностями своих рантаймов (V8 и CPython соответственно).

### 10.6 Сравнение по задачам реализации типовых паттернов

**Коллекция с типобезопасным итератором:**
- C++: `template<typename T> class Container` с `iterator` — полный контроль, высокая сложность.
- Java: `class Container<T> implements Iterable<T>` — стирание типов, boxing для примитивов.
- C#: `class Container<T> : IEnumerable<T>` — реификация, без boxing для value-типов.
- Rust: `struct Container<T>` с реализацией трейта `Iterator` — мономорфизация, borrow checker.
- Haskell: через `Foldable` и `Traversable` — наиболее обобщённо, через типклассы.

**Функция, работающая с любым числовым типом:**
- C++: концепт `Numeric`, мономорфизация — нулевые накладные расходы.
- Java: `<T extends Number>` — работает только с boxed-типами.
- C#: статические абстрактные члены (C# 11) — оператор `+` обобщается через интерфейс.
- Rust: трейты `Add`, `Sub`, `Mul` из `std::ops` — мономорфизация.
- Haskell: `Num a => a -> a -> a` — выводится автоматически.
- Python: `TypeVar('T', int, float)` — только статическая проверка.

---

<a name="langs"></a>

## 11. Заключение

Обобщённое программирование реализуется через принципиально разные механизмы в зависимости от целей языка:

**C++** предоставляет наиболее мощный механизм: шаблоны Тьюринг-полны на этапе компиляции, концепты формализуют ограничения, специализации позволяют оптимизировать под конкретные типы. Цена — сложность языка, медленная компиляция и раздувание бинарных файлов.

**Java** выбрала стирание типов ради обратной совместимости. Это решение, принятое в 2004 году, наложило фундаментальные ограничения: невозможность работы с примитивами без boxing и недоступность реального типа в рантайме. Проект Valhalla (value types) частично устраняет эти ограничения.

**C#** реализовала reified generics с момента появления CLR 2.0, что позволило избежать boxing для value-типов и обеспечить доступность информации о типе в рантайме. Добавление статических абстрактных членов в C# 11 приближает систему ограничений по выразительности к типклассам.

**Python** использует дженерики исключительно как инструмент статического анализа. Интерпретатор игнорирует все параметры типа; реальная гибкость обеспечивается утиной типизацией. Механизм Protocol предоставляет структурную типизацию без явного наследования.

**Rust** сочетает мономорфизацию (производительность C++) с системой трейтов (выразительность близкая к типклассам Haskell) и гарантиями безопасности памяти. Ограничения: нестабильная специализация и сложность при сочетании с lifetime-аннотациями.

**Haskell** предоставляет наиболее теоретически строгую реализацию: параметрический полиморфизм с автоматическим выводом типов, типклассы с ретроспективными инстансами и типы высшего рода. Применение паттернов Haskell в других языках ограничено принципиальными различиями систем типов.

**TypeScript** строит систему дженериков поверх JavaScript, используя структурную типизацию и стирание типов. Условные типы и mapped types делают систему типов TypeScript одной из наиболее выразительных среди языков со стиранием типов. Фундаментальное ограничение — невозможность выхода за рамки того, что может сделать JavaScript в рантайме.

**Julia** заменяет традиционные дженерики механизмом multiple dispatch с JIT-специализацией. Это обеспечивает производительность, сопоставимую с языками со статической компиляцией, при динамической типизации. Слабая сторона — отсутствие формальных интерфейсов и задержка первого запуска.

Ни один из рассмотренных подходов не является доминирующим по всем критериям одновременно. Каждый язык представляет конкретную точку в пространстве компромиссов между производительностью, безопасностью типов, выразительностью системы ограничений и сложностью реализации.

---

*Версии языков на момент написания: C++23, Java 21, C# 12, Python 3.12, Rust 1.75, GHC 9.8, TypeScript 5.3, Julia 1.10*