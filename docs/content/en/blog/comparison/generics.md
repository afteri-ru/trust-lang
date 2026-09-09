---
title: Generic programming in different languages
slug: generics
date: 2026-03-24
tags: [generics, c++, rust, python, programming-languages]
---

Generic programming is a paradigm that allows describing algorithms and data structures in abstraction from concrete types. Despite the single concept, each language implements it in its own way: through templates, parametric polymorphism, type erasure or duck typing. This article provides a detailed analysis of the mechanisms of generic programming in eight languages - C++, Java, C#, Python, Rust, Haskell, TypeScript, Julia - with an assessment of the strengths and weaknesses of each approach and a comparative summary.

---

## 1. Introduction

The idea of generic programming goes back to the works of Alexander Stepanov and David Musser in the late 1980s. Its essence is the separation of algorithms from concrete data types while preserving type safety and controlled performance. In practice, different languages solve this task through different mechanisms:

| Approach | Representatives |
|---|---|
| Templates (compile-time code generation) | C++, Rust |
| Type erasure | Java, TypeScript |
| Reification (reified generics) | C# |
| Parametric polymorphism + typeclasses | Haskell |
| Annotations + structural typing | Python |
| Multiple dispatch + JIT | Julia |

Each mechanism is considered according to the following criteria: the implementation model, the system of constraints on types, type safety, performance, expressiveness, limitations and known problems.

---

## 2. C++ - Templates and concepts

### 2.1 Implementation model

C++ implements generic programming through templates - a mechanism of compile-time metaprogramming. A template is an instruction to the compiler to generate code for each concrete set of arguments. This mechanism is called monomorphization: for each instantiation of the template, a separate copy of the code is generated.

```cpp
// Function template
template<typename T>
T max_value(T a, T b) {
    return (a > b) ? a : b;
}

// Class template with a default parameter
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

// Full specialization of the template for bool
template<>
class Stack<bool> {
    std::vector<uint8_t> data_; // bitwise storage
    // ...
};
```

### 2.2 Concepts (C++20)

Before C++20, constraints on types were expressed through SFINAE (Substitution Failure Is Not An Error) and `std::enable_if`. C++20 introduced concepts - named predicates over types, checked at compile time.

```cpp
#include <concepts>
#include <numeric>

// Definition of a concept
template<typename T>
concept Numeric = std::integral<T> || std::floating_point<T>;

template<typename T>
concept Sortable = requires(T container) {
    { container.begin() } -> std::input_iterator;
    { container.end() }   -> std::input_iterator;
    requires std::totally_ordered<typename T::value_type>;
};

// Using the concept
template<Numeric T>
T sum(const std::vector<T>& v) {
    return std::accumulate(v.begin(), v.end(), T{});
}

// Compound constraints
template<typename T>
requires Sortable<T> && (!std::same_as<typename T::value_type, bool>)
void sort_container(T& container) {
    std::sort(container.begin(), container.end());
}

// Abbreviated syntax (abbreviated function template)
void print_numeric(Numeric auto value) {
    std::cout << value << '\n';
}
```

### 2.3 Template metaprogramming

C++ provides a mechanism for computations at compile time:

```cpp
// Computing the Fibonacci number at compile time
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

// Branching at the type level (if constexpr, C++17)
template<typename T>
void process(T value) {
    if constexpr (std::is_integral_v<T>) {
        // branch for integers
    } else if constexpr (std::is_floating_point_v<T>) {
        // branch for floating-point numbers
    }
}
```

### 2.4 Advantages and disadvantages

**Advantages:**
- Zero runtime overhead - all polymorphism is resolved at compile time.
- Templates are Turing-complete at compile time, which opens up broad possibilities for metaprogramming.
- Partial and full specializations make it possible to provide optimized implementations for concrete types.
- Concepts (C++20) make compilation errors readable and turn constraints into part of the documentation.
- The standard library (STL) is built on templates and is a reference implementation of generic programming.

**Disadvantages:**
- Binary file bloat (code bloat): each instantiation generates separate machine code.
- Slow compilation: template code is compiled anew in each translation unit.
- The implementation of templates must be located in header files, which violates encapsulation.
- Before C++20, error diagnostics were extremely unreadable; with concepts the situation improved, but did not become trivial.
- SFINAE and partial specializations remain difficult even for experienced developers.

---

## 3. Java - Generics and type erasure

### 3.1 Implementation model

Java added generics in version 1.5 (2004). The key design decision is type erasure: type parameters exist only at compile time and are completely removed from the bytecode. All instantiations of `List<String>` and `List<Integer>` in the compiled code turn into `List` (raw type), and the type parameters are replaced by their upper bounds (usually `Object`).

```java
// Generic class
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

// Bounded type parameter
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

### 3.2 Wildcards and variance

Java uses use-site variance through wildcards:

```java
// ? extends T - covariance (reading)
public double sumList(List<? extends Number> list) {
    return list.stream()
               .mapToDouble(Number::doubleValue)
               .sum();
}

// ? super T - contravariance (writing)
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

### 3.3 Limitations of type erasure

```java
public class TypeErasureProblems<T> {

    // Compilation error: cannot create an array of a parameterized type
    // T[] array = new T[10];

    // Compilation error: cannot check membership in a parameterized type
    // if (obj instanceof List<String>) { }

    // Compilation error: cannot create an instance of a type parameter
    // T instance = new T();

    // Workaround: pass Class<T> explicitly
    public T createInstance(Class<T> clazz) throws Exception {
        return clazz.getDeclaredConstructor().newInstance();
    }

    public boolean isInstanceOf(Object obj, Class<T> clazz) {
        return clazz.isInstance(obj);
    }
}
```

### 3.4 Advantages and disadvantages

**Advantages:**
- Backward compatibility: raw types make it possible to use new generic classes in code without parameters.
- A single bytecode instance serves all instantiations - no binary file bloat.
- Wildcards provide expressive use-site variance.
- Integration with reflection allows tooling to work with generic code.

**Disadvantages:**
- Type erasure excludes the possibility of using type parameters at runtime.
- Primitive types cannot be type arguments: `List<int>` is impossible, only `List<Integer>` with boxing/unboxing overhead.
- `? extends` and `? super` are a source of constant errors for developers.
- No reified generics: information about the real type parameter at runtime is unavailable.
- Overloading methods by type parameters is impossible: `void f(List<String>)` and `void f(List<Integer>)` conflict after erasure.
- No specializations: it is impossible to provide an optimized implementation for a concrete type.

---

## 4. C# - Generics with reification

### 4.1 Implementation model

C# implements reified generics: type parameters are preserved both in the compiled code and at runtime. The CLR generates separate native code for each instantiation with a value type and one shared implementation for all instantiations with reference types.

```csharp
// Generic class with several constraints
public class Repository<TEntity, TKey>
    where TEntity : class, IEntity<TKey>, new()
    where TKey    : struct, IComparable<TKey>
{
    private readonly Dictionary<TKey, TEntity> _store = new();

    public void Add(TEntity entity) =>
        _store[entity.Id] = entity;

    public TEntity? Find(TKey id) =>
        _store.TryGetValue(id, out var entity) ? entity : null;

    // Creating an instance is possible thanks to the new() constraint
    public TEntity CreateAndAdd(TKey id) {
        var entity = new TEntity { Id = id };
        _store[id] = entity;
        return entity;
    }
}

// Generic extension method
public static class Extensions {
    public static TResult Pipe<TInput, TResult>(
        this TInput value,
        Func<TInput, TResult> transform
    ) => transform(value);
}
```

### 4.2 Constraint system

```csharp
// Full list of C# constraints
public class Constraints<T>
    where T : class              // reference type
    // where T : struct           // value type (incompatible with class)
    where T : new()              // public parameterless constructor
    where T : SomeBaseClass      // inheritance from a class
    where T : ISomeInterface     // interface implementation
    where T : unmanaged          // unmanaged type (C# 7.3)
    where T : notnull            // non-nullable (C# 8.0)
{ }

// Static abstract members in interfaces (C# 11)
// make it possible to generalize operators and static factories
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

### 4.3 Reification at runtime

```csharp
using System.Reflection;

public class RuntimeGenericDemo {
    public void ShowReification<T>() {
        // The real type is available at runtime
        Type type = typeof(T);
        Console.WriteLine(type.Name);

        // Check at runtime
        if (typeof(T) == typeof(string)) {
            // a specific branch for strings
        }

        // Creating a closed generic type dynamically
        Type listType = typeof(List<>).MakeGenericType(type);
        var list = Activator.CreateInstance(listType)!;

        // Getting generic parameters through reflection
        if (type.IsGenericType) {
            Type[] args = type.GetGenericArguments();
            foreach (var arg in args)
                Console.WriteLine(arg.Name);
        }
    }
}
```

### 4.4 Variance in C#

```csharp
// Declaration-site variance on interfaces and delegates

// Covariance (out): IEnumerable<Derived> is compatible with IEnumerable<Base>
public interface IProducer<out T> {
    T Produce();
}

// Contravariance (in): Action<Base> is compatible with Action<Derived>
public interface IConsumer<in T> {
    void Consume(T item);
}

// Application: safe assignment
IEnumerable<string>  strings = new List<string>();
IEnumerable<object>  objects = strings;  // works, because out
```

### 4.5 Advantages and disadvantages

**Advantages:**
- Reification provides complete type information at runtime.
- Value types (int, double, struct) are used without boxing: `List<int>` stores `int`, not `object`.
- The constraint system includes `new()`, `unmanaged`, `notnull` - capabilities unavailable in Java.
- Static abstract members of interfaces (C# 11) make it possible to generalize operators and other static contracts.
- Declaration-site variance through `in`/`out` on interfaces and delegates.

**Disadvantages:**
- Variance is available only for interfaces and delegates, but not for classes.
- The constraint system is less expressive than the concepts of C++ or the typeclasses of Haskell: arbitrary predicates over a type cannot be expressed.
- No specializations: it is impossible to provide a different implementation for a concrete type argument.
- For value types, separate code is generated for each instantiation, which increases the assembly size.
- The constraint syntax becomes cumbersome with several type parameters.

---

## 5. Python - Generics through annotations and structural typing

### 5.1 Implementation model

Python is a dynamically typed language. Type parameters exist only for static analyzers (mypy, pyright, pytype) and are completely ignored by the interpreter. Real flexibility is provided by duck typing: an object fits anywhere it supports the required operations.

```python
from typing import TypeVar, Generic
from collections.abc import Iterator, Sequence

T = TypeVar('T')

# Generic class
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

# Generic function
def first(sequence: Sequence[T]) -> T:
    if not sequence:
        raise ValueError("Empty sequence")
    return sequence[0]
```

### 5.2 Protocol - structural typing

```python
from typing import Protocol, runtime_checkable

# Structural interface: a type is compatible if it has the needed attributes
@runtime_checkable
class Comparable(Protocol):
    def __lt__(self, other: 'Comparable') -> bool: ...
    def __le__(self, other: 'Comparable') -> bool: ...

# TypeVar with a constraint through Protocol
T_Comparable = TypeVar('T_Comparable', bound=Comparable)

def max_element(items: Sequence[T_Comparable]) -> T_Comparable:
    if not items:
        raise ValueError("Empty sequence")
    result = items[0]
    for item in items[1:]:
        if item > result:
            result = item
    return result

# TypeVar with an explicit enumeration of allowed types
Numeric = TypeVar('Numeric', int, float, complex)

def add(a: Numeric, b: Numeric) -> Numeric:
    return a + b  # type: ignore
```

### 5.3 Python 3.12 syntax (PEP 695)

```python
# New built-in type parameter syntax
def first[T](lst: list[T]) -> T:
    return lst[0]

class Pair[T, U]:
    def __init__(self, first: T, second: U) -> None:
        self.first = first
        self.second = second

# Type alias
type Vector[T] = list[T]

# Constraint in the new syntax
def max_val[T: (int, float)](a: T, b: T) -> T:
    return a if a > b else b
```

### 5.4 Advantages and disadvantages

**Advantages:**
- Duck typing provides real flexibility without compatibility declarations.
- Protocol implements structural typing: a type is compatible with a protocol without explicit inheritance.
- Gradual typing: annotations are optional and can be added incrementally.
- The Python 3.12 syntax makes type parameters part of the language syntax, and not a library construct.

**Disadvantages:**
- No type safety at runtime: `Stack[int]` is identical to `Stack[str]` from the interpreter's point of view.
- Static analyzers (mypy, pyright) interpret the same constructs differently.
- No type-based optimizations: the type of an argument does not affect the generated bytecode.
- All objects are stored as references; there is no equivalent of unboxed value types.
- The difference between `bound=` and an enumeration of types in `TypeVar` is non-obvious and leads to errors.

---

## 6. Rust - Traits and monomorphization

### 6.1 Implementation model

Rust implements generic programming through traits and monomorphization. Traits describe behavior (a set of methods and associated types), and generic functions are parameterized by trait bounds. During compilation, a separate implementation is generated for each combination of concrete types.

```rust
use std::fmt::Display;

// Generic structure
#[derive(Debug, Clone)]
struct Pair<T> {
    first: T,
    second: T,
}

// Implementing methods with constraints
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

// Generic function with a trait bound
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

### 6.2 Traits, where-clauses, static and dynamic dispatch

```rust
use std::fmt::{Debug, Display};

// where-clause for readability with several constraints
fn print_pair<T, U>(pair: (T, U))
where
    T: Display + Debug,
    U: Display + Clone,
{
    println!("({}, {})", pair.0, pair.1);
}

// Static dispatch: the compiler generates separate code for each T
fn static_dispatch<T: Display>(value: T) {
    println!("{}", value);
}

// Dynamic dispatch: one code, method selection through a vtable at runtime
fn dynamic_dispatch(value: &dyn Display) {
    println!("{}", value);
}

// impl Trait as an opaque return type
fn makeadder(x: i32) -> impl Fn(i32) -> i32 {
    move |y| x + y
}

// Associated types in traits
trait Container {
    type Item;
    fn first(&self)    -> Option<&Self::Item>;
    fn last(&self)     -> Option<&Self::Item>;
    fn len(&self)      -> usize;
    fn is_empty(&self) -> bool { self.len() == 0 }
}
```

### 6.3 Const Generics and Generic Associated Types

```rust
// Const generics (stable since Rust 1.51)
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

// Generic Associated Types (GAT, stable since Rust 1.65)
trait StreamingIterator {
    type Item<'a> where Self: 'a;
    fn next<'a>(&'a mut self) -> Option<Self::Item<'a>>;
}

// PhantomData for type markers without storing values
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

### 6.4 Advantages and disadvantages

**Advantages:**
- Monomorphization provides performance comparable to C++, without the overhead of dynamic dispatch.
- Traits are an expressive constraint system with support for associated types and default methods.
- Const generics make it possible to be parameterized by constant values (array sizes, numeric parameters).
- The borrow checker works at full strength inside generic code, preserving memory safety guarantees.
- GATs make it possible to express complex dependencies between lifetimes and associated types.
- A clear distinction between static (`T: Trait`) and dynamic (`dyn Trait`) dispatch - the choice is explicit.

**Disadvantages:**
- Monomorphization leads to binary file bloat when there is a large number of instantiations.
- The combination of generics with lifetime annotations quickly increases the complexity of signatures.
- Trait specialization (the `specialization` feature) is unstable and unavailable in stable Rust.
- Slow compilation: borrow checking + monomorphization + LLVM optimizations.
- GATs have known limitations: a number of patterns cannot be expressed due to the limitations of the type inference system.

---

## 7. Haskell - Parametric polymorphism and typeclasses

### 7.1 Implementation model

Haskell implements parametric polymorphism based on the Hindley-Milner type system. The compiler automatically infers types; explicit annotations are optional. Typeclasses are a mechanism of ad-hoc polymorphism: they describe a set of operations that a type must support, and make it possible to define instances retrospectively.

```haskell
-- Parametric polymorphism: works for any type a
identity :: a -> a
identity x = x

-- Typeclass with several methods
class Container f where
    empty  :: f a
    insert :: a -> f a -> f a
    toList :: f a -> [a]

-- Instance for a list
instance Container [] where
    empty  = []
    insert = (:)
    toList = id

-- Generic function with a typeclass constraint
fromList :: Container f => [a] -> f a
fromList = foldr insert empty

-- User-defined type with instances of standard typeclasses
data Tree a = Leaf | Node (Tree a) a (Tree a)

instance Functor Tree where
    fmap _ Leaf         = Leaf
    fmap f (Node l x r) = Node (fmap f l) (f x) (fmap f r)

instance Foldable Tree where
    foldr _ z Leaf         = z
    foldr f z (Node l x r) = foldr f (f x (foldr f z r)) l
```

### 7.2 Multi-parameter typeclasses and higher-kinded types

```haskell
{-# LANGUAGE MultiParamTypeClasses  #-}
{-# LANGUAGE FunctionalDependencies #-}
{-# LANGUAGE FlexibleInstances      #-}

-- Multi-parameter typeclass with a functional dependency
-- a -> b means: the type a uniquely determines the type b
class Convert a b | a -> b where
    convert :: a -> b

instance Convert String Int    where convert = read
instance Convert Int    Double where convert = fromIntegral

-- Higher-kinded types (kind * -> *): generalization over type constructors
class Monad m where
    return :: a -> m a
    (>>=)  :: m a -> (a -> m b) -> m b

-- Monad transformer: parameterization over the type constructor m
newtype StateT s m a = StateT { runStateT :: s -> m (a, s) }

instance Monad m => Monad (StateT s m) where
    return a = StateT $ \s -> return (a, s)
    m >>= f  = StateT $ \s -> do
        (a, s') <- runStateT m s
        runStateT (f a) s'
```

### 7.3 Type Families

```haskell
{-# LANGUAGE TypeFamilies #-}

-- Associated type families
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

-- Closed type families for computations at the type level
type family If (b :: Bool) (t :: *) (f :: *) :: * where
    If 'True  t _ = t
    If 'False _ f = f
```

### 7.4 Advantages and disadvantages

**Advantages:**
- Parametric polymorphism guarantees correctness through parametricity theorems: all possible implementations can be derived from the signature of a function.
- Type inference removes the need to explicitly specify type parameters in most cases.
- Typeclasses support retrospective instances: one can add an implementation for a third-party type.
- Higher-kinded types make it possible to generalize over type constructors (`Functor`, `Monad`, `Traversable`).
- Type families provide computations at the type level, expressed declaratively.
- Typeclasses allow formal laws that document the invariants of behavior.

**Disadvantages:**
- For one type there can be only one instance of a given typeclass in scope. Instance conflicts (orphan instances) are a source of problems when working with several libraries.
- Compiler errors when using multi-parameter typeclasses and type families are hard to read.
- There is no specialization mechanism: it is impossible to provide an optimized instance for a concrete type bypassing the general one.
- Higher-kinded type parameters practically do not occur in other widespread languages, which complicates the transfer of patterns.
- Lazy evaluation interacts with generic code unpredictably from the point of view of memory consumption.

---

## 8. TypeScript - Structural typing and generics

### 8.1 Implementation model

TypeScript adds static typing to JavaScript through type erasure: all type annotations are removed when compiling to JavaScript. The basis of the system is structural typing: compatibility is determined by the set of properties and methods, and not by the name of the type.

```typescript
// Generic interface
interface Repository<T, ID> {
    findById(id: ID): Promise<T | null>;
    findAll(): Promise<T[]>;
    save(entity: T): Promise<T>;
    delete(id: ID): Promise<void>;
}

// Generic class
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

// Generic functions
function identity<T>(arg: T): T { return arg; }

function zip<A, B>(as: A[], bs: B[]): [A, B][] {
    return as.map((a, i) => [a, bs[i]]);
}
```

### 8.2 Conditional types and mapped types

```typescript
// Conditional types
type IsArray<T> = T extends any[] ? true : false;
type Flatten<T> = T extends Array<infer U> ? U : T;

// Standard utility types through mapped types
type Readonly<T>  = { readonly [K in keyof T]: T[K] };
type Partial<T>   = { [K in keyof T]?: T[K] };
type Required<T>  = { [K in keyof T]-?: T[K] };

// Recursive mapped type
type DeepReadonly<T> = {
    readonly [K in keyof T]: T[K] extends object
        ? DeepReadonly<T[K]>
        : T[K];
};

// Template literal types
type EventName<T extends string> = `on${Capitalize<T>}`;
type ClickHandler = EventName<'click'>; // 'onClick'

// infer in conditional types
type ReturnType<T extends (...args: any[]) => any> =
    T extends (...args: any[]) => infer R ? R : never;

type Parameters<T extends (...args: any[]) => any> =
    T extends (...args: infer P) => any ? P : never;

// Recursive type
type DeepPartial<T> = T extends object
    ? { [K in keyof T]?: DeepPartial<T[K]> }
    : T;
```

### 8.3 Constraints and variance

```typescript
// Constraint through extends
function getProperty<T, K extends keyof T>(obj: T, key: K): T[K] {
    return obj[key];
}

// Structural typing: a structural match is enough
interface Printable {
    toString(): string;
}

function print<T extends Printable>(value: T): void {
    console.log(value.toString());
}

// Variance is structural, inferred by the compiler automatically
// Discriminated unions in combination with generics
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

### 8.4 Advantages and disadvantages

**Advantages:**
- Structural typing removes the need to explicitly declare the compatibility of types.
- Conditional types and mapped types make it possible to express complex transformations at the type level.
- Template literal types give the ability to work with string patterns as types.
- Inferring type parameters works in most contexts without explicit annotations.
- Gradual typing: generics are introduced into existing JavaScript code without rewriting it.

**Disadvantages:**
- Type erasure: no information about generic parameters at runtime.
- Structural typing allows accidental compatibility of unrelated types.
- Conditional types become unreadable with several levels of nesting.
- The `any` type completely disables checking for the whole call chain.
- Variance is inferred by the compiler automatically and does not always match the developer's expectations.
- All computations are limited by the capabilities of the JavaScript runtime.

---

## 9. Julia - Multiple Dispatch and parametric types

### 9.1 Implementation model

Julia uses the mechanism of multiple dispatch in combination with JIT compilation (LLVM) and parametric types. Functions are not generic templates in the traditional sense: the compiler automatically specializes each method for concrete argument types on the first call. The choice of method is determined by the types of all arguments simultaneously.

```julia
# Parametric composite type
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

# Multiple dispatch: the method is selected by the types of all arguments
combine(a::Int,    b::Int)    = a + b
combine(a::String, b::String) = a * b
combine(a::Vector, b::Vector) = vcat(a, b)

# Parametric function with a constraint
function dot_product(a::Vector{T}, b::Vector{T}) where T <: Number
    length(a) == length(b) || throw(DimensionMismatch())
    return sum(a .* b)
end
```

### 9.2 Parametric types and variance

```julia
# Parametric types in Julia are invariant:
# Vector{Float64} is not a subtype of Vector{Number}

# For covariant behavior the <: syntax is used
function sum_numbers(v::AbstractVector{<:Number})
    return sum(v)
end

# Union of types
function process(x::Union{Int, Float64})
    return x * 2
end

# Parametric abstract types
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

### 9.3 Generated functions and macros

```julia
# @generated - a function whose code is generated at compile time
# depending on the types of the arguments
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

# sum_fields(Point3D(1.0, 2.0, 3.0)) will return 6.0
# without iterating over the fields at runtime

# Macro for generating methods
macro define_ops(T)
    quote
        Base.:+(a::$T, b::$T) = $T(a.value + b.value)
        Base.:-(a::$T, b::$T) = $T(a.value - b.value)
        Base.:*(a::$T, b::Number) = $T(a.value * b)
    end
end
```

### 9.4 Advantages and disadvantages

**Advantages:**
- Multiple dispatch is the most flexible mechanism for choosing an implementation: the method is determined by the types of all arguments simultaneously.
- JIT specialization automatically generates optimized code for each combination of types.
- The performance of numeric computations is comparable to C when concrete types are used.
- Parametric types and `@generated` functions make it possible to express complex dependencies between types.
- Complete type information is available at runtime.

**Disadvantages:**
- A long delay on the first run (time-to-first-execution) due to JIT compilation.
- Parametric types are invariant, which requires the explicit use of `<:` for covariant behavior.
- The absence of formal interfaces: "protocols" are defined by conventions and documentation, but are not checked by the compiler.
- Method mismatch errors (MethodError) are detected only at runtime.
- The language is oriented toward numerical computations; its application in other domains is limited by the ecosystem.

---

## 10. Comparative analysis

<a name="compare"></a>

### 10.1 Summary table of characteristics

| Criterion | C++ | Java | C# | Python | Rust | Haskell | TypeScript | Julia |
|---|---|---|---|---|---|---|---|---|
| Model | Templates / monomorphization | Type erasure | Reification | Annotations | Traits / monomorphization | Typeclasses / parametric polymorphism | Type erasure | Multiple dispatch / JIT |
| Type safety at runtime | No | Partial | Full | No | Partial | No | No | Full |
| Performance with value types | No loss | Boxing overhead | No loss | Always boxing | No loss | Depends on the implementation | JS runtime | No loss (JIT) |
| Variance | Through concepts | Use-site (wildcards) | Declaration-site (in/out) | Through TypeVar | Through traits | Parametric | Structural (automatic) | Invariant |
| Metaprogramming | TMP, if constexpr | Reflection | Reflection, source generators | Metaclasses, decorators | Procedural macros | Type families, Template Haskell | Conditional types | @generated, macros |
| Specialization for concrete types | Full | No | No | No | Partial (unstable) | No | No | Through multiple dispatch |
| Constraints on type parameters | Concepts | Bounds, wildcards | Constraints | Protocol, TypeVar bounds | Trait bounds | Typeclass constraints | extends | where T <: |
| Constraint checking | Compilation | Compilation | Compilation | Static analyzer | Compilation | Compilation | Compilation | Compilation / runtime |

### 10.2 Monomorphization versus type erasure

The two dominant approaches to implementing generics have opposite trade-offs:

**Monomorphization (C++, Rust):**
- For each combination of concrete types, separate machine code is generated.
- The compiler applies full optimization taking into account the concrete type: inlining, branch elimination, vectorization.
- There are no runtime overheads.
- The size of the binary file grows proportionally to the number of instantiations.
- Compilation slows down with a large number of template instantiations.

**Type erasure (Java, TypeScript):**
- A single bytecode instance serves all instantiations.
- The size of the binary file does not depend on the number of type parameters.
- In Java, working with primitive types requires boxing; value types cannot be arguments.
- Information about the real type parameter is unavailable at runtime.
- A number of operations on type parameters are forbidden by the compiler (creating an instance, instanceof).

**Reification (C#):**
- The CLR generates separate native code for each instantiation with a value type.
- All instantiations with reference types share one implementation.
- Information about the real type parameters is available through reflection.
- Trade-off: code grows only for value types, which is practically more advantageous than full monomorphization.

### 10.3 Nominal versus structural typing in the context of generics

**Nominal typing (Java, C#, Rust, Haskell):**
A type is compatible with a constraint only if it is explicitly declared as implementing the corresponding interface, trait or typeclass. This excludes accidental compatibility and makes the author's intentions explicit. Retrospective addition of compatibility (for a third-party type) is possible in Rust and Haskell, but limited in Java and C#.

**Structural typing (TypeScript, Python):**
A type is compatible with a constraint if it has the required structure (a set of fields and methods). An explicit declaration is not required. This maximizes flexibility, but allows accidental compatibility of unrelated types with the same structure.

### 10.4 Expressiveness of constraint systems

The constraint systems of languages differ in what exactly can be required of a type parameter:

| What can be required | C++ | Java | C# | Rust | Haskell |
|---|---|---|---|---|---|
| Implementation of an interface / trait | Concept | Bound | Constraint | Trait bound | Typeclass |
| Presence of a concrete constructor | Through a concept | No | `new()` | Through a trait | No |
| Belonging to a value / reference type | No | No | `struct` / `class` | Through traits | No |
| Absence of pointers (unmanaged) | No | No | `unmanaged` | `Copy` | No |
| An arbitrary predicate over a type | Through a concept | No | No | No | Partially through type families |
| A constraint on an associated type | Through a concept | No | Through an interface | `where T::Item: Trait` | Through type families |

### 10.5 Performance: a qualitative comparison

The performance of generic code is determined by two factors: the possibility of applying optimizations taking into account the concrete type and the overhead of working with values.

Languages with monomorphization (C++, Rust) and Julia (JIT) give the best performance for numeric algorithms, since the compiler sees the concrete type and can apply vectorization, branch elimination and inlining. C# occupies an intermediate position: value types are optimized, reference types are not. Java loses because of the inevitable boxing when working with primitives through generic containers. TypeScript and Python are limited by the capabilities of their runtimes (V8 and CPython respectively).

### 10.6 Comparison by tasks of implementing typical patterns

**A collection with a type-safe iterator:**
- C++: `template<typename T> class Container` with `iterator` - full control, high complexity.
- Java: `class Container<T> implements Iterable<T>` - type erasure, boxing for primitives.
- C#: `class Container<T> : IEnumerable<T>` - reification, no boxing for value types.
- Rust: `struct Container<T>` with an implementation of the `Iterator` trait - monomorphization, borrow checker.
- Haskell: through `Foldable` and `Traversable` - the most generalized, through typeclasses.

**A function that works with any numeric type:**
- C++: the `Numeric` concept, monomorphization - zero overhead.
- Java: `<T extends Number>` - works only with boxed types.
- C#: static abstract members (C# 11) - the `+` operator is generalized through an interface.
- Rust: the `Add`, `Sub`, `Mul` traits from `std::ops` - monomorphization.
- Haskell: `Num a => a -> a -> a` - inferred automatically.
- Python: `TypeVar('T', int, float)` - static checking only.

---

<a name="langs"></a>

## 11. Conclusion

Generic programming is implemented through fundamentally different mechanisms depending on the goals of the language:

**C++** provides the most powerful mechanism: templates are Turing-complete at compile time, concepts formalize constraints, specializations make it possible to optimize for concrete types. The price is the complexity of the language, slow compilation and binary file bloat.

**Java** chose type erasure for the sake of backward compatibility. This decision, made in 2004, imposed fundamental limitations: the impossibility of working with primitives without boxing and the unavailability of the real type at runtime. The Valhalla project (value types) partially removes these limitations.

**C#** implemented reified generics from the moment CLR 2.0 appeared, which made it possible to avoid boxing for value types and to ensure the availability of type information at runtime. The addition of static abstract members in C# 11 brings the constraint system closer to typeclasses in expressiveness.

**Python** uses generics exclusively as a tool of static analysis. The interpreter ignores all type parameters; real flexibility is provided by duck typing. The Protocol mechanism provides structural typing without explicit inheritance.

**Rust** combines monomorphization (the performance of C++) with a system of traits (expressiveness close to the typeclasses of Haskell) and memory safety guarantees. Limitations: unstable specialization and complexity when combined with lifetime annotations.

**Haskell** provides the most theoretically rigorous implementation: parametric polymorphism with automatic type inference, typeclasses with retrospective instances and higher-kinded types. The application of Haskell patterns in other languages is limited by fundamental differences in type systems.

**TypeScript** builds a system of generics on top of JavaScript, using structural typing and type erasure. Conditional types and mapped types make the type system of TypeScript one of the most expressive among languages with type erasure. The fundamental limitation is the impossibility of going beyond what JavaScript can do at runtime.

**Julia** replaces traditional generics with the mechanism of multiple dispatch with JIT specialization. This provides performance comparable to statically compiled languages, with dynamic typing. The weak side is the absence of formal interfaces and the delay of the first run.

None of the considered approaches is dominant by all criteria at the same time. Each language represents a concrete point in the space of trade-offs between performance, type safety, the expressiveness of the constraint system and the complexity of the implementation.

---

*Versions of the languages at the time of writing: C++23, Java 21, C# 12, Python 3.12, Rust 1.75, GHC 9.8, TypeScript 5.3, Julia 1.10*