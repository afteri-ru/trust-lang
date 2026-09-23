---
title: Functions
weight: 6
tags: [types]
---

A function is defined by the [object creation operators](../operators/create/); the function name must
comply with the [naming rules](../syntax/naming/) and end with parentheses. The body of an ordinary
function is a [code block](../operators/#block) located in the namespace of its definition.

The type is specified for the arguments and for the return type (`arg:Type`).

## Sets of allowed types {#type-sets}

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
    @print('call: {}\n', str);
};

@main() := {
    hello('Hello, world!');
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

## Pure functions {#pure}

A pure function has no side effects: it returns a result only from its arguments and does not
change the state of the program. The declaration syntax `::-` is **not implemented** (see [Status](../status/));
the notion of purity also includes the requirement of immutability of arguments by reference (see
[Naming of objects](../syntax/naming/)).

## Coroutines {#coro}

A coroutine is a closure function capable of suspending and resuming execution (cooperative
multitasking). It is **not implemented** in the current version — see [Status](../status/) and the section
[Multithreading and asynchrony](../operators/concurrency/).

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
function" — the pure function (`::-`) is not yet implemented in the language.

## Redefinition and overloading of functions {#redefinition}

Since *TrustLang* allows calling native C/C++ functions directly and making
[C++ code inserts](../operators/#block), the redefinition and overloading of functions have their own specifics.

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
variants are incomparable — `ambiguous`.

Overloading of **free functions and methods** of user classes/Struct is supported:

```python
f(a:Int32):Int32 := { @return a + 1; };
f(a:Int64):Int32 := { @return 100; };

@main() := {
    x : Int32 := 5;
    @print('{}\n', f(x));        # f(a:Int32) selected
    @print('{}\n', f(7:Int64));  # f(a:Int64) selected
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

Overloading (the selection of a signature and the admissibility/casting of arguments) for functions and methods is
**always performed by the TrustLang compiler** — even with a single signature too. Delegation to the
C++ layer is applied **only to native** functions/methods (their types are specified explicitly as native, and the name
of the symbol is fixed by the external library).

Overloading is still possible for [native](../types/native/) functions too (all argument types
are specified explicitly and are native types); native functions are also available for a direct call
from C/C++ languages:

```python
%func(arg:Int8):None := {};   # void func(int8_t)
%func(arg:Byte):None := {};   # void func(uint8_t)
%func(arg:Int64):None := {};  # void func(int64_t)
```

