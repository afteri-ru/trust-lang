---
title: Architecture
tags: [architecture, compiler]
linkTitle: Architecture
description: >
    Basic information about the internal structure of TrustLang
weight: 70
# draft: true
---

### Architecture boundaries

- **TrustLang is responsible for safety, not speed.** Multi-stage lowering (AST → HIR → MIR) exists for
  analysis and property proofs; there is no middle-end optimizer. Performance optimization is performed by
  the C++ compiler that receives the generated code.
- **Safety checks must survive C++ optimization.** They are emitted as observable operations
  (runtime functions/`throw`), not as potentially eliminable arithmetic; this is covered by building and
  running the artifact with `-O2/-O3`.
- **A component is the directory `include/<component>/`.** The public API is declared explicitly — the
  root headers listed in the `trust_component(PUBLIC_HEADERS ...)` manifest; implementation details live in
  internal subdirectories (`INTERNAL_DIRS`: `ast/nodes`, `semantic/typer`, `ir/hir`, ...); an internal
  directory's memory is its `MEMORY.md` (optional where no invariant is hard to derive: knowledge
  lives in the code's WHY comments). Including an internal subdirectory header from outside the component is forbidden
  (CTest `internal_boundary_guard`); `public_api_guard` checks that the public API is complete and reachable.
- **Delegation depends on the backend's target contract.** C++ ABI, layout and mangling are not modelled
  while the C++ backend's contract provides them; a native backend (LLVM) owns ABI/layout/mangling in its own
  target contract — that is not C++ delegation. Target facts never enter the language model in either case.
- **A backend is plugged in by contract, not by editing the orchestrator:** stages are described by data
  (`BACKEND_STAGE_LIST`), the target contract is `include/pipeline/backend/` (the contract is an internal subdirectory of `pipeline`); the C++ build (`make`) is the
  `toolchain` layer, not part of the compilation pipeline.
- **One owner per fact, a single computation path (mandatory architecture requirement).** Every compiler
  fact has exactly one stored representation (its owner) and exactly one named way to compute it — the owner's
  accessor; a second stored representation of the same fact is forbidden and a projection may be read only
  through the owner's accessor. Entity identity is deterministic and assigned by the owner of the
  representation; a fact owned by an external tool (C++ ABI, layout, mangling) is not modelled again — only a
  predicate and a verification. A violation is an architecture defect; the full requirement list is in
  `ARCHITECTURE.md` (section 3).

### Sequence of processing stages of the program source text

1. The input stream of the source text is split by the lexer into individual universal elements
   (tokens), each of which carries information about its location in the source text.
2. The macro processor compares token sequences and, on a match with a given template
   sequence, replaces the macro definition with its body.
3. The token sequence is analyzed by the parser (Bison), which builds a parse tree —
   Term. Term stores the parse structure and token ranges; for the purpose of formatting the
   program text, information about the position between adjacent elements is used.
4. The parse tree (Term) and universal elements (tokens) are converted into concrete AST nodes,
   which all the other components of the application work with. Information about the node's
   location in the source text is also preserved.
5. The AST goes through semantic analysis: name/scope resolution, type checking and inference,
   function/operator overload resolution, contract checking. Analysis facts are materialized in AST
   node fields; on compilation errors code generation is not performed.
6. The typed AST is lowered to HIR (structural form: sugar expansion, labels/goto, analyzer
   temporaries) and MIR (CFG for flow analyses and the verifier). The `verifyHir`/`verifyFlowModule`
   gates check the representation contracts; flow analyses (definite assignment, derived-view
   invalidation) run afterwards.
7. HIR goes into the code generator, which transpiles it into a C++ source buffer.
   At the same time, a special map of the program source text onto the generated
   C++ code (SourceMapper) is created, which can be used to trace the translation of each fragment into the final
   C++ source.
8. The source buffer is saved to the build directory. Next to the transpiled `.cppt`, the configured
   build files `Makefile` and `build.conf` are placed, with which an executable file can be built
   by an external C++ compiler without manual configuration. The compressed SourceMapper information is written there as well,
   which can be embedded in the binary for later debugging or used by the LSP server for
   code navigation.
9. If the generated C++ uses TrustLang runtime headers, they are delivered from the standard
   library package `share/trust/stdlib.trust` (trust sources, built-in-type implementations
   `builtin/*.src` next to their C++ headers `builtin/*.hpp`). The package is unpacked into the build directory, and `build.conf`
   adds the include paths `-Istdlib -Istdlib/include`; headers are not copied individually.
   Header delivery rules: **C++ standard library** headers (angle brackets, `<...>`) are provided
   by the C++ compiler in use; TrustLang's own headers (`@trust/…` for runtime primitives and
   `@stdlib/builtin/…` for built-in types, `@stdlib/…` for the standard library) live in `stdlib.trust` and are resolved through its
   include paths. Built-in implementations and library classes are loaded explicitly by `\std();`
   (which also loads the runtime implementations of memory/ownership wrappers via `\mem();` and of
   interruption/exception types via `\exceptions();`). Optional native libraries (tensor) are delivered as separate provider
   packages in `share/trust`; their `[[artifact]]` describes include dirs and link options, which are
   added only on actual use (`@[link]`/`[[link.headers]]`). Source packages (`kind="source"`) are
   delivered as sources only: the application compiler builds them into `libtrust_third_party.a`.
10. Formal verification (AoRTE) is implemented as a separate transpiler of contracts into the SMT-LIB 2 format
   for an external SMT solver.
<<<<<<< HEAD
9. A backend to any other programming language or directly to LLVM can also be created. In this case,
   the ability to insert C++ code is lost (but fragments of another language can be inserted),
   however, greater versatility appears through the use of different backends.

=======
11. A backend to any other programming language or directly to LLVM is provided for by the architecture: the
   LLVM backend is a mandatory capability of the target model. In that case the ability to insert C++ code is
   lost (but fragments of another language can be inserted), while ABI/layout/mangling become facts of that
   backend's own target contract rather than C++ delegation; greater versatility appears through the use of
   different backends.
>>>>>>> release/0.7.x
