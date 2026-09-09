---
title: Type system
weight: 1
tags: [types, oop, collections, generics]
---

## Principles

*TrustLang* has a **closed type system**: any new data type necessarily inherits
(extends) one of the already existing ones. This resembles the `Object` model in Java, but without physical inheritance (without boxing simple types into objects).
This approach is the basis for [generic programming](generics/), when a derived type inherits the set of operations of the "parent" type,
and continues to work with all its descendants.

The language is dynamically typed: the type of a variable can be specified explicitly or left to automatic inference.
The specified type is used for compatibility checks on assignment, for checking the type of
a function's return value and for controlling the placement in memory of [native types](native/).
Automatic casting is possible only between compatible types; otherwise an explicit
[type conversion](type_system/) is required.

## Type categories

Data types are divided into **simple** (scalar, non-extensible) and **composite**
(container and user-defined). Briefly by category (details — on the
corresponding pages of the section):

| Category | Types | Description on the page |
|---|---|---|
| Integers | `:Int8`…`:Int64`, unsigned synonyms (`:Byte`, `:Word`, `:DWord`), `:Bool` | [Numbers and tensors](numbers/) |
| Floating-point numbers | `:Float16`, `:Float32`, `:Float64` | [Numbers and tensors](numbers/) |
| Arbitrary precision | rational `m\n` and `:BigInteger` (GMP) | [Numbers and tensors](numbers/) |
| Strings | `:StrChar` (UTF-8), `:StrWide` | [Character strings](strings/) |
| Ranges and arrays | `:Range`, `:Array` (one-dimensional, `std::vector`) | [Ranges, arrays and tensors](containers/) |
| Dictionaries | `:Dictionary` (positional and named elements) | [Dictionaries and sets](dicts/) |
| User-defined | enumerations `:Enum`, variants `:Variant`, tuples `:Tuple`, aliases `::=` | [Dictionaries and sets](dicts/) |
| Native | import and forward declaration of C/C++ types and templates | [Native types](native/) |

Variables can store a value directly or be **reference** variables (strong/weak references,
`shared`/`weak`) — see [Memory management](../safety/memory/).

## Type names and constructors

Each type has a *short name* starting with a colon (for example, `:Int32`), and a same-named
constructor function without the colon. A new instance is created by calling the constructor: `x := Int32(5);`.
Types can be synonyms — two types with the same short name in different namespaces;
in this case an explicit qualification of the name is required when calling the constructor (see
[Naming of objects](../syntax/naming/)).

## Implementation status

Of the "object-oriented" capabilities, the basic `:Struct`/`:Class` (fields, methods,
inheritance), the types `Enum`/`Variant`/`Tuple` and the forward declaration of native classes
are implemented (see [Classes](class/)). Complex numbers (`:Complex16/32/64`), multidimensional tensors (full-fledged
`:Tensor`), virtual methods/`@[override]` (full-fledged OOP), coroutines and pure functions are not
implemented — more in [Implementation status and limitations](../status/).

## Type conversion

Despite dynamic typing, if the type of a variable is specified explicitly, automatic casting of
types is **not** performed — assigning a value of an incompatible type requires its explicit
conversion.

## Explicit casting by calling a type

An explicit conversion is performed by calling a function with the name of the target type (the short name without
the colon): `:Int32(x)` is read as `Int32(x)` — this is a call to the constructor/type cast.

```python
n := Int32(3.14);      # floating-point number → integer
b := Bool(5);          # number → logical (0/1)
f := Float64(n);       # integer → floating-point number
```

The conversion is performed **with range control**: if the value does not fit into the target type
(narrowing), an error occurs — both at compile time for known values and at runtime for
variables. There is no silent "truncation"/overflow.

## Dictionary as a universal type

A dictionary is the only type that can be cast to any other: to create a value
of a specific type, it is enough to pass a dictionary as an argument to the cast/type constructor. More —
in [Dictionaries and sets](dicts/).

## Conversions between categories

The rules and examples of conversions for specific categories are described together with the types themselves:

- numbers and rational values — [Numbers and tensors](numbers/);
- strings and their formatting — [Character strings](strings/);
- ranges/arrays and changing dimensionality — [Ranges, arrays and tensors](containers/);
- native C/C++ types — [Native types](native/).
