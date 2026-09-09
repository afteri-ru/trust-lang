---
title: Creating objects and assignment
tags: [operators, types, assign]
description: >
    Object creation and assignment operators, destructuring
weight: 10
---

<!--
##### Object scope != object lifetime {#important}
An object *always* has a name, even when the object itself is not physically created yet.
The scope of a *name* is determined by its location in the program code (code block, namespace, module, etc.).
An object may exist but be inaccessible from the current scope, for example, shadowed by another object with the same name.

The lifetime of a local/automatic (i.e. temporary) object is always limited by the scope of the current code block,
while the lifetime of a static object is limited by the lifetime of the module in which it is defined, or by the lifetime of the program if the object is global.
But *initialization of the initial value of a static object* occurs in accordance with the executed program code,
(i.e. when executing the code with the statement creating the static object).
-->
### Creating objects {#create}

TrustLang uses several operators to create objects:

- "**:=**" — creates or assigns a value to a **variable/function** (an object by value).
                        If an object with such a name does not exist yet, it is created; otherwise the value is reassigned.
                        Example: `x := 5;`, `hello(str) := { ... };`.

- "**::=**" — declares a **type** (an alias or a typed type), for example `MyInt ::= :Int32;`,
                        `Status ::= (OK=0, ERROR=1, BUSY=2,):Enum;`. Using "**::=**" to create
                        values/functions is impossible — the compiler reports: *"cannot assign a type ... to a value variable; use '::=' to declare a type alias"*.

  Also, a **forward declaration of a native class** is declared via `::=`:
  `String ::= %std::string { %size(): UInt64 := ...; };` — the RHS is the class name (native — with a leading `%`),
  the body `{ ... }` — interface members (methods/fields/constructors), each `:= ...` (without a body). Constructing
  an object — calling the class name: `s: String := String('hello');` → `std::string("hello")`. More — in
  [Native types](../types/native/).

The operators "**::-**" and "**:-**" are not implemented in the current version; earlier they were intended
for pure functions and compile-time computed values.

### Assigning a new value {#assign}

The operator "**=**" is used to assign a new value to already existing objects.
If an object with the specified name is absent, a *compilation* error occurs.
<!--
~~If a [function](/en/docs/types/funcs/) or a [class method](/en/docs/types/class/) is overridden,
the old value is not *removed*, but a stack of overridden names is created (a kind of table of virtual compile-time names),
and the old (previous) object can be accessed from the new function by the system name "**$$**".*~~
-->

#### Value swap operator {#swap}
The operator "**:=:**" (swap) does not create new variables, but only exchanges their values with each other.
The variables must have identical/compatible data types, or have the type **`:None`**, i.e. **"`_`"**.
It is used to implement the copy-and-swap idiom, since no errors can occur when the operator is executed.

In the current implementation `:=:` is an exchange/move intrinsic (used as a statement; the result is
`void`): `a :=: b` expands to `std::swap(a, b)` (the types must be compatible, not necessarily
references; exchanging incompatible types is an error with a diagnostic); the form `var :=: _` — moving the value
into discard: the move is performed REALLY (a temporary is materialized, which is immediately destroyed),
therefore `var` remains in a moved-from state, and for owning references (`unique`/`shared`) the resource is released.
See also ["Reference types"](../safety/memory/).

#### Append element operator {#append}
The operator "**[]=**" — an analog of `push_back`: it appends a new element to the container on the left, increasing its size by one.
General rule: `X []= v` is equivalent to `X.push_back(v)`.

- For a **dictionary** (`Dict`, including positional "arrays"), an unnamed append adds a positional element: `d []= 999`.
- For **strings**, it appends a character/string: `s []= 'd'` → `s.append("d")`.
- The right object of the operator must be compatible with the type of a single element of the left object
  (string width: the narrow `'...'` / the wide `"..."` must match the type of the string container).
- A nested LHS (`d['x'] []= v`, `d[0] []= v`, `d.field []= v`) — not yet implemented (compilation error).
- **Dictionary merge** via the expansion operator `...`: `d []= ... dict2` or `d []= ... (a=1, b=2,)`
  adds **all elements** of the operand dictionary to the target (an analog of `extend`/`update`); the size of the target grows
  by the number of elements (for a literal and a dictionary with a known size — it is checked statically).

```python
    d := (1, two=2,);
    d []= 999;        # add a positional element → size 3
    d []= 'x';        # add one more element
    d []= ... (a=100, b=200,);  # merge a literal → size 5 (a and b added)
    d2 := (three=3, 4,);
    d []= ... d2;     # merge a dictionary variable → size 7

    s := 'abc';
    s []= 'd';        # the string became "abcd"
    @print('{}', s);   # > abcd
```

Adding a **named** element of a dictionary is performed by the assignment operator `d[name] = value` (set/add by key).

*For example, when defining the class `:NewClass2`:*
```python
    :NewClass ::= :Class{       # Base class
        field1:Int32 := 1;      # class field
        field2:Int32 := 2;      # class field
        method1():Void := {};   # method
    };

    :NewClass2 ::= :NewClass{   # inheritance
        field2:Int32 := 2;      # field field2 of the base class
        method1():Void := {};   # method override (override)
    };
```

```python
    $var := 99; # Create a temporary variable $var
    {
        $var := 100; # The new variable shadows the higher-level $var
        @print('{}', $var)    # > 100
    }
    @print('{}', $var)    # > 99
```

### Assigning a value to several variables at once and the dictionary unpacking operator {#expand}
*TrustLang* supports the operation of assigning a value to several variables at once,
which must be listed comma-separated to the left of the assignment operator.

On the right side of the assignment operator there can be only one value,
and to exchange the values of two variables, instead of writing `a,b = b,a;`
you must use the [swap](#swap) operator:
```python
    a :=: b;
```

As the right operand in the assignment operator, it is allowed to use the dictionary unpacking operator **...** (ellipsis),
which can also be used when passing arguments to a function.

<!--
## Filling and repeating values {#comprehensions}

The family of the **closing ellipsis** in a list of values. It is allowed only as the last element and
works uniformly in two positions:

- **array literal/construct** — the number of positions defines the sized type (`Int32[N]`);
- **call arguments** — the number of positions equals the number of parameters of the callee (the same for **functions
  and methods**: `obj.method(1, ... 2 ...)`).

Forms:

- `... expr ...` — fill the remaining positions with the value `expr`, **recomputing** it for each
  position:
  ```python
      v:Int32[10] := [2, 3, ... 42 ...,];   # [2, 3, 42, 42, 42, 42, 42, 42, 42, 42]
      b:Int32[8]  := [1, ... next() ...,];  # next() is called for each position

      @func sum(a:Int32, b:Int32, c:Int32):Int32 { @return a + b + c; };
      sum(5, ... 7 ...);                    # sum(5, 7, 7) = 19
  ```
- `...` (without an operand) — cyclically repeat **all previous** values up to the known number of
  positions (the incomplete last cycle is truncated):
  ```python
      a:Int32[4] := [1, 2, ...,];           # [1, 2, 1, 2]
      w:Int32[8] := [1, 2, 3, ...,];        # [1, 2, 3, 1, 2, 3, 1, 2]
  ```

The list expansion is performed by the compiler (the number of positions is known from the target type/signature), therefore
the operator does not affect C++ generation — the generated code contains an ordinary list of values.

Compilation errors: the known number of positions is missing (the literal has no sized target type,
the callee is variadic or its signature is unknown); the ellipsis is not last; there are more explicit elements
than positions; there is nothing to repeat (`...` without previous values); the operand type is incompatible with
the target position.

The filling of data when creating sized tensors and the operators of expanding a range/dictionary
(`...`) during initialization and when passing arguments to a function are described in the section
[Ranges, arrays and tensors](../types/containers/).

-->

