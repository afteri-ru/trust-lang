---
title: Semantic ABI
tags: [architecture, modules, abi]
description: >
    The principle of ABI formation, name mangling in binary files and the principles of linking and loading modules
weight: 75
draft: true
---

*TrustLang* is transpiled to C++ and built by an external compiler, therefore for "ordinary"
native objects the ordinary C/C++ linking works (see [Native types](types/native/)).
However, for **modules in TrustLang itself**, the language uses its own way of binding —
the **semantic ABI**: binary symbols are bound not only to an address, but also to a
**semantic definition** of the object in the Trust language, and the restoration of the type and the matching
are performed by the **language compiler**, not by the linker.

Building a `.trust` module (`trust -m`) and viewing the export table (`trust --module-info`) are already
implemented; loading/importing already compiled modules is **not** yet (import works only by
the source text `.src`). The current boundaries — in [Status](status/).

## Why a semantic ABI is needed {#why}

The classic ABI (as in C++) encodes types in the symbol name (decoration/mangling). This
works only for a predefined set of types and rules for naming them. TrustLang
relies on a type system that can be extended, and takes into account properties of objects that are not
directly derivable from the "machine" type (static and dynamic constness, an explicit type
or inference from literals and attributes, access models during synchronization of shared variables, and
so on). Such rules cannot be fixed in name mangling: a decorated name is poorly readable
by a human and may be incompatible between different compilers and systems.

The semantic ABI removes this dependency: the names in the binary file do not encode the type, and the type
is restored from the semantic definition attached to the module. This makes it possible to
extend the type system and not to worry about the internal representation of the type identifier
(TypeID), which is different for each program, and also opens up optimizations and analysis unavailable
with a fixed ABI (which gives only a name, but does not reveal the structure of the object and its
additional properties).

## Formation of the module ABI {#module-abi}

A compiled module carries a **semantic ABI table** — a correspondence between a
binary symbol (the address of an object) and its symbolic definition in TrustLang. An exported
binary symbol has the form of an `extern "C"` name, which points to the namespace, the class/method and
the position in the table: by this name one can obtain the position of the record, and from the record — the address of the object and
the semantic definition.

The semantic definition is a **typed prototype in the Trust language** (a signature with types and
attributes). That is, a module is distributed by **prototypes, not by source text**: this is
enough to import an object from a precompiled module.

### Overloads in the table {#overloads}

An overloaded object (one name — several signatures) is represented in the table by **several
records**: each overload gets its own positional record name, its own address and its own
typed prototype in the `decls` set. The overload names inside the module differ by a
deterministic suffix by signature (a code generation detail, not part of the inter-module contract).
On import, the compiler restores a SET of prototypes by the name and resolves the call by the same
algorithm as inside a single module (an exact match is preferable to a widening; several
incomparable best ones — an "ambiguous" error).

## Name mangling in binary files {#mangling}

Two independent levels of names are distinguished:

- **The export C name** — the name of the binary symbol of the module. It is fixed in form (the namespace,
  class, method, position in the table) and serves as an entry point for matching through the
  semantic ABI table.
- **The internal C++ mangling of code generation** — the transformation of trust names into C++ identifiers
  inside the generated translation units. This is a code generation detail, not part of the inter-module
  contract.

## Principles of linking and loading {#linking}

- The model is **precompiled**: the module is compiled in advance, and on import the compiler
  matches the required name through the semantic ABI table.
- **A linker alone is not enough**: the correct matching of an object by type is performed by the
  *language compiler*. The linker can bind symbols, but not restore semantics and types.
- With a predefined interface, a **parallel `extern "C"` export** of
  trivial objects is allowed — an additional, classic binary contract that does not require a
  compiler.
- Modules can be loaded **statically** (matching at compile time) and
  **dynamically** (at runtime) — see [Code hierarchy](syntax/hierarchy/#import).
- The table and the attached semantic definition give **bidirectional traceability**
  of names: from a symbol to a definition and back, which is used by development tools.

## Open questions {#open}

The exact format of a compiled module, the scheme of table keys/positions and the composition of the semantic
description (which object properties it includes) are implementation details left to separate
tasks.

## Links {#links}

- [Code hierarchy](syntax/hierarchy/) — namespaces, modules, import.
- [Native types](types/native/) — integration with C/C++, ordinary linking.
- [Architecture](architecture/) — the stages of program processing.
