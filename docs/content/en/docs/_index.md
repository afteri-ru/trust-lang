---
title: About the project
tags: [trust-lang, overview, language-design]
weight: 10
menu: main
no_list: false
---

*TrustLang* is a programming language that is transpiled to C++ and built by an external
compiler. The key properties of the model:

- **Rule-based grammar, not keyword-based** — a simple single-pass parser and
  extensibility; the usual keyword-based notation is implemented by macros
  ([Syntax](syntax/), [Macros](syntax/macros/)).
- **Safe memory model without a garbage collector** — RAII, strong/weak references, prohibition of
  strong cyclic references, stack protection ([Safety and quality](safety/)).
- **C/C++ — first-class integration** — native types/functions/classes and templates, embed
  inserts, linking; taken into account in every context ([Types](types/native/)).
- **Flexible typing and type inference** — static and dynamic, automatic inference
  ([Types](types/)).
- **Contracts and verification** — pre/postconditions and assertions are checked by an SMT solver (Z3)
  ([Verification](safety/verification/)).
- **Script-like launch** — a file with the shebang `#!.../trust --run` runs directly, like a
  Python script, but with full compilation: transpilation to C++, build and launch of the binary happen
  automatically, no interpreter is needed.
