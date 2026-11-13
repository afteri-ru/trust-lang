---
title: Status and plans
weight: 90
tags: [overview, development, verification]
---

## Implemented in the current version (0.6.0)

- [x] The [syntax concept](/en/docs/syntax/concept/) has been developed
- [x] The [macro syntax](/en/docs/syntax/macros/) has been defined
- [x] Parser and literals for [basic data types](/en/docs/types/)
- [x] The concept of [language guarantees](/en/docs/safety/) has been formulated
- [x] The [playground](/en/docs/sandbox/) for testing examples has been developed
<<<<<<< HEAD
- [x] Package manager [trust-pm](/en/docs/pm/): CLI+JSON contract, `*.trust` format, cache and offline mode
=======
- [x] Generic functions and templated native imports (`<T>`, type inference); type packs (`<T...>`, `xs: T...`) for functions and user-defined native class templates
- [x] A second name of an existing type (`:A ::- T;`): a semantic synonym (one `TypeId`), unlike `::=` (a new nominal type)

## Files, groups and packages

- [x] Files, groups and packages: file import (`\path(...)`) and group import (`\\group()`, `\\group\dir()`),
  first-level group import of a package (`\\\pkg()`), import masks (glob visibility filter)
- [x] File header (`\path;`), matching against the logical path, project and package search roots
- [x] Qualified name of an entity is a programmer namespace (`ns::name`, call/value/type); an import
  brings the owner's names into the current scope; a qualified name is not shadowed by a local one;
  the module is NOT a name prefix (the `\path::name` reference form is removed)
- [x] `.trust` packages: `trust.toml` manifest, `[[layout.layers]]` layout, `[[artifact]]` artifacts,
  demand-driven linking `[[link.headers]]`, `[files]` integrity (MD5)
- [ ] Package dependency declarations (`dependencies`), version resolution and signing — not implemented
- [ ] Precompiled (binary) files — not implemented: the file interface is source text only
- [x] Module header (`.hpp`): the file's export set (without an automatic module namespace),
  a self-contained TU; also generated for declaration-only files (which have no `.cppt`); a rule in
  `--emit-module-deps`; the client TU includes the header (`#include "<enc>.hpp"`)
- [x] Bodies of exported **generic** functions and `@[constexpr]` free functions are published in
  the header: a client instantiates/uses them from another module; `.cppt` does not repeat them;
  a reference to `__::` from such a body is a Trust diagnostic

- [x] The export set determines the visible type: a forward in the interface → the client sees an
  incomplete type; a full definition in the interface → the client uses the type by value
- [x] A type definition in the body behind an interface forward is the owner's implementation: it is
  not published to the client and is emitted in the owner's TU (the file gets its own `.cppt`)
- [x] Scope visibility: `__::` — private (file only), `_::` — protected (files of the same group/package); public — file export.
- [x] File interface (`\path { ... };`): declarations outside the block are **file-private**
  (module-qualified registration key, C++ anonymous namespace)
- [x] Set re-export (`\lib(a, b);` inside the interface block): the names join the facade's export
  set without modifying the original declarations; the facade header is an umbrella (`#include` of
  the owner headers), and the client sees the names both short and via the facade qualification;
  re-export is transitive (a chain of facades)
- [x] C++ names of entities come from programmer namespaces (`ns::`); the module does not enter the
  C++ symbol (the ABI is transparent w.r.t. C++). Same-named DEFINITIONS of different modules in one
  namespace — a collision diagnosed before the linker
- [x] Name aliases (`f :- name;` / `f :- ns::name;`): functions and values/constants; the alias
  name joins the file's export set and binds the target's overload set/storage; no C++ entity is
  emitted (calls/reads use the target's C++ name and suffix); the file header includes the owner's header
- [x] Type aliases (`:A ::- T;` / `:A ::- ns::T;`): a second name of the same `TypeId`; the C++
  type name is the target's name
- [x] Namespace aliases by area (`a :- ns::;`): the target is a visible namespace area (module
  qualification is not required); the binding is expanded during name lookup, creates no C++ entity,
  and is not exported to importers (a warning is emitted inside an interface block). An ambiguous
  area (the name exists in several modules) — deferred resolution: the name is resolved by the
  ordinary lookup
- [ ] Alias chains are not supported: a namespace alias of another alias is an error (see
  [Alias and import limits](syntax/hierarchy/#alias-limits))
- [ ] Negating import masks (`!name`) are not supported: a mask only selects visible names
- [x] Extended name lookup (`... = ns, ns::sub;`): scope-bound, overload sets
  merge, no cancellation statement (`... = _;` is an error)
- [ ] Full-fledged OOP (classes/methods) — not implemented; see [Classes](types/class/)
>>>>>>> release/0.7.x
