---
title: Architecture
tags: [architecture, compiler]
linkTitle: Architecture
description: >
    Basic information about the internal structure of TrustLang
weight: 70
draft: true
---

### Sequence of processing stages of the program source text

1. The input stream of the source text is split by the lexer into individual universal elements
   (tokens), each of which carries information about its location in the source text.
2. The macro processor compares token sequences and, on a match with a given template
   sequence, replaces the macro definition with its body.
3. The token sequence is analyzed by the parser, which builds an AST. A separate concrete
   syntax tree (CST, or parse tree) is not built; instead, for the purpose of
   formatting the program text, information about the position between adjacent AST elements is used.
4. Universal elements (tokens) are converted into concrete AST nodes, which all
   the other components of the application (analyzers, type inference, etc.) work with. Information about
   the node's location in the source text is also preserved.
5. The processed AST goes into the code generator, which transpiles it into a C++ source buffer.
   At the same time, a special map of the program source text onto the generated
   C++ code (SourceMapper) is created, which can be used to trace the translation of each fragment into the final
   C++ source.
6. The source buffer is saved to the build directory. Next to the transpiled `.cppt`, the configured
   build files `Makefile` and `build.conf` are placed, with which an executable file can be built
   by an external C++ compiler without manual configuration. The compressed SourceMapper information is written there as well,
   which can be embedded in the binary for later debugging or used by the LSP server for
   code navigation.
7. If the generated C++ uses TrustLang runtime headers, they are also saved in the build
   directory so that the C++ compiler can use them when building the project. There are two sources: headers of
   **built-in** types (`@trust/…`) are extracted from the `trust-runtime` library, headers of
   the **standard library** (`@stdlib/…`) — from the compiler itself.
8. Formal verification (AoRTE) is implemented as a separate transpiler of contracts into the SMT-LIB 2 format
   for an external SMT solver.
9. A backend to any other programming language or directly to LLVM can also be created. In this case,
   the ability to insert C++ code is lost (but fragments of another language can be inserted),
   however, greater versatility appears through the use of different backends.
