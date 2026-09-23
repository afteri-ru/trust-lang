---
title: Code hierarchy
tags: [syntax, namespaces, naming, modules]
description: Namespaces, modules and packages
weight: 40
---

## Namespaces

*TrustLang* supports namespaces; the separator, as in C++, is the double colon `::`.
A namespace can be specified both for an individual identifier and for a whole
[code block](../operators/#block).

Unlike `namespace` in C++, namespaces in *TrustLang* serve not only to organize code into
logical groups and to avoid name conflicts: a name in which a namespace is explicitly specified refers
to **static** objects (memory for which is allocated at compile time).

A global name cannot be shadowed by a macro or a local variable during
[name lookup](naming/#name-lookup). To create a global (static)
variable, it is enough to write its name with the global qualifier `::var`. The namespace for
a code block is specified before the opening curly brace (`ns:: { ... };`).

```
::var := 0;    # global variable (not shadowed by a macro/local)

ns:: {          # namespace ns
    a:Int32 := 2;   # ns::a
    sub:: {         # nested namespace
        c:Int32 := 3;   # ns::sub::c
    };
};
```

## Modules

*TrustLang* implements program modules and packages: the idea of hierarchical file placement in
file-system directories (as in Python), but the name separator is not a dot, but a backslash `\`.

A module name may contain only lowercase English letters, digits and the underscore character (except
the first and last positions). The restriction is due to the direct mapping of module names onto
file-system objects: different file systems may have different encodings and case requirements.

A module is understood as a **file** with source code (the `*.src` extension). Modules store frequently used
functions, classes, constants, etc. They are conventionally divided into modules and programs: programs are intended for
direct launch, modules — for import into other programs (functionally they do not differ).

### Module objects {#thread-local}

All objects defined inside one module without specifying a global namespace are visible
only within the current file and in modules connected after its definition.

The lifetime of static and local variables of the top level of a module is the same and is limited by the lifetime
of the module itself, but for multithreading they differ: a **static** module variable always
exists in a single instance for all threads, while a **local** module variable is its own for
each thread (an analog of `thread_local` in C++11).

### Importing modules {#import}

A relative module name starts with one `\` character and points to a file relative to the current
module. An absolute name starts with `\\` and points to a file relative to the directory of the current
executable file (or the module search directories, which can be overridden, for example, by
command-line arguments).

The simplest way to import a module is to write its name with parentheses as a function call; in the parentheses you can
pass module initialization arguments, the list of imported functions, etc. If the module name is preceded
by the preprocessor character (`@\dir\module()`), the module imports not only code but also its macros
(only with static loading).

Modules can be loaded statically or dynamically (by analogy with static and dynamic
linking of libraries):

- `\dir\module()` — static loading by relative path;
- `@\dir\module()` — static loading with macro import;
- `\\root\dir\module()` — static loading by absolute path;
- `\\( "dir\file" )` — dynamic loading at runtime (with dynamic linking
  macros cannot be imported).

With dynamic loading, the compilation of the module and all checks are performed at application runtime;
static loading allows detecting possible errors at compile time.

Besides import by source text, TrustLang describes a **semantic ABI** — a way of
representing a compiled module in which binary symbols are bound to a typed
semantic definition, while the matching of objects is performed by the language compiler. The principle
is described in [Semantic ABI of modules](../sabi/).

## Packages {#package}

A package is a directory that includes other directories and modules and contains an additional file
`__init__.src`. Packages are used as a complement to namespaces and allow working with modules
through specifying the nesting level. Unlike Python and Java, where modules/packages *replace* namespaces,
in *TrustLang* the modular structure and namespaces are used simultaneously and, when specifying a full
name, are combined: `\root\dir\module::ns::name::var`, where `root` and `dir` are directories relative to
the current module, `module` is the file name (`root/dir/module.src`).

The file `__init__.src`, unlike Python, cannot be empty — it must explicitly load
the modules that belong to the package:

```
\simper();
\compper();
\annuity();
```

Package import uses the same syntax as module import. An example of the package `fincalc`
(`__init__.src` + modules `simper.src`, `compper.src`, `annuity.src`):

```
\fincalc();                 # import the whole package
\fincalc\simper();          # import a single module
sp := \fincalc\simper();    # import a module with an alias
\fincalc\simper(__import__="fv");  # import a specific function
```

## Namespaces and classes

In *TrustLang*, decoration (mangling) of names by argument types is not used. Class methods
(when OOP is implemented) get unique identifiers by analogy with Python: a global
function with the class and method names joined through `::` (for the class `:NewClass` and the method `method` —
`NewClass::method`). This makes it possible to define class methods outside the class body by specifying the desired name in the
namespace. Full-fledged OOP is not implemented in the current version (see [Classes](../types/class/) and
[Status](../status/)).
