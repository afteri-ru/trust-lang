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
  (runtime functions/`throw`), not as potentially eliminable arithmetic; this is covered by `-O2/-O3` tests.
- **A component is the directory `include/<component>/`.** The public API is declared explicitly — the
  root headers listed in the `trust_component(PUBLIC_HEADERS ...)` manifest; implementation details live in
  internal subdirectories (`INTERNAL_DIRS`: `ast/nodes`, `semantic/typer`, `ir/hir`, ...); an internal
  directory's memory is its `MEMORY.md` (optional for a small implementation — at most 3 files and 16 KiB — where knowledge
  lives in the code's WHY comments). Including an internal subdirectory header from outside the component is forbidden
  (CTest `internal_boundary_guard`); `public_api_guard` checks that the public API is complete and reachable.
- **A backend is plugged in by contract, not by editing the orchestrator:** stages are described by data
  (`BACKEND_STAGE_LIST`), the target contract is `include/pipeline/backend/` (the contract is an internal subdirectory of `pipeline`); the C++ build (`make`) is the
  `toolchain` layer, not part of the compilation pipeline.

### Sequence of processing stages of the program source text

1. The input stream of the source text is split by the lexer into individual universal elements
   (tokens), each of which carries information about its location in the source text.
2. The macro processor compares token sequences and, on a match with a given template
   sequence, replaces the macro definition with its body.
3. The token sequence is analyzed by the parser, which builds an AST. A separate concrete
   syntax tree (CST, or parse tree) is not built; instead, for the purpose of
   formatting the program text, information about the position between adjacent AST elements is used.
4. Universal elements (tokens) are converted into concrete AST nodes, which all the other
   components of the application work with. Information about the node's location in the source text
   is also preserved.
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
   library package `share/trust/stdlib.trust` (trust sources, built-in-type definitions and C++
   headers under `include/`). The package is unpacked into the build directory, and `build.conf`
   adds the include paths `-Istdlib -Istdlib/include`; headers are not copied individually.
   Header delivery rules: **C++ standard library** headers (angle brackets, `<...>`) are provided
   by the C++ compiler in use; TrustLang's own headers (`@trust/…` for built-in types and
   `@stdlib/…` for the standard library) live in `stdlib.trust` and are resolved through its
   include paths. Optional native libraries (tensor) are delivered as separate provider
   packages in `share/trust`; their `[[artifact]]` describes include dirs and link options, which are
   added only on actual use (`@[link]`/`[[link.headers]]`). Source packages (`kind="source"`) are
   delivered as sources only: the application compiler builds them into `libtrust_third_party.a`.
10. Formal verification (AoRTE) is implemented as a separate transpiler of contracts into the SMT-LIB 2 format
   for an external SMT solver.
11. A backend to any other programming language or directly to LLVM can also be created. In this case,
   the ability to insert C++ code is lost (but fragments of another language can be inserted),
   however, greater versatility appears through the use of different backends.
