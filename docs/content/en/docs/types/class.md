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

For **`:Struct`** (strictly POD) a default field value is **forbidden** by the analyzer (NSDMI violates
triviality): the field is declared as `field:Type := _;`. For **`:Class`** the field value is not checked —
both `:= <value>` (emitted as a C++ default member initializer) and `:= _` (without an initializer) are allowed.

Virtual methods (`@[virtual]`) and overriding (`@[override]`) are **not implemented** in the current version —
their use leads to an explicit compilation error (rather than silent ignoring).

## Operator overloading {#operators}

An operator is declared by a **symbol name in backticks** (the `REFLECTION` lexeme): in the class body —
as a method, at the top level of a module — as a free function. The body and signature are like those of an ordinary method.

```python
Point ::= :Class{
    x: Int32 := _;
    `==`(o: Point): Bool := { ... };      # member → C++ operator==
    `()`(i: Int32): Int32 := { ... };     # operator()  (member only)
    `[]`(i: Int32): Int32 := { ... };     # operator[]  (member only)
};

`!=`(a: Point, b: Point): Bool := { ... };   # free operator (all operands are parameters)
```

The symbol is transferred into the C++ `operator<sym>` (`` `==` `` → `operator==`, `` `()` `` → `operator()`);
the expressions `a == b` / `a != b` for user types are resolved by C++ overloading, therefore
the operator can be used in ordinary expressions.

Usage in TrustLang code: `a == b` (comparisons), `a(5)` (calling a value — the `()` operator),
`a[i]` (indexing — the `[]` operator). Each form is transferred into the corresponding C++ operation
(`a(5)` → `a(5)`, `a[i]` → `(a)[i]`) and resolved by C++ overloading.

Implemented: comparisons `== != < > <= >=` (both as a member and free) and call/index `()` / `[]`
(**member only** — a C++ requirement; `[]` accepts exactly one parameter). The arity is checked
(a comparison — one parameter for a member and two for a free one), a repeated symbol in a type is forbidden.

The usage is checked at the analysis stage: a comparison/call/indexing requires a declared operator
on the type (otherwise — a compilation error, not a C++ error); the arity of the call is checked against the declaration;
a conflict "member + free function" of the same symbol is diagnosed as ambiguity. Operators
support **contracts** (`trust_pre`/`trust_post`, like functions and methods). Template operators
(`<T> \`==\`(...)`) are not implemented — an explicit error.

