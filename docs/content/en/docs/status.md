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
- [x] Generic functions and templated native imports (`<T>`, type inference); type packs (`<T...>`, `xs: T...`) for functions and user-defined native class templates
- [x] A second name of an existing type (`:A ::- T;`): a semantic synonym (one `TypeId`), unlike `::=` (a new nominal type)

## Files, groups and packages

- [x] Files, groups and packages: file import (`\path(...)`) and group import (`\\group()`, `\\group\dir()`),
  first-level group import of a package (`\\\pkg()`), import masks (glob visibility filter)
- [x] File header (`\path;`), matching against the logical path, project and package search roots
- [x] Qualified references `\path::name` (call/value/type): the module must already be loaded by an
  import (a reference never loads), visibility follows the site masks and the file export set; the
  module is part of a name's identity (`\a::f` and `\b::f` coexist; a bare name with two imports is
  `ambiguous name`)
- [x] `.trust` packages: `trust.toml` manifest, `[[layout.layers]]` layout, `[[artifact]]` artifacts,
  demand-driven linking `[[link.headers]]`, `[files]` integrity (MD5)
- [ ] Package dependency declarations (`dependencies`), version resolution and signing — not implemented
- [ ] Precompiled (binary) files — not implemented: the file interface is source text only
- [x] Module header (`.hpp`): the file's export set inside the automatic `namespace <enc>`,
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
- [x] C++ name isolation for modules: all entities of an importable unit live in the automatic
  `namespace <encoded name>` (entry and `{% %}` stay outside); a headerless root program is not
  isolated (same-named exports of different modules link together)
- [x] Name aliases (`f :- \path::name;`, facade curation): functions and values/constants; the alias
  name joins the file's export set and binds the target's overload set/storage; no C++ entity is
  emitted (calls/reads use the target's C++ name and suffix); the file header includes the owner's header
- [x] Type aliases (`:A ::- T;` / `:A ::- \lib::T;`): a second name of the same `TypeId`; the C++
  type name is the target's name
- [x] Namespace aliases (`ns:: :- \lib::ns::;`): the prefix is bound to a namespace and expanded
  through the owner's export set; inside an exported namespace block it is hoisted with a qualified
  prefix; inside the interface block it is exported, including a nested one (`\facade::outer::ns::f`)
- [x] Module aliases (`\m :- \pkg\deep\mod;`): a spelling of an already loaded module; visible only in
  the declaring module (not exported), the same spelling is allowed in different modules; `\m::name`
  is normalized to the target, no new identity
- [ ] A namespace alias of the file's own namespace (`a:: :- b::`) is not supported: the target must be
  module-qualified (use `... = ns` for short-name lookup)
- [ ] Alias chains and module-alias export are not supported: a namespace alias of another alias is an
  error, and a module alias only works in the declaring module (see [Alias and import limits](syntax/hierarchy/#alias-limits))
- [ ] Negating import masks (`!name`) are not supported: a mask only selects visible names
- [x] Extended name lookup (`... = ns, ns::sub;` / `... = \lib::ns;`): scope-bound, overload sets
  merge, no cancellation statement (`... = _;` is an error)
- [ ] Full-fledged OOP (classes/methods) — not implemented; see [Classes](types/class/)
