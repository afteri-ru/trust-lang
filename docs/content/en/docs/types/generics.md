---
title: Generic programming
tags: [syntax, generics]
weight: 50
draft: true
---

Generic programming allows writing algorithms and data structures that
work with different types, regardless of their concrete form. Different languages implement this paradigm
in their own way; a comparative analysis of the approaches in other languages is published in a separate
[blog article](/en/blog/comparison/generics/).

In *TrustLang*, generics are implemented in the current version through **type parameters** and their substitution in
declarations of native template types and methods of collections/ranges. The "generic
dispatcher function" model with runtime dispatch by types and a separate template syntax is not yet
implemented (see the "What is not implemented" section below).

## Native template types {#native-template}

A native C++ template type is declared with a prefix of type parameters and a native C++ name
(the `@include` mnemonic adds the attribute `@[include]` to the declaration):

```trust
@include("vector")
<T> %std::vector() := ...;    # declares the type vector<T> with the C++ name std::vector

v: vector<Int32> := [1, 2, 3,];
# → #include <vector>
# → std::vector<int32_t> c_v = std::vector<int32_t>{1, 2, 3};
```

Using `vector<Int32>` (in annotations: declarations, parameters, returns) interns the concrete
instantiation, and the C++ header is included **on-use** — only when the type is actually used. If the
C++ name coincides with a built-in container (`std::vector`/`std::array` for `:Array`), a soft
diagnostic is output, and the type is resolved through the built-in `:Array`. A type argument is written as `Int32`
(with a warning about the sigil `:`) or an explicit type `:Int32`/`:MyClass`/`:MyClass<:Int8>` (nested
templates).

### Type alias to a template

The operator `::=` accepts ONLY a type (a type name, a typed template, `Enum`/`Variant` from a dictionary), not an
arbitrary expression. Therefore an alias to a template is written as a type name:

```trust
copy ::= :vector<Int32>;
x:copy := [1, 2, 3,];
# → using c_copy = std::vector<int32_t>;
```

## Native template classes (forward declaration) {#native-template-class}

A native C++ template class with a member interface is declared via `::=` with type parameters on the left
and an **explicit implementation** `%std::pair<T1,T2>` on the right:

```trust
@include("utility")
<T1,T2> Pair ::= %std::pair<T1,T2> {   # trust-template Pair ↔ std::pair
    %first(): T1 := ...;               # Pair<A,B>.first
    %second(): T2 := ...;              # Pair<A,B>.second
};

p: Pair<Int32, StrChar> := ...;
# → std::pair<int32_t, std::string> + #include <utility>
```

The generalized form `<T1,T2> Pair ::= <T1,T2> %std::pair { ... }` (the template is bound without an explicit list
of arguments in the RHS) — sugar, the implementation is **deferred** (a "not implemented" diagnostic); use the explicit
`%std::pair<T1,T2>`. The notation format is the same for a native and a non-native name (only the `%` differs).
More — [Native types](../types/native/).

## User template classes {#class-template}

A user class can be made templated: the type parameters are specified to the left of the declaration
(`<T>` or `<T,U>`) and are used in fields/methods as ordinary types. The code generator emits a C++
`template <typename T> struct`, and the usage interns the concrete instantiation (`c_Box<int32_t>`):

```trust
<T> :Box ::= :Class{
    value: T := _;
    get(): T := { return value; };
    set(v: T): Void := { value = v; };
};

b : Box<Int32> := Box();     # → template <typename T> struct c_Box { T c_value; ... };
b.set(42);                   # → c_Box<int32_t> c_b = c_Box<int32_t>();
```

A template class can be inherited: `<T> :Derived ::= :Base<:T>{ ... }` → `struct c_Derived : public c_Base<T>`.
Type arguments are mandatory (`:Box` without `<...>` is an error). Only type parameters are supported;
value parameters and constraints — no.

### Struct templates (POD)

`:Struct` can also be templated (`<T> :Point ::= :Struct{ x: T := _; y: T := _; }`). The definition
is emitted as `template <typename T> struct c_Point` **without** `static_assert`: a POD check over a template
is inexpressible and depends on `T`. The check is emitted for a **concrete instantiation**:

```cpp
template <typename T> struct c_Point { T c_x; T c_y; };
c_Point<int32_t> c_p = c_Point<int32_t>(1, 2);
static_assert(std::is_trivial_v<c_Point<int32_t>> && std::is_standard_layout_v<c_Point<int32_t>>,
              "trust: Struct 'Point' must be POD");
```

### Constructor of a template class

The constructor is written in the short form `Box(args)` — the C++ name of the instantiation is taken from the target type
of the context (the annotation of the variable/the type of the LHS of the assignment), therefore an empty list also works. Or the explicit
form `:Box<Int32>(args)` (similar to `:vector<Int32>(...)` for native templates):

```trust
b : Box<Int32> := Box();          # → c_Box<int32_t> c_b = c_Box<int32_t>();
b  = Box(7);                      # → c_b = c_Box<int32_t>(7);
c : Box<Int32> := :Box<Int32>(5); # → c_Box<int32_t> c_c = c_Box<int32_t>(5);

v := :vector<Int32>(1, 2, 3);     # → std::vector<int32_t> c_v = std::vector<int32_t>{1, 2, 3};
```

## Restrictions of the `::=` operator

A non-type in the RHS of `::=` (`func(...) ::= { }`, `name ::= '123'`, `term ::= term2(arg)`) is an error: `::=`
creates a type, it has no arithmetic/expressions. The "class-with-body" form (`func(arg) ::= Class(){...}`)
is not implemented in the current version (planned).

## Sets of allowed types {#type-sets}

A special case of type variability is implemented as a **set of allowed types** — sugar over explicit
overloading: `Name ::= :A + :B;` (named) or `:(:A + :B)` (inline). A function with a set parameter
is expanded into N definitions with a common body. Details and restrictions — in
[Functions](funcs/#type-sets).

## What is not implemented

Typical value parameters (`array<Int32, 4>`) and constraints of type parameters (`T:Integer`), as well as
the model of a generalized "dispatcher function" are under development. The concept is described in the
[blog article](/en/blog/comparison/generics/), the current implementation boundaries — in
[Implementation status](../status/).

