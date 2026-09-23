---
title: Naming of objects
tags: [syntax, naming]
description:  Rules for naming variables, functions and data types
weight: 20
---

As names of objects and names of data types you can use letters, digits and underscores in any
combinations, provided that the first character of the name is not a digit.

A name cannot consist of a single underscore, since this is a special term used for service
purposes.

To avoid name collisions you can use a [namespace](/en/docs/syntax/hierarchy/)
and a [modular code structure](/en/docs/syntax/hierarchy/), which *TrustLang* supports simultaneously.
[Overriding](/en/docs/types/funcs/#overriding) and [overloading](/en/docs/types/funcs/#overload) of functions are allowed.

When [creating objects](/en/docs/operators/create/), the identifier name may contain one or several special
characters — qualifiers (or [sigils](https://en.wikipedia.org/wiki/Sigil_(computer_programming))), each of which has a fixed meaning.

### Name qualifiers {#sigil}
- '**@**' — the *at* prefix is used to indicate the name of a [macro](/en/docs/syntax/macros/),
        which is processed by the preprocessor before the syntax analysis of the program source text begins.
- '**$**' — the dollar sign at the beginning of an object name denotes a *temporary* name whose lifetime is
        [limited by the language semantics](/en/docs/safety/memory/#rules).
- '**::**' — the double colon separates [namespaces](/en/docs/syntax/hierarchy/)
        and is a sign of a *static* object whose value is preserved after leaving the current scope.
        If a name starts with '**::**', its scope will be global and it will be accessible from other program modules.
- '**.**' — the *dot* prefix is used when accessing a field of a module or class (it restricts the scope to the current object only).
        The *dot* prefix can also be used when defining or calling a function to indicate the name of an argument
        that cannot be shadowed by a preprocessor macro.
- '**\\**' — the *backslash* at the beginning of a name denotes a [program module name](/en/docs/syntax/hierarchy/),
        and also separates directory names in the hierarchy of program module placement in the file system.
- '**:**' — the colon at the beginning of a term denotes the name of a data [type](/en/docs/types/) or a constructor of a [class](/en/docs/types/class/), which is always *static*
- '**%**' — the *percent sign* prefix is specified for [native names of variables and functions](/en/docs/types/native/)

### Name lookup {#name-lookup}
If an object name contains no [qualifier](/en/docs/syntax/naming/#sigil), it is *simple*.

When *TrustLang* encounters a *simple* object name without a qualifier,
the name lookup algorithm comes into play, which binds the *simple* name encountered in the program source text
to its declaration or the created object.

The lookup of *simple* names *without a qualifier* (*name lookup*, or search for the name of a function/variable) always occurs in a strictly defined order:
- first, the name is searched among macros
- in the case of a function call, static name resolution is performed upon [function overloading](/en/docs/types/funcs/#overload)
- then the name is searched among local objects before objects of the current module
- last, the search is performed among static objects with a gradual expansion of the search namespace from the current to the global one

Such a name resolution order always provides the ability to redefine
global/local objects or function argument names for already existing code without serious changes to it.

For example, for the name `name` in the namespace **`ns`**, the search occurs in the following order:
`@name` → `%name` → `$name` → `ns::name` → `::ns::name` → `::name`,
and for the argument name `arg` only `@arg` is checked:
```python
    ns:: {
        name(arg='value');
    };
```

At the same time, there is always the ability to specify a concrete object regardless of the resolution algorithm of *simple* names.
It is enough to specify the [qualifier](/en/docs/syntax/naming/#sigil) in the object name explicitly.

For example, to refer to the global object **name** from the namespace **ns** of the example above, you must use the full object name `::ns::name`,
and the named argument *'**.** arg'* will not be replaced by the macro `@arg`, if such is defined:
```python
    ::ns::name(.arg='value');
```

### Extension of namespace search {#using}
To specify several namespaces for an extended search when resolving *simple* names *without a qualifier*,
the syntactic construct `... = ns::name, ns::name2;` is used, or `@using(ns::name, ns::name2);` when using [DSL](/en/docs/syntax/macros/).

The search in the listed namespaces is performed in the order they are specified until the end of the current code block,
until the next extended search statement or until the statement `... = _;`,
which cancels the extended search in namespaces until the end of the current code block (until the end of the module).

### Forward declaration {#forward-declaration}
In the program text you can refer only to really existing (created) objects.
But for those cases when you need to refer to an object that is created in another module or will be created later,
you can make a forward declaration, in which the compiler registers the name and type of the object without actually creating it.

A forward declaration allows referring only to static objects (data types),
or local fields of a class, which the compiler does not know about yet but which will be defined later during compilation.

For a forward declaration, the full qualified name is used, which must
exactly match the object name at its subsequent creation.

For a forward declaration, the same syntax is used as for the real [creation](/en/docs/operators/create/) of an object,
only an ellipsis must be specified to the right of the creation operator.

The scope of a forward declaration corresponds to the scope of its placement,
not to the real scope of the object that will be created subsequently.

```python

    # Forward definition of a module variable
    # Applies to the whole module
    var_module:Int32 := ...;

    func() := {
        @return func2(var_module);
    };

    func2(arg:Int32):Int32 := {
        @return $arg*$arg;
    }

    var_module:Int32 := 1;
```


### Argument names, special and system names {#args}
The notation of function argument names is very similar to accessing arguments in bash scripts,
where "**$1**" or "**$name**" is the ordinal number or the name of the corresponding argument.

The reserved name "**$0**" denotes the current object, and the name "**$$**" denotes the parent object.

All function arguments are collected in a single dictionary with the special name **$\***

The immutable virtual compiler variable "**$^**" contains the result of the last statement or code block.


### Immutable objects {#immutable}
TrustLang implements dynamic [immutability](https://en.wikipedia.org/wiki/Immutable_object).
This is a property of a lexical object, not a property of a data type, and means
that an object can become immutable not only at the moment of creation,
but also in the future during one of the assignments/mutations of the value.

To give an object the property of immutability (inability to change its value further in the program text),
the character '**^**' *caret* (roof/house) is used after the object name.

```python
    val := 0; # Create mutable variable
    val = 1; # Set new value 
    val^ = 2; # Set unchangeable value 
    val = 3; # Error  !!!
```

The immutability property of function arguments depends on the type of the function.
For [pure functions](/en/docs/types/funcs/#pure), arguments passed by reference (owning variables) are always immutable.
The mutability property of the remaining arguments is specified when defining the function on a common basis:

```python
    func( arg1^:Int32, arg2:Int32) := {
        arg1 = 1;    # Error (arg1 - immutable)

        arg2 = 1;    # OK
        arg2^ = 2;    # OK - set immutable value
        arg2 = 3;    # Already error
    }
```
### Immutability of calls and attributes

The immutability attribute can also relate to function calls. Pure functions and forced compile-time evaluation (`::-`, `cube^(...)`) are **not implemented** in the current version — see [Functions](../types/funcs/) and [Status](../status/). Object attributes (including the real form `@[name(...)]` before a declaration) are described in the section [Macros](macros/) and [Interaction with C/C++](../types/native/).

