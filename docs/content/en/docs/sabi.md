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

The semantic definition is stored in a dedicated versioned ELF section `.trust.sabi`, which is read
at compile time **without loading the module** (`dlopen` is not required). The section contains a
header (magic, format version, compiler version, source record, record count, compression flag) and
an array of **Trust declarations** of the exports (one per export, `\0`-separated, the block may be
compressed). A declaration is self-sufficient: the name, the typed signature and the right-hand side —
a concrete native symbol name (`%<symbol>`). The compiler **generates the symbol name itself** and pins
it with an `asm` label (an operator's C++ name is not expressible as a Trust name, so the name is
generated from the qualified/C++ name). In a textual module the right-hand side is `...`
(a forward declaration). The kind of the entity (function/variable/…) is derived
from the declaration text. The command `trust --module-info[=<masks>]` prints the version, the
source record and the declarations; name masks (`?`, `*`, `,`) filter the list. The declarations are
validated by the language parser (an invalid section is an explicit error). In the target model, the
module interface is a single canonical Trust text (see [Module interface](#interface)).

### Overloads in the table {#overloads}

An overloaded object (one name — several signatures) is represented in the table by **several
records**: each overload gets its own positional record name, its own address and its own
typed prototype in the `decls` set. The overload names inside the module differ by a
deterministic suffix by signature (a code generation detail, not part of the inter-module contract).
On import, the compiler restores a SET of prototypes by the name and resolves the call by the same
algorithm as inside a single module (an exact match is preferable to a widening; several
incomparable best ones — an "ambiguous" error).

## Module interface {#interface}

The module interface is a **single canonical textual form in Trust itself**: full semantic definitions
of types and declarations (prototypes) **without implementation**. It is printed by
`trust --module-info` and is not a standalone file: the interface is tied to a specific build of the
module (it contains the binding to that build's symbols), therefore it is distributed together with the
module.

The key invariant: **only the implementation is precompiled**. Even for a precompiled module, the
consumer receives **full semantic definitions of all data types**, not merely a "machine" signature.
This allows the language compiler to perform the usual checks (member access, overload resolution,
ownership and borrowing) over the imported entities, although their bodies are absent. ABI/SABI apply
**only to precompiled modules**: for a source module (`.src`) everything is rebuilt from the source
text, and no ABI checks apply to it.

The binding to the ABI is expressed uniformly. A declaration in the interface is an ordinary Trust
declaration (a prototype), and its link symbol is given by the right-hand side `%<symbol>` (an extern
name). The same mechanism serves C/C++ integration: a declaration with `%<symbol>` binds a Trust name
to a native symbol. The compiler **generates** the symbol name and pins it with an `asm` label; for
trivial C/C++ integration the right-hand side may point directly at a native name. The binding is
**stored** in the interface rather than re-derived: name generation depends on attributes, declaration
order and generated names and is not deterministic.

The interface is **order-independent and extensible**: a type (class) is defined once, while its methods
and specializations may appear later as separate declarations (just as class methods are declared
outside the class body). For the analyzer, when loading a module it does not matter whether an entity
was added at the type definition or at the end of the interface: all declarations are merged into a
single model.

### Kinds of artifacts {#artifacts}

| Artifact | Purpose |
|---|---|
| `*.src` | source text of a module/program |
| `*.srcp` | package: an archive with one or more `*.src` (including subdirectories) and metadata (publisher, project link, integrity control, etc.) |
| `*.cppt` | generated C++ code (implementation) |
| `*.hppt` | generated C++ header (ABI interface for C/C++) |
| module | compiled code together with semantics; static or dynamic form |
| application | the final program (the result of compiling code and modules) |

### Single container {#container}

A module is a **single container**: object code plus a section (or sections) with semantics (for a
dynamic module — additionally the dynamic linking configuration). The dynamic form is a shared object
(e.g. `.so`), the static form is a static library; the semantics is read by the compiler **without
loading the module**. A single container eliminates the "code versus semantics" mismatch and simplifies
distribution.

### Two interfaces {#interfaces}

Two **interfaces** are built from a single semantic model of a module — for two different consumers:

| | Trust interface | C++ interface (`*.hppt`) |
|---|---|---|
| Consumer | the TrustLang compiler/analyzer | external C/C++ toolchain |
| Format | canonical Trust text | C++ header |
| Completeness | full semantics (without implementation) | projection onto the binary ABI |
| Output | `trust --module-info` | header export |

The Trust interface is **lossless**; the C++ interface is a **projection**, so some information is not
carried over: attributes, contracts and ownership rules are **dropped** (a C++ consumer does not check
them), while type properties are reflected **only partially** — by what is expressible in C++ (for
example `const` and wrappers such as `trust::Shared`). The boundary of the C++ interface is **anything
expressible in a header file**: declarations, type definitions and definitions of inline/template
entities. In C++, overloads receive unique names, while the binding to the binary symbol still goes
through the `asm` label (a single `%<symbol>`).

### Interface versioning {#versioning}

- A **module container** stores the source record (`srcRecord`) and the compiler version.
- The **C++ header `*.hppt`** (an external artifact) stores the **hash of the module** it was generated
  from and the Trust compiler version: this is an external reference to a specific build (no
  self-reference) and it lets one detect a "header versus library" mismatch. Header generation must be
  deterministic.

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

Settled in principle: a single module container (code together with semantics); two interfaces from a
single semantic model (Trust and C++); the way to bind to the ABI (a single `%sym` as the extern name of
a prototype); the order-independence and extensibility of the interface; versioning (the source record
and the compiler version in the container; the module hash and the compiler version in `*.hppt`).

Still open (implementation details left to separate tasks): the final name and role of the module
extension; the self-containment of `*.hppt` (require runtime headers or embed the needed declarations);
the format and metadata of the `*.srcp` package; combining semantics and object code in one container
versus a separate platform-independent semantic artifact; the module dependency model; the linking and
visibility of implementation symbols; dynamic loading at runtime.

## Links {#links}

- [Code hierarchy](syntax/hierarchy/) — namespaces, modules, import.
- [Native types](types/native/) — integration with C/C++, ordinary linking.
- [Architecture](architecture/) — the stages of program processing.
