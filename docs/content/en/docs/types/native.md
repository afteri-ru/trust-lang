---
title: Native Data Types
# linkTitle: Docs
# menu: {main: {weight: 20}}
weight: 70
tags: [типы данных, системые типы, коллекции]
---

Native (machine) data can be variables or functions, and the names of native objects start with the percent symbol "**%**".

In the case of variables, these are binary data located in a single continuous area of machine memory at a specific address and have a strictly defined format.

To use existing libraries, you need to import the native object, thereby creating an object TrustLang, 
but with an implementation in another language, for example, in C/C++.

The syntax for importing native objects is the same as for [creating](/en/docs/ops/create/) regular language objects, 
only the name of the native object needs to be specified as the right operand in the creation operator.

When importing a native object, it is necessary to always specify the variable types, 
as well as the types of arguments and return values for functions.

If the data types of the created object and the native object match (i.e., no type conversion is required), 
then the native object name can be specified with an ellipsis, 
which means that the data types will replicate those specified at the left operand.

```python
:FILE ::= :Plain;

fopen(filename:StrChar, modes:StrChar):FILE ::= %fopen...;
fclose(f:FILE):Int32 ::= %fclose...;
fflush(f:FILE):Int32 ::= %fflush...;
fprintf(f:FILE, format:FmtChar, ...):Int32 ::= %fprintf...;

fremove(filename:String):Int32 ::= %remove...;
frename(old:String, new:String):Int32 ::= %rename...;
```


{{% pageinfo %}}

The following description is under development.

{{% /pageinfo %}}


## Forward declaration of native classes {#native-class}

A native C++ class (non-template) can be used in TrustLang via a **forward declaration** analogous
to forward declarations of functions: the trust class name is bound to the native C++ name, and the
member interface (methods, fields, constructors, static members) is described in the `{ ... }` body.
The body does **not** contain implementations — every member is a forward declaration (`:= ...`); the
class is defined in a C++ header pulled in on-use via the `@[include]` attribute.

```trust
@include("string")
String ::= %std::string {          # trust class String ↔ C++ std::string
    %size(): UInt64 := ...;        # object method → s.size()
    %empty(): Bool := ...;         # object method → s.empty()
    %data: CString := ...;         # object field → s.data
};
```

Syntax:

- `String ::= %std::string { ... };` — the RHS is any name; native ones start with `%` (C++ name
  without `%`). The trust class name is on the left of `::=`.
- Members (all forward, `:= ...`): `%method(...):Ret` — object method → `obj.method(...)`;
  `@::st(...):Ret` — static method → `String::st(...)`; `%field:Type` — object field → `obj.field`;
  `@::static_field:Type` — static field → `String::static_field`;
  `%String(...):String` — constructor (name == class) → `std::string(...)`.
- `@[include("header")@]` (or `@include("header")`) before the declaration pulls the C++ header
  only when the type is used (on-use).

A C++ `struct` for a native class is **not generated** — the existing C++ name is used.

```trust
@include("string")
MyStr ::= %std::string {
    %size(): UInt64 := ...;
    %data: CString := ...;   # object field → s.data
};

s: MyStr := MyStr('hello');              # constructor → std::string("hello")
n: UInt64 := s.%size();                  # method → (c_s).size()
d: CString := s.%data;                   # field → (c_s).data
# → #include <string>
```

## Forward declaration of native template classes {#native-class-template}

A native C++ template class is declared the same way, but with template parameters on the left and an
**explicit realization** through instantiation `%std::pair<T1,T2>` on the right (primary form):

```trust
@include("utility")
<T1,T2> Pair ::= %std::pair<T1,T2> {    # trust template Pair ↔ std::pair
    %first(): T1 := ...;                # Pair<A,B>.first
    %second(): T2 := ...;               # Pair<A,B>.second
};

p: Pair<Int32, StrChar> := ...;
# → std::pair<int32_t, std::string> + #include <utility>
```

The generic form `<T1,T2> Pair ::= <T1,T2> %std::pair { ... }` (the template is bound generically,
without an explicit argument list in the RHS) is sugar whose **implementation is deferred**:
parsing it emits a "not implemented" diagnostic; use the explicit form `%std::pair<T1,T2>`. The record
format is the same for native and non-native names and differs only by the presence/absence of `%`.
