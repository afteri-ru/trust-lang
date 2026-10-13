---
title: Code hierarchy
tags: [syntax, namespaces, naming, modules]
description: Namespaces, modules and packages
weight: 40
---

## Namespaces

*TrustLang* supports namespaces; the separator, as in C++, is the double colon `::`.
A namespace can be specified both for an individual identifier and for a whole
[code block](concept/#block).

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

## Modules and groups

*TrustLang* organizes source text into **files** grouped into **groups** (directories) and
**package archives**. The path separator is a backslash `\`; a dot is not used in this role.

**File** — a unit of source text (`.src`), AST, semantics, import and interface.
**Group** — a first-level directory in a project or package that groups files. A group is not an
AST/semantic unit and does not create its own namespace: it is a path and file affiliation.
**Package** — one `.trust` archive with a `trust.toml` manifest; a distribution unit. A package does
not contain importable nested packages: a `.trust` inside a package is an ordinary file/resource.
**Program** — the entry file; it may have no header.

Path components use only lowercase Latin letters, digits and `_`; `_` cannot start or end a
component. The restriction follows from the direct mapping of names onto the file system.

### File, group and import terminology {#module-terms}

- **File** — identified by its **logical path** (relative to the project or package root), not by the
  header and not by the absolute cache path. The header (if present) must match that path and the file
  name: this is package-composition control.
- **Group** — a directory grouping; not an AST/semantic unit. An import always targets a **file** or a
  **group of files**; a group cannot be imported as an object.
- **File import** — `path()`; **group import** — `dir-path()`: it expands to importing all `.src` files
  **directly** in the directory (non-recursive). A subdirectory needs its own `dir-path()`.
- **Header** — the first significant line `path;` (after BOM, shebang, `#` comments and `/* */`). The
  header is static: macro expansion is not applied; it is not part of the AST.
- **File interface** — exported identifiers and their declarations; it can be specified explicitly by
  a header block (see [File interface](#file-interface)). Masks define the set of VISIBLE names.
- **Source form** — a regular `.src`; **compiled form** — the same `.src` with bodies replaced by
  forward declarations and native imports.

### Path prefixes and the file header {#roots}

The prefix type is specified **only in the first position**; all subsequent separators are a single `\`.

| Prefix | First component | Example file | Meaning |
|---|---|---|---|
| `\` | file | `\hello` | a file in the project root |
| `\\` | group | `\\util\string` | a file in group `util` |
| `\\\` | package | `\\\tensor\tensor` | a file in package `tensor` |

Path forms:

- `\file` — a file in the project root;
- `\\group\file` — a file in a group; `\\group\dir\file` — a file in a group subdirectory;
- `\\\pkg\file` — a package root file; `\\\pkg\group\file` — a file in a package group.

The file header matches its logical path:

| File | Header |
|---|---|
| `hello.src` in the project root | `\hello;` |
| `util/string.src` | `\\util\string;` |
| `tensor/tensor.src` in package `tensor` | `\\\tensor\tensor;` |
| `tensor/math/matrix.src` | `\\\tensor\math\matrix;` |

The header is mandatory for imported files and for files of groups/packages. It is optional only for
**the root program** (`\file`); a program inside a group/package must have a header. Importing a
headerless file is an error.

### File interface {#file-interface}

The exported set of a file can be specified explicitly — by a block in the header:

```
\lib {
    f(x:Int32):Int32 := ...;      # declared here, defined in the body
    counter:Int32 := ...;         # the same for a variable
    :Point ::= :Struct{ x:Int32 := _; };   # public type — its full definition is here
};
```

Rules:

- the block is part of the **first significant construct** of the file (after the header path) and ends
  with `};`;
- the **export set of the file** (import masks and forward declarations) contains only the declarations
  of the block; the other top-level declarations are **not part of it**: they are not visible to the
  importer, are registered under a module-qualified key and are emitted into a C++ anonymous namespace
  (identically named private names of different files do not conflict). A type definition completing an
  interface forward is the owner's implementation: it is not published to the client;
- a **function/variable** declaration with `...` **must** be defined in the same file (otherwise it is
  a compile error); `...` means "forward declaration", `_` means "not initialized" (not used in the
  interface);
- a **type** declaration with `...` (forward) is an ordinary forward declaration: the name is
  exported, but no representation is visible at this point. A full type definition is given by a
  definition in the interface block (`:T ::= :Struct{...};`); then the client uses the type by value.
  A body definition behind an interface forward is the owner's implementation: it is not published
  to the client, so the type stays incomplete (non-owning `ptr`/`ref` only; by value it is a compile
  error);
- **an incomplete type for the client is a forward in the export set**, not a separate marker. A full
  type definition directly in the interface block makes the type complete. An external C/C++ type is
  described by a native class forward (`:T ::= %cpp_name { };` + `@[include]`);
- namespace blocks (`ns:: { ... }`), attributes (`@[...@]`) and **load operators with parentheses**
  (`\lib(a, b);`) are allowed inside; such an import means **re-export**: the name set filtered by
  masks joins this file's export set (name ownership does not change; the file's header gets
  `#include`s of those modules' headers). Re-export is **transitive**: a facade may re-export names of
  another facade — the owner is still the original file. **Function aliases** (`f :- \lib::g;`, see
  [Function aliases](#func-alias)) are declared here as facade curation: the client sees the new name.
  A load/re-export operator is allowed **only at the top level** of the interface block; an import
  inside a nested namespace block (`ns:: { \lib(f); };`) is an error (qualified re-export semantics are
  not defined). Non-public blocks (`__::` — private, `_::`/`_{...}` — protected) are forbidden at any depth, and `\path;` without
  parentheses is not a load. Of macros, only those defined outside modules (DSL/keywords) are allowed: the interface must not
  depend on who imports the file, so a macro defined in a module (including one inherited from the
  importer) inside the interface block is a compilation error;
- the entry function (`@main`) and system names do not need to be listed;
- a file without a block exports all top-level declarations.

### Importing files and groups {#import}

- `\path()` imports one file; masks filter that file's exports.
- Parentheses are **mandatory**: a bare first significant line `\path;` is the file header (see above),
  not an import.
- `\\group()` / `\\group\dir()` import all `.src` files **directly** in the directory (non-recursive);
  a subdirectory needs its own `\\group\dir()`.
- `\\\pkg()` imports first-level groups (their direct `.src`) plus the package root `.src` files.
- The group order is lexicographic; an empty group is an error; group masks apply to the union of
  exports.
- If `dir.src` exists, `dir()` is a file import; otherwise, if directory `dir/` exists, it is a group
  import; if both exist, it is an ambiguity error. For an explicitly group path `\\dir()`, the file
  `dir.src` is not allowed: a directory is expected (otherwise — the “module directory expected” error).
- An import is **positional**: a name is visible only after the import operator (single-pass semantics —
  declaration before use). An import may appear in any block and applies to the current scope.
  Imports are not hoisted to the start of a block: place them before the first use of imported names.
- **A cyclic dependency between files is a compile error** (the diagnostic shows the import chain).
- **Import masks** are a comma-separated list; `*` matches any (possibly empty) sequence and `?` exactly
  one character. A mask containing `::` is matched against the fully qualified trust name (`ns::f`), a
  mask without `::` against the short name (`f`, `*`). An empty mask list means "all names". A name that
  matches no export produces the `-Wunmatched-mask` diagnostic (a warning by default; `-Wunmatched-mask=error`
  makes it an error), and an import that brings no name at all produces `-Wempty-import`;
  non-exported names are not part of the interface.
- **Group/package import masks apply to the site's combined export set:** a mask counts as matched if
  the name was found in at least one file of the group. `-Wunmatched-mask` is reported only for a mask
  that matched no file at all.

`@__MODULE_NAME__` is derived from the file's logical path: **the scope kind is part of the name** —
`m_` for a group file, `p_` for a package file; the `\` separator is encoded as `_`. For
**compound** paths the `_` character is additionally encoded as `__`
(`\\util\open_file` → `m_util_open__file`, `\\\tensor\math\matrix` → `p_tensor_math_matrix`), so
`\\a\b_c` (`m_a_b__c`) and `\\a_b\c` (`m_a__b_c`) are distinguishable, and the project path
`\\a\b` (`m_a_b`) does not collide with the package path `\\\a\b` (`p_a_b`). A single-component
name (a program stem or `\file`) is used as is; if two DIFFERENT paths produce the same derived
name, the compiler reports an error.

The same encoded name is used as the **automatic C++ namespace of the module**: ALL entities of an
importable file (functions, values, types, free operators) live in `namespace <enc> { … }`
(`m_util_open__file::c_f`, `m_util_open__file::c_ns::c_Point`), so same-named exports of different
modules do not collide. The entry function (`<module>__main__`, an exact global name for
`_main.cppt`) and top-level `{% %}` embeds stay outside the namespace. A headerless root program is
not isolated: it cannot be imported and nothing can reference its symbols.

### Qualified references {#module-qualified-refs}

A name exported by a file can be written together with its module, in any name position:

```
\lib(f, counter, Point);
\lib::f();                        # call
x := \lib::counter;               # value
p: :\lib::Point := \lib::make(); # type
```

Rules:

- **A reference does not load the module.** Loading is only the `\path(...)` operator; the module
  must already be loaded by an import, otherwise "Module '...' is not loaded".
- **Reference visibility equals the unqualified visibility at that site:** import masks and the
  file export set apply. A reference to a masked-out name, to `__::` (private), to a `_::` name not accessible from this group/package,
  or to a declaration outside the file interface is an error.
- **A reference is bound to the named module:** `\a::f` never falls back to a same-named export of
  another file; if `f` is exported by `\b`, the reference is an error.
- **The module is part of an exported name's identity.** `\a::f` and `\b::f` are DIFFERENT
  entities and coexist: same-named exports of different files are not a duplicate declaration. A bare
  name imported from several modules is **ambiguous** (`ambiguous name` diagnostic at the use site);
  the choice is made by qualification `\a::f`. `namespace` groups names but does not separate
  same-named declarations of different files.
- **A reference names one file:** a group path (`\\group`) in a qualified-name position is
  ambiguous and is an error (use the path of a file inside the group).
- The module spelling in a reference matches the import site spelling (`\dir\mod`), because the
  reference identity is the same import identifier.

### File loading principles {#module-loading}

- **One parse, one conversion:** a file AST is converted exactly once
  (`ModuleRegistry::m_bodyAst`); all import sites and the file `.cppt` use the same nodes.
- **One semantic run:** the file `.cppt` is a codegen projection of already analyzed facts.
- **Repeated import:** file loading is idempotent (the file is read and analyzed once).
  `duplicate-import` is an error only for the same file, the same scope and the same normalized masks;
  different masks or scopes are allowed. The severity is configurable (`-Wduplicate-import`, Error by
  default); one import site produces ONE message (including a group of files). Loading itself is not diagnosed.
- **Project file search:** `\`/`\\` are resolved from the project root (`--module-root`; the input
  file directory by default) and from additional `-I` roots in the given order. A file found in an `-I`
  root takes that root as its own scope: relative paths **inside it** are resolved from that root.
- **Package search:** `\\\pkg` is looked up in the importer's scope, `--package-path`,
  `TRUST_PACKAGE_PATH`, the built-in `share/trust` directory (`TRUST_RESOURCE_DIR` overrides it) and the
  current directory. If a package is found in several places as different archives/directories, this is
  an error, not "first match wins".
- **The build directory is not a search root** for project files or packages.
- **A file is not a namespace segment:** namespaces are explicit (`::`/`name::`); the file takes part
  in the identity of private names and in C++ linkage, but **declarations do not inherit the file
  name**.
- **Imports are not transitive:** names imported by a file do not enter its importer's scope.
  An import filters the VISIBILITY of names, not the analysis: function bodies of the imported file
  still see all their own declarations.
- **Duplicates:** inside one file always an error; same-named exports of different files coexist and
  the bare name becomes ambiguous (diagnostic at the use site); private names do not conflict;
  re-registering the same declaration of the same file is not a duplicate.

### File dependency output {#module-deps}

`trust --emit-module-deps <input file>` prints the file dependency graph as make rules
(`<target>: <deps>`): a rule for the input file's `.cppt` and a rule for each module's artifacts
(`.hpp`, plus `.cppt` when out-of-line definitions exist). Dependencies are transitive: a module
header includes the headers of its imports, so an artifact depends on ALL compilation sources and
headers as well as on the used package archives (otherwise a change to a deep module's interface
produces a stale build). Paths are relative to the current directory; the output can be included in
a Makefile.

### Compiled file form {#module-compiled}

The compiled form of a file is **the same source text** (`.src`) in which declaration bodies are
replaced by forward declarations (`f(...):Int32 := ...;`) and native imports
(`name(...) := %sym...;`). There is no separate loading path: the loader, parser, converter and
semantics treat it exactly like the source form. The generated module header publishes the C++
prototypes and type definitions (see [Module header](#module-header)); the import site contains only
the `#include` of that header (`@[include(\...\)]`/`@[link(\...\)]` are collected there as well).

Form limitations:

- generic functions/operators require a body (C++ `template`) and cannot be represented in
  declaration-only form: an exported generic without a body **and without a definition in the same
  module** is a Trust compile error (qualified name + C++ signature suffix are compared): the header
  must contain the body;
- native declarations must carry `@[include(\...\)]`/`@[link(\...\)]`, otherwise the importer's TU cannot be built;
- interface names must be qualified (`ns::Type`) so that they do not depend on imports.

A file without definitions (only declarations and native imports) does not get its own translation
unit: its interface is published by the `.hpp` header, so no `.cppt` is generated for it (the header
is still generated — see below).

### Module header (`<enc>.hpp`) {#module-header}

For every importable file the compiler generates a C++ header named `<encoded name>.hpp` (see
"Path prefixes and the header"). Its content is the file's **export set** inside
`namespace <enc> { … }`: declarations of exported types (a forward or a full definition, as in the
export set), function prototypes, `extern` variables; the
first line is `#pragma once`. Native declarations and the entry stay outside the wrapper (qualified
external name / global symbol). The header is self-contained (it compiles as a separate translation
unit); declaration positions for debugging and navigation stay in the module's `.cppt`.

- **The header publishes exactly the export set:** a forward in the interface stays a forward
  declaration (the client sees an incomplete type); a full definition in the interface is published
  in full. A body definition behind an interface forward does not go into the header — it is the
  owner's implementation (emitted in its `.cppt`).
- **Incomplete type:** the client may use only non-owning references (`ptr`/`ref`; owning `unique`
  requires a complete type). The completeness check also applies to **type inference**
  (`h := make();`): completeness is determined from the callee declaration (its return type), not
  only from an explicit variable annotation.
- **The client includes the header:** at the import statement the generated C++ contains
  `#include "<enc>.hpp"`; the module's own translation unit also starts with the header include and
  does not repeat published definitions.
- **Re-export (facade):** an import inside the interface block (`\lib(a, b);`) publishes the owner's
  names; the facade header is an umbrella: `#pragma once` + `#include "<owner enc>.hpp"`. If the
  facade interface block has no declarations of its own, the header has no other declarations;
  otherwise the facade's own declarations are added next to the `#include`. Name ownership does not
  change — the definition and the C++ symbol stay with the owner. Namespace aliases of re-exported
  modules are **not** published automatically: to let a facade client see the prefix binding, declare
  it in the facade interface block (`ns:: :- \lib::ns::;`).
- **Generic/constexpr free-function bodies go into the header:** an exported generic (`<T> f(...)`)
  and an `@[constexpr]` free function are published as definitions, so a client can instantiate/use
  them from another module; `.cppt` does not repeat those bodies. A reference from such a body to a
  `__::` helper is a Trust diagnostic (it cannot be expressed in the header).
- **Every** importable file gets a header, including declaration-only ones (which have no `.cppt`).
  A headerless root program has no header.
- The dependency graph (`--emit-module-deps`) contains a rule for the header too. Dependencies are
  **conservative**: the header rule refers to all sources, headers, and package archives of the
  compilation, not only the module's direct imports (otherwise an interface change in a deep module
  could produce a stale build).

The precompiled form (a binary file interface) is **not supported** in the current version: the file
interface is source text only. A possible future direction is to represent a compiled file as a native
library with a stable name and import it through the existing native declaration mechanism; the
language does not introduce a separate ABI contract for files. Implementation boundaries — in
[Status](../status/).

### Name aliases (`f :- \lib::g;`) {#func-alias}

An alias is a **new name for an existing function or value/constant** (facade curation): the client
works with a short name (`f`), while the implementation and the C++ symbol stay with the owning module.
The `:-` operator declares an alias in the file's interface block; in a file without an interface
block — in the body (where default-public applies):

```trust-lang
\facade {
    \lib(g);
    f :- \lib::g;
};
```

- For a function the alias binds the **target's overload set**: the signature is selected at the call
  site by the arguments, as for a regular overload; types and implementation belong to the target.
  For a value/constant the alias is a second name of the same storage: reads and writes are
  normalized to the target.
- No dedicated C++ function is emitted for the alias: the generated code prints the target's C++ name
  together with its identity suffix (`lib::c_g_P10N5_3AInt32_(1)`). The file's header includes the
  target owner's header (umbrella `#include`).
- The alias name joins the file's export set (the client sees it both short `f` and qualified
  `\facade::f`) if it is declared in the interface block or in a file **without** an interface block.
  In the body of a file **with** an interface block the alias, like any declaration outside the block,
  is file-private and invisible to importers.
- The right-hand side does not load the module: the target must already be loaded by an import of
  this file; an alias with an unresolved reference is a compile error. The signature form
  (`f(x:Int32):Int32 :- \lib::g;`) is not supported; types use `::-`, not `:-` (explicit diagnostic).
- The `alias` keyword is the same operation in `import … as …` style: `alias \lib::g as f;`
  (a name) and `alias \lib::T as :MyT;` (a second type name, see below); the kind is given by `:`
  on the new name.

### Type aliases (`:A ::- T;`) {#type-alias}

`::-` declares a **second name of an already existing type** — the same `TypeId` as the target (no
nominal type is created). It also works for a cross-module target:

```trust-lang
:MyInt ::- :Int32;
:MyPoint ::- \lib::Point;
```

The equivalent spelling through the `alias` keyword: `alias \lib::Point as :MyPoint;`.

No C++ type is emitted: annotations and constructor calls through the alias name are printed with
the target's name. A new nominal type is declared with `::=` (see
[Object creation](../../operators/create/)).

Implementation limits — see [Status](../status/).

### Namespace aliases (`ns:: :- \lib::ns::;`) {#namespace-alias}

A namespace prefix may be bound to a namespace of another module — no entity is created and no C++
name is emitted:

```trust-lang
ns:: :- \lib::ns::;
x:Int32 := ns::f(1);      # → lib::c_ns::c_f(...)
```

- Both LHS forms are allowed: `ns::` and `::ns::`; the RHS must end with `::` (the "namespace, not a
  name" marker).
- The alias is bound to the declaration scope: inside an exported namespace block (`ns:: { ... }`,
  interface block) it is hoisted into the module root with a qualified prefix (`outer::ns`); in other
  blocks it lives until the block ends.
- Inside the interface block the alias **is exported**, including a nested one (`\facade::outer::ns::f`):
  an importer of the facade receives the prefix binding and may write `outer::ns::f` and
  `\facade::outer::ns::f`; the import mask applies to the alias name by the general rules: a mask with
  `::` matches the full name (`outer::ns`), one without `::` matches the short name (`ns`). A mask that selects only a
  namespace alias (no declarations) counts as matched: it is neither `-Wunmatched-mask` nor
  `-Wempty-import`. Outside the interface block
  the alias is not exported.
- The target namespace must exist in the given module (otherwise an error at the alias); one prefix
  cannot be bound to two namespaces. The target must be module-qualified: an alias of a namespace of the
  same file is not supported (use `... = ns` for short-name lookup).

### Module aliases (`\m :- \pkg\deep\mod;`) {#module-alias}

`:-` with a module on the left declares a **spelling alias of an already loaded module** (no new
module identity is created):

```trust-lang
\lib(g);        # the target must already be loaded
\m :- \lib;
x:Int32 := \m::g();   # → lib::c_g(...)
```

- The alias does not load the target; an unloaded module is an error.
- The alias is declared at module level (not inside a function) and is visible ONLY in the declaring
  module: the spelling is unique per module, the same spelling is allowed in different modules, and the
  alias is not exported. Rebinding it in the same module to another module is an error. The alias
  spelling cannot coincide with an import spelling of the **same module** (the import-spelling table is
  per module, not compilation-wide); the check does not depend on declaration order.
- References `\m::name` are normalized to the target module; the header/imports use the target `.hpp`.

Implementation limits — see [Status](../status/).

### Name visibility {#visibility}

- **Top-level file declarations are exported** (public by default): the module is part of a name's
  identity, so same-named exports of different files coexist; a bare name after importing several
  modules is ambiguous (use `\module::name`).
- **`_:: { ... }` / `_{ ... }` at file top level is protected**: visible to the current file and to
  files of the same project group or the same package; not exported to outside importers. In the
  current C++ backend it is emitted into the module namespace (not anonymous); Trust semantics
  enforces visibility.
- **`__:: { ... }` / `__{ ... }` at file top level is private**: visible only to the current file;
  emitted into a C++ anonymous namespace (internal linkage).
- **`:: { ... }` / `name:: { ... }`** are exported namespaces. Their declarations have static
  lifetime: after the block they are accessible with qualification (`name::f`), and `::` contents
  without qualification. In C++ they are emitted as namespaces (`c_<name>`); a call `name::f`
  becomes `c_<name>::c_f`. An importable file additionally isolates its C++ names with the automatic
  module namespace `<encoded name>` (see "Path prefixes and header").
- Duplicates inside one file are always an error. Private names from different files do not
  conflict (their identity includes the file); same-named exports of different files coexist and
  make the bare name ambiguous.

### Alias and import limits {#alias-limits}

- **Imports are positional** (see above): an import is not hoisted to the start of a block, unlike
  in most languages; it applies to the current scope.
- **No alias chains:** a name alias of a name alias is normalized to the FINAL target; a namespace
  alias whose target is another namespace alias (including one exported by a facade) is not
  supported — the target must be a namespace of a module.
- **A module alias is neither exported nor re-exported:** it only works in the declaring module
  (unlike name and type aliases, which are part of the file's export set).
- **No negating masks:** a mask only selects visible names (`*`, `?`); exclusion cannot be expressed.
- **Visibility granularity is three-level:** file (`__::`), group/package (`_::`), file export;
  there is no intermediate granularity (like `pub(in path)`).


## Packages {#package}

The package manifest — the only file with package information — is `trust.toml` in the root of the
directory (its presence is the package marker). The file format is TOML. The required field is
`name`; `description`, `authors`, `license`, `module-root` are additionally allowed, as well as
`[[layout.layers]]`, `[[artifact]]` and `[[link.headers]]`. Dependency declarations (`dependencies`)
are not supported in the current version — see [Status](../status/).

A package is always **single**: an archive does not contain importable nested packages. A `.trust`
inside a package is an ordinary file/resource, not a nested package. If a package needs another
package, it is resolved separately through search paths, not by recursive packing.
`module-root` defines the root against which package file headers are computed (by default, the
archive root).

The `version` field is the **package's own semver**, not the compiler version. The packager keeps
the authored version; if the source `trust.toml` has none, the compiler version
(`TRUST_VERSION_SHORT`) is filled in as provenance. The compiler that built the package is recorded
separately — in `[build].toolchain`. The package version does not have to match the compiler version:
a `1.2.3` package can be built by any compiler.

Example manifest:

```toml
name = "demo"
description = "Demo package"
```

### Creating a package {#pack-create}

The compiler creates a package:

```
trust --package <directory> [-o <file>.trust]
```

By default the whole directory tree is packed (sources `*.src`, C++ headers, other files, and
`*.trust` files as ordinary files — they do not become importable nested packages); build artifacts
(`.trust/`, `*.cppt`, `*.src_map`, `*.o`, `*.so`, `app`) are excluded.

If the package must not contain the whole tree (or files come from different directories), the
layout is described **in the manifest itself** with `[[layout.layers]]` tables:

```toml
name = "demo"

# Sources: the manifest directory, recursively; only *.src files are packed
[[layout.layers]]
source-dir = "."
archive-dir = "src"
patterns = ["*.src"]

# Headers: the inc subdirectory; they land in include/demo/ keeping their structure
[[layout.layers]]
source-dir = "inc"
archive-dir = "include/demo"
patterns = ["*.hpp"]
```

| field | meaning |
|---|---|
| `source-dir` | source directory relative to the manifest directory (default `.`); walked **recursively** (subdirectories are included automatically) |
| `archive-dir` | directory inside the archive (default `.`); the file path relative to `source-dir` is preserved under it |
| `patterns` | glob masks of **file names** (`*`, `?`); files not matching any mask are not packed |

The following are errors (no fallback): a `source-dir` directory that does not exist, an absolute
`archive-dir` or one containing `..`, an empty/invalid `patterns` list, a layer that matches no
files, an explicitly empty `layout.layers` array, and two layers producing the same archive path.

A copy of the manifest with the `version` field (the package's authored version; the compiler
version as provenance when absent) and automatically added `[build]` (`formatVersion`, `toolchain` — compiler version, `created` — date/time) and
`[files]` (MD5 checksums for integrity control) tables is placed in the archive; the original
`trust.toml` is not modified. By default the archive is named `<name>.trust`.

### Provider artifacts {#pack-artifact}

A package may describe a **link contract** with a native library. It is not a file list but a set
of rules: how a logical library name becomes `-l`/`-L` and which include directories are required.
A package by itself **does not add link options** — linking appears only on actual use
(`@[link("...")]` or `[[link.headers]]`).

```toml
name = "native_async"

[[layout.layers]]
patterns = ["*.src"]

[[layout.layers]]
source-dir = "include"
archive-dir = "include"
patterns = ["*.hpp"]

[[layout.layers]]
source-dir = "lib"
archive-dir = "lib"
patterns = ["*.cpp", "*.hpp"]

[[artifact]]
provides = ["native_async"]  # logical name for @[link("native_async")]
kind = "source"              # source | package | toolchain | system
sources = ["lib/thread_pool.cpp", "lib/async_mutex.cpp"]
link-options = ["-pthread"]

# The artifact's OWN dependency: linked transitively whenever the artifact is.
[[artifact]]
provides = ["trust-runtime"]
kind = "toolchain"
libs = ["trust-runtime"]
requires = ["gmp"]

# Demand: a used header -> logical libraries.
[[link.headers]]
header = "trust/rational.hpp"
libs = ["trust-runtime"]
```

| `kind` | meaning |
|---|---|
| `source` | `sources` live **in the package** and are compiled by the application compiler into `libtrust_third_party.a`; `libs` are forbidden |
| `package` | library files live **inside the package**; `libs` are relative paths (e.g. `lib/libfoo.a`) |
| `toolchain` | the library is provided by the toolchain and looked up next to `trust`, in `lib/`, `../lib/`, `LD_LIBRARY_PATH`; `libs` are logical names (`-l<name>`) |
| `system` | a system library resolved by the linker (`-l<name>`) |

Required fields: `kind`, `provides`; for `source` — `sources`, for the other kinds — `libs`.
Optional: `link-options` (e.g. `-pthread`; for `source` they apply to both compilation and
linking), `capabilities` (e.g. `with-torch`; absence is diagnosed before linking) and `requires`
(logical libraries the artifact itself needs: they are linked transitively whenever the artifact
is; referring to the artifact's own `provides` is an error). Provider
headers always live in the package `include/` directory (added automatically); artifacts have no
separate include fields. A `provides` name must be unique among used packages; a collision is an
error.

`trust-runtime` and `gmp` are ordinary artifacts, just like `tensor_cpu`/`pthread`. In the
standard library package `trust-runtime` is declared as `kind="toolchain"` with
`requires = ["gmp"]`: the `trust-runtime.a` archive contains objects built against GMP, so `-lgmp`
is needed for **any** runtime link, not only when `BigInteger`/`Rational` are used. The
`[[link.headers]]` rules therefore list only `trust-runtime`, while `gmp` arrives via `requires`.
A program without runtime headers (a plain hello world) still does **not** link `trust-runtime`.

### Extracting a package {#pack-extract}

The compiler extracts a package:

```
trust --extract <file>.trust [-o <directory>]
```

Archive entries are checked for escaping the destination directory (path traversal protection), and the
`trust.toml` manifest from the archive is parsed and validated. After extraction the content is verified
against the `[files]` table (MD5) if present. If `-o` is not given, extraction goes
into the `<name>/` subdirectory (name from the manifest) of the current directory.

### Viewing a package as a directory {#pack-view}

A `.trust` package is a regular gzip-compressed tar archive, so its contents can be opened with any
archiver (`tar`, 7-Zip, ark, etc.). In the Midnight Commander file manager the archive can be opened as
a directory right in a panel by adding a rule to the extension file `~/.config/mc/mc.ext.ini` (it is
created by copying the system-wide `/etc/mc/mc.ext.ini` via `F9` → Command → Edit extension file):

```ini
[trust]
Shell=.trust
ShellIgnoreCase=true
Include=tar.gz
```

After saving the file (or restarting mc), pressing Enter on a `<name>.trust` file enters the archive as
a directory. The `Include=tar.gz` section reuses mc's built-in tar handling; the `.trust` rule must be
placed above the `[gzip]` section (sections are matched top to bottom).

### Loading a package during compilation {#pack-use}

The compiler loads sources not only from individual `*.src` files but also from `.trust` packages.
A package is addressed with the `\\\` prefix: `\\\pkg\file` is a package root file, and
`\\\pkg\group\file` is a file of a package group. The compiler unpacks the package into the
temporary build directory (`<build-dir>/packages/<name>-<md5(archive)[0..12]>/`), not into the package's
source tree, and loads files from there as ordinary sources. Inside a package, `\`/`\\` paths are
resolved from the **package root** (`module-root` of the manifest), not from the project; another
package is addressed as `\\\other\...`.

Package search paths: the local package in the project root, then `--package-path`, then built-in
paths (`TRUST_PACKAGE_PATH`, `share/trust`; `TRUST_RESOURCE_DIR` overrides `share/trust`). If a
package is found in several roots as different archives, this is an error, not "first match wins".

If a package declares `[[artifact]]`, its include directories are added to the build while link
options are added **only on demand** (`@[link("...")]` on a used declaration or `[[link.headers]]`
for a used header). Extracting a package alone never adds link options.

All files of the package are available to the compiler: the unpacked package directory and its
`include/` subdirectory are added to the header search paths when building the generated C++
(`-Ipackages/<name>` and `-Ipackages/<name>/include`). Therefore C++ headers of a package should
be placed in `include/`: they are included via `@[include("header.hpp")@]` /
`#include <header.hpp>` without copying them into the build directory.

When a package is created, a `[files]` table with MD5 checksums of every file (except `trust.toml`
itself) is added to the manifest copy. During extraction the compiler verifies the content against
`[files]`: a mismatch, a missing file, or an extra file is an error; a missing `[files]` table itself
(no integrity data) is an error too. This is integrity control
(protection against corruption and tampering), not publisher signature verification — package signing
and version resolution are not implemented in the current version (see [Status](../status/)).

### Package parameters (summary) {#pack-params}

| Parameter / option | Location | Purpose |
|---|---|---|
| `name` | `trust.toml` | required package name (lowercase letters, digits, `_`) |
| `description`, `authors`, `license` | `trust.toml` | optional informational metadata |
| `version` | `trust.toml` (optional) | package semver; the packager keeps the authored version (fills in the compiler version as provenance when absent); the toolchain version lives in `[build].toolchain` |
| `module-root` | `trust.toml` | root for package file headers; **default: the archive root** |
| `source-dir` | `[[layout.layers]]` | source dir relative to the manifest; **default `.`** |
| `archive-dir` | `[[layout.layers]]` | directory inside the archive; **default `.`** |
| `patterns` | `[[layout.layers]]` | required list of file-name glob masks (e.g. `["*.src"]`) |
| `kind` | `[[artifact]]` | required: `source` (sources inside the package), `package` (files inside the package), `toolchain` (next to `trust`) or `system` (`-l`) |
| `provides` | `[[artifact]]` | required list of logical library names (for `@[link("...")]`/`[[link.headers]]`) |
| `sources` | `[[artifact]]` | required for `kind="source"`: package-relative `.cpp` paths |
| `libs` | `[[artifact]]` | required for `package`/`toolchain`/`system`: package-relative paths or `-l` names |
| `link-options` | `[[artifact]]` | optional flags; for `source` they apply to both compilation and linking (`-pthread`) |
| `capabilities` | `[[artifact]]` | optional toolchain capability requirements (e.g. `with-torch`) |
| `header`, `libs` | `[[link.headers]]` | required: used header → logical libraries (demand-driven linking) |
| `[build]`, `[files]` | archive `trust.toml` | generated by the packager: provenance and MD5 integrity |
| `trust --package <dir> [-o <file>.trust]` | CLI | create a package from a directory |
| `trust --extract <file>.trust [-o <dir>]` | CLI | extract and verify a package |
| `TRUST_RESOURCE_DIR` | env | override the `share/trust` root for `\\\pkg` |
| `TRUST_PACKAGE_PATH` | env | extra search directories for `\\\pkg` (`:`) |

