---
title: Classes
weight: 5
tags: [types, oop, collections]
---

The data type `:Class` is similar to a dictionary, but all its fields must have names (access to properties by
index is also possible). When creating an instance of a class, a new type is created that copies the properties and
methods of all parents. Unlike a dictionary, the set of fields of a class is determined at compile time
and can only be extended relative to the parent types.

**Status:** the basic `:Struct` (strictly POD) and `:Class` (inheritance) are implemented, generated in C++ as
`struct`; full-fledged OOP (virtual methods/override, constructors/destructors, static members)
is not implemented in the current version. Of the "object-oriented" types, the enumerations
`:Enum`, the variants `:Variant` and the tuples `:Tuple` are also implemented (see [Dictionaries and sets](dicts/)), as well as
the forward declaration of native C++ classes (`String ::= %std::string { ... };` — see
[Native types](native/)) and user **template classes** (`<T> :Box ::= :Class{...}` — see
[Generic programming](generics/#class-template)). The current implementation boundaries — in
[Implementation status](../status/).

The declaration of `:Struct`/`:Class` — via `::=` with a body whose members are specified by the operator `:=`
(`Name ::= :Base{ ... };`):

```python
Point ::= :Struct{ x:Int32 := _; y:Int32 := _; };      # strictly POD; fields WITHOUT a value (`:= _`)
Animal ::= :Class{ name:Int32 := 0; speak():Int32 := { ... }; };  # Class: default is allowed
Dog ::= :Animal{ age:Int32 := _; };                    # inheritance (Class)
Iface ::= :Class ...;                                  # forward declaration (form without a body)
```

Both types are generated as a C++ `struct` (all members are public); `:Struct` additionally gets a
`static_assert` for POD (`std::is_trivial_v && std::is_standard_layout_v`). Fields/methods with a leading `_`
(protected) and `__` (private) are supported at the level of a naming convention — they affect only
autocompletion; in C++ they are generated without changing access. A bare member name (`obj.field`, `obj.method()`)
is transferred into a direct access to the `struct` member.

Rules of values and declarations:

- `member := ...` — a **method prototype** (a declaration without a body) inside a class definition. A class with a body
  (even if all methods are prototypes) is a **complete definition** (`struct c_Name { ... };`) —
  in C++ prototypes exist only inside a definition, not in a forward declaration.
- `member := _` — **absence of a value** (a field without a default value).
- `field:Type := <value>` — a field with a default value.
- `:Name ::= :Base ...;` (the form **without a body**) — a **forward declaration** → C++ incomplete type
  `struct c_Name;`. The type name is already registered and visible, therefore **later the same name can be
  defined further** (`:Name ::= :Base { ... };`); a repeated *definition* is an error. In C++ a forward declaration
  contains no members (neither fields nor methods); the type is incomplete.
- **Self-references are allowed:** the record type name is visible inside its own body (an analog of the C++
  injected-class-name), therefore `:Node ::= :Class{ next : @[reftype("shared")@] Node := _; }` is correct.
  References (`shared`/`weak`/`unique`-pointer) to an incomplete type are allowed; a value field of itself is
  an infinite-size error (see below).
- `field := ...` is not allowed: in C++ a data member cannot be forward-declared (it exists only in a definition).

**Recursive/cyclic references in fields** are forbidden statically (the analyzer `RefCycleHook`) so as not to
allow leaks and inexpressible types: `-Wrecursive-shared` — a cycle with a `shared` edge (refcount leak;
broken by a `weak` back-reference), `-Wrecursive-unique` — mutual `unique` ownership of two types,
`-Wrecursive-value` — recursion by value (`value`/default `unique` = inline, infinite size). All —
default `error`, switched by `-Wrecursive-*=ignore|warning|error`. Self-reference by a `unique` pointer is a
legal owned list and is not diagnosed.

**Module loading** cycles are forbidden (the import graph is a DAG), so mutually recursive **nominal
types in different modules** cannot be expressed directly. Runtime data cycles (references between
objects) are allowed; for mutual types use a shared third module with the types, a forward handle,
or `:Any` on one side.

For **`:Struct`** (strictly POD) a default field value is **forbidden** by the analyzer (NSDMI violates
triviality): the field is declared as `field:Type := _;`. For **`:Class`** the field value is not checked —
both `:= <value>` (emitted as a C++ default member initializer) and `:= _` (without an initializer) are allowed.

Virtual methods (`@[virtual]`) and overriding (`@[override]`) are **not implemented** in the current version —
their use leads to an explicit compilation error (rather than silent ignoring).

## Operator overloading {#operators}

An operator is declared as an **ordinary function with a system name** (`__eq__`, `__ne__`, `__lt__`,
`__gt__`, `__le__`, `__ge__`, `__call__`, `__getitem__`, `__deref__`, `__inc__`, `__dec__`): in a class
body — as a method, at file top level — as a free function. The body and signature are like those of an
ordinary method.

```python
Point ::= :Class{
    x: Int32 := _;
    __eq__(o: Point): Bool := { ... };       # member → C++ operator==
    __call__(i: Int32): Int32 := { ... };    # operator()  (member only)
    __getitem__(i: Int32): Int32 := { ... }; # operator[]  (member only)
};

__ne__(a: Point, b: Point): Bool := { ... }; # free operator (all operands are parameters)
```

The `__...__` namespace is reserved for the compiler: only registered operator system functions may be
declared; an unknown `__frob__` is an explicit error. The symbol (`==`, `[]`, `()`, …) remains syntax
sugar: `a == b` is equivalent to `__eq__(a, b)`, `a[i]` — `__getitem__(a, i)`, `a(5)` — `__call__(a, 5)`.
Direct calls `__eq__(a,b)` / `a.__eq__(b)` are also allowed.

Code generation: the system name **never** appears in C++ (identifiers with `__` are reserved) — it is
translated into `operator<sym>` (`__eq__` → `operator==`, `__getitem__` → `operator[]`,
`__call__` → `operator()`). Overload selection is always performed by the analyzer; when several
signatures exist, arguments are explicitly cast to the declared parameter types (`static_cast`), so C++
never selects the overload again.

Overloading is a normal **signature set** of one system name: different parameter types are allowed
(including several `__eq__`, `__getitem__`, `__call__` in one type). An exact signature duplicate is an
error; overloading on the return type alone is impossible (C++ compatibility). Implemented: comparisons
`== != < > <= >=` (member and free), call/index `()`/`[]` (**member only**; `[]` accepts exactly one
parameter), `*`, prefix/postfix `++`/`--`; the postfix dummy must be `Int32` (emitted as C++ `int`).
A free operator must have at least one user-defined parameter type.

Usage is checked at the analysis stage: comparison/call/indexing require a declared operator on the type
(otherwise — a compilation error, not a C++ error). The member and free forms of one symbol form
**one overload set**: the choice follows the usual conversion rules (C++), so both forms compete on
equal terms and ambiguity arises only for incomparable best candidates. Operators support **contracts**
(`trust_pre`/`trust_post`, like functions and methods). Template operators (`<T> __eq__(...)`) are not
implemented — an explicit error.


