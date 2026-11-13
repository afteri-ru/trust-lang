---
title: Functions
weight: 6
tags: [types]
---

A function is defined by the [object creation operators](../operators/create/); the function name must
comply with the [naming rules](../syntax/naming/) and end with parentheses. The body of an ordinary
function is a [code block](../syntax/concept/#block) located in the namespace of its definition.

The type is specified for the arguments and for the return type (`arg:Type`).

## Sets of allowed types {#type-sets}

> ⚠ **Not implemented in the current version.** Declaring a set (`Name ::= :A + :B`) or a set
> parameter produces the diagnostic “type set definitions are not implemented yet”. The model
> below is the target contract, not the current behavior.

Several allowed types are specified by a **type set** — the mathematical operators
`+` (union) and `-` (exclusion). A set is defined once as a named type:

```python
combi ::= :Int8 + :Int16 + :Int32;   # named set
```

A function whose parameter has a set type is **expanded into N definitions** (one per
member of the set) with a common body; the call is resolved by the type of the argument, as with explicit overloading:

```python
sq(x:combi):Int64 := { @return :Int64(x) * :Int64(x); };
# equivalent to three definitions: sq(:Int8), sq(:Int16), sq(:Int32)

sq(:Int16(3));    # will select the definition with the parameter :Int16
```

A set can also be specified **inline** — directly in the type position (the sigil `:` goes immediately before `(`):

```python
sq2(x:(:Int8 + :Int16)):Int64 := { @return :Int64(x) * :Int64(x); };
```

The same named set can appear both in a parameter and in the return — then the definitions
are matched **positionally** (the i-th member of the set — for both the parameter and the return):

```python
combi ::= :Int8 + :Int16;
idk(x:combi):combi := { @return x; };   # 2 definitions: (:Int8)->:Int8, (:Int16)->:Int16
```

**Rules and restrictions:**

- In a prototype, **no more than one distinct set** is allowed (otherwise — an error about combinatorial
  complexity); the same set can be used in several positions.
- A set **only in the return** is not allowed: the overloads would differ only by the return type (in C++
  there is no overloading by return). Use the same set in the parameter too.
- An inline set is **only a type**, not an expression (it cannot be assigned/passed as a value).
- The exclusion `-` is **not yet implemented** (an explicit error).
- The members of a set can be simple types (template types in a set are not yet supported).

At the syntax level, *TrustLang* supports several kinds of functions.

## Ordinary functions {#func}

An ordinary function is not limited in the C/C++ sense: it can contain conditions, loops, calls of other
functions, etc. It is created by the operator `:=`:

```python
hello(str:StrChar) := {
    @print("call: {}\n", str);
};

@main() := {
    hello("Hello, world!");
};
```

## Named and optional parameters {#named-params}

**Named parameters** allow passing arguments by name (with the prefix "."), rather than by position:

```python
func(width:Int32, height:Int32) := { ... };

func(.width=640, .height=480);
func(.height=480, .width=640);  # the order does not matter
```

The name of a named argument is not shadowed by a preprocessor macro (see
[Naming of objects](../syntax/naming/)).

**Optional parameters** have a default value and can be omitted when calling: the name of the argument
is separated from the value by the sign `=`. First the mandatory parameters are listed, then those with default
values. A function that allows an arbitrary number of arguments ends the parameter list
with an ellipsis `...`.

> **Current implementation limitation:** a default value cannot reference another parameter (a C++
> restriction for default arguments); named arguments for generic functions are not supported, and
> named arguments for native functions/constructors are an explicit diagnostic. A default expression
> is emitted as an ordinary C++ expression (without overflow wrappers)
> (see [Status](../status/)).

## Pure functions {#pure}

A pure function has no side effects: it returns a result only from its arguments and does not
change the state of the program. Purity is expressed by declaration **attributes**, not by a
dedicated syntax: `@[func_pure@]` → C++ `__attribute__((pure))`, `@[func_const@]` →
`__attribute__((const))`, `@[constexpr@]` — compile-time evaluation; automatic **checking** of
purity by the compiler is not implemented (see [Status](../status/)). The notion of purity also
includes the requirement of immutability of arguments by reference (see
[Naming of objects](../syntax/naming/)). The `:-` operator is a name alias and `::-` is a type
alias (see [Module hierarchy](../syntax/hierarchy/#func-alias)).

## Generic functions {#generic}

A function can be parameterized by types: `<T> identity(x: T): T := { @return x; };`.
Type arguments are deduced from the actual call arguments; explicit type arguments in the call
syntax are not provided. C++ code is generated as `template <typename T>` with explicit
substitution of the deduced arguments (`c_identity<int32_t>(...)`).

A templated **native import** declares a generic alias to a C++ function template:
`<T> run(t: AsyncTask<:T>): T := %trust::sync_wait...;` — no C++ function is emitted, the call
is rewritten to `trust::sync_wait(...)`, and the result type is deduced by unification.

### Type packs (`<T...>`) {#packs}

A function can accept a variable number of types: `<T...>`. The pack parameter is declared as a
trailing `xs: T...`; C++ code is generated as `template <typename... T>` and `T... xs`.
A call substitutes concrete types: `count()` → `c_count()`, `count(1:Int32)` →
`c_count<int32_t>(1)`, `count(1:Int32, 2:Int32)` → `c_count<int32_t, int32_t>(1, 2)`.

C++ `std::tuple` is **not a built-in synonym**: it is declared by the user/library as a native
class template:

```trust
@include("tuple")
<T...> StdTuple ::= %std::tuple<T...> { };

<T...> makeStdTuple(xs: T...): StdTuple<T...> := %std::make_tuple...;
```

`StdTuple<...>` is an FFI type (`std::tuple<...>`); it does not have the features of the built-in
`Tuple(...)` (element names, literal, destructuring). The built-in `Tuple` is not a template:
`Tuple<...>` is an error.

Current limitations:

- generic methods, generic operators and generic lambdas are not supported (explicit error);
- using a generic function as a value (function pointer) is not supported;
- a type parameter that appears only in the return type cannot be deduced (error);
- arithmetic/field access on `T` without constraints is not supported;
- only one trailing pack is supported; non-trailing packs, zip of several packs, fold/sizeof,
  pack values in a function body and pack parameters in class templates are not supported
  (explicit diagnostic);
- `Array<T...>` is invalid: `Array` is parameterized by a type and a dimension.

## Coroutines {#coro}

A coroutine is a closure function capable of suspending and resuming execution (cooperative
multitasking). It is **not implemented** in the current version — see [Status](../status/) and the section
[Multithreading and asynchrony](../safety/concurrency/).

## Anonymous functions {#anonymous}

An anonymous function is declared at the place of use and does not get its own name (a unique
identifier). In *TrustLang* an anonymous function is specified by a [lambda expression](#lambda) and
represents a **value of a functional type** `std::function<...>`: it can be saved in a
variable, passed as an argument (a callback function, a predicate) or called in place.

## Lambda expressions (closures) {#lambda}

A lambda expression declares an anonymous function at the place of application:

```python
[ captures ] (params) : Ret { body }
```

- **`captures`** — the capture list: only **names** of previously declared variables, **by value**
  (a copy). Empty brackets `[]` — no capture. A reference capture `[&x]` is **not
  supported** in the current version (an error diagnostic).
- **`params`** — parameters like those of an ordinary function (`x:Type`, the type is optional).
- **`: Ret`** — the return type; if absent, it is inferred from the body.
- **`body`** — the code block `{ ... }`; the value of the block is the result of the lambda.

A lambda value is typed by a functional type and can be stored/passed. Returning a value —
as in an ordinary function (`@return` / the named form); a lambda internally has its own return
label:

```python
f := [](x:Int32):Int32 { @return x * x; };
y := f(5);
```

### Immediate call {#iiife}

A lambda can be called immediately after its definition. The canonical form is the **wrapped** one:

```python
y := ( [](x:Int32):Int32 { @return x * x; } )(5);
```

The direct form `[](x){... }(5)` (without wrapping) is **not supported** and produces an error with a hint
to use `( lambda )(args)`.

### Not supported (first iteration)

A reference capture `[&x]`, an implicit capture `[=]`/`[&]`, a capture with a value/type
(`[x = expr]`, `[x:Type]`) and a capture of non-copyable values (exclusive ownership `unique`) —
are **not supported** and produce an explicit diagnostic. "A closure by definition cannot be a pure
function" — purity checking for closures is not implemented (purity is set by declaration attributes).

## Redefinition and overloading of functions {#redefinition}

Since *TrustLang* allows calling native C/C++ functions directly and making
[C++ code inserts](../syntax/concept/#block), the redefinition and overloading of functions have their own specifics.

- **Overriding** — the implementation by a subclass method of a method declared in the superclass
  (a fundamental principle of OOP).
- **Overloading** — the creation of several functions with the same name but a different set of
  parameters; the required variant is selected at compile time by the parameters.

### Overriding {#overriding}

Overriding is allowed not only for class methods, but also for ordinary functions (a mechanism similar
to `LD_PRELOAD`, implemented by the means of the language itself). For a function to be overridable, an
immutability attribute must not be specified at its creation. If a superclass declared a method mutable
(virtual), and a subclass implements it with an immutability attribute, this is equivalent to `override final`
(without further overriding):

```python
base::func() := { ... };   # virtual
class1::func() = { ... };  # override
class2::func^() = { ... }; # override final
```

Overriding goes **by the function name** without taking arguments into account, however the new function must have
arguments [compatible](generics/) with the argument types of the original function.


### Overloading {#overload}

Overloading — several functions with the same name and **different signatures** (a set of parameter types);
the required variant is selected **statically** at compile time by the types of the arguments. An exact duplicate
of a signature is an error; if there is no suitable variant — the `no matching overload` error; if several
variants are incomparable — `ambiguous`. Overloading on the return type alone is impossible (C++ cannot
distinguish such functions): such a declaration is diagnosed as a duplicate.

Operators are the same kind of system functions (`__eq__`, `__getitem__`, `__call__`, ...): their
signature sets are resolved by the same algorithm (see [Operator overloading](class/#operators)).

Overloading of **free functions and methods** of user classes/Struct is supported:

```python
f(a:Int32):Int32 := { @return a + 1; };
f(a:Int64):Int32 := { @return 100; };

@main() := {
    x : Int32 := 5;
    @print("{}\n", f(x));        # f(a:Int32) selected
    @print("{}\n", f(7:Int64));  # f(a:Int64) selected
};

:C ::= :Class{
    base:Int32 := 10;
    f(a:Int32):Int32 := { @return a + 1; };   # overloading of a METHOD
    f(a:Int64):Int32 := { @return base + 90; };
};
```

The variant is selected by the types of the arguments: an exact match is preferable to a numeric widening
(promotion/conversion); a parameter of `:Any` accepts any type (the lowest priority); with several
incomparable best ones — `ambiguous`.

`:Any` is a **concrete type** (`std::any`). A `:Any` parameter accepts any type with the lowest
priority; an `Any`↔`Any` match is exact. An `Any` value is **not** implicitly converted to a concrete
parameter (such a pass is an overload-resolution error). Explicit call-site conversion is the typed
construction `:T(expr)` / `(expr):T`: for an Any operand the code generator emits `trust::any_to<T>`
(a runtime check; a mismatch throws, never a silent fallback). In a generic function a bare type
parameter (`<T> f(x:T)`) may be deduced as `:Any` (`T := Any`); for a structural parameter (`Array<T>`)
an `:Any` argument does not match structurally.

Overloading (the selection of a signature and the admissibility/casting of arguments) for functions and methods is
**always performed by the TrustLang compiler** — even with a single signature too. For native **methods**
the analyzer checks both the arity and the argument types; for a native **function** with a single
signature the argument conversion is performed by C++ (overloads of native functions are resolved
by the analyzer — see below).

Overloading of native targets is limited by the linkage model. A target without `::` (`%name`) has C linkage
(`extern "C"`) — a C symbol has a single C++ name, so **overloading such a name is forbidden**
(an explicit compile-time diagnostic instead of invalid C++). A target with `::` (`%lib::name`) is overloaded
naturally: the analyzer selects the signature (for the result type and argument checking), and
C++ selects the concrete overload from the same argument types — the symbol name is NOT mangled:

```trust
parse(x:Int32):Int32 := %mylib::parse...;    # C++ symbol (C++ linkage)
parse(x:StrChar):Int32 := %mylib::parse...;  # overload of the same C++ name
```

### C++ names and signature identity {#signature-identity}

The C++ name of a function/method is derived from the **declaration identity**: `c_<name>` + a
deterministic suffix of the positional signature (builtin representation aliases are resolved, a
user alias stays nominal — see below; template parameters are encoded by their position in the
declaration list). The suffix is a projection of the SAME segment identity used by duplicate checks
and overload selection (one implementation: `FunctionIdentity`). The suffix is applied **always**
(except the entry function, operators and native names), therefore:

- overloads do not depend on whether the name is overloaded in the current file/scope;
- a forward declaration and the definition of one function get one C++ name; a consistent rename of
  a template parameter (`<T> f(T)` → `<U> f(U)`) is allowed;
- declarations that differ in the positions of the used template parameters (`<T,U> g(T,U)` and
  `<U,T> g(T,U)`) are different overloads;
- a template parameter that does not occur in the signature is diagnosed (it cannot be deduced at a call).

Positional canonicalization matches the C++ `[temp.over.link]` rule: template parameter **names** are
not part of the identity, so a consistent rename (`<T> f(T)` ↔ `<U> f(U)`) is one declaration; use
**positions** are, so `<T,U> g(T,U)` (pattern `(1,2)`) and `<U,T> g(T,U)` (pattern `(2,1)`) are
different function templates (C++ also treats them as distinct overloads; the suffix additionally gives
them distinct C++ names).

**A user alias (`::=`) is a separate Trust type.** In C++ it is implemented with `using`, but for
overloading and identity it is a **new nominal type**: `f(a:MyInt)` and `f(a:Int32)` are two different
overloads (they get different C++ names). Therefore a call without an explicit annotation/cast may be
ambiguous: the literal `1` has type `Int8`, and promotion to `MyInt` and to `Int32` has the same rank
(`ambiguous` diagnostic). Assignment and argument passing between an alias and its base are allowed
as a representation conversion (numeric — by the underlying type). Builtin aliases (`Integer`, `String`, `Byte`, …) are representation aliases and
are transparent. For **native** C++ names (`%lib::f`) aliases are indistinguishable (external C++):
two such declarations with the same C++ signature produce a `duplicate declaration` diagnostic.

Native C++ names (`%lib::f`) are not mangled: C++ selects the concrete overload, so the call
arguments are cast to the declared parameter types.

