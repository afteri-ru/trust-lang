---
title: Guarantees and safety
tags: [trust-lang, safety, verification, memory]
weight: 50
description: Overview of TrustLang features responsible for code safety and quality
---

This section brings together the *TrustLang* capabilities that are responsible for the **safety and
quality** of programs: safe memory management, protection from data races, correctness control at
compile time and formal verification.

Each capability is described in detail **in exactly one place** (the source page) and is only
mentioned here with a summary and a link. Detailed lists of options/diagnostics are not duplicated —
their source is the tool's help (`trust --help`, `trust -Whelp`, LSP hovers).

## Capabilities of the section

| Capability | Briefly | In detail (source) |
|---|---|---|
| Safe memory management without GC | RAII, strong/weak references, prohibition of strong cyclic references, protection from stack overflow | [Memory](memory/) |
| Protection from races and access synchronization | Automatic inter-thread synchronization when accessing shared data | [Memory](memory/) |
| Contract programming | Pre/postconditions and assertions (`trust_pre`/`trust_post`/`trust_assert`) | [Verification](verification/) |
| Formal verification (SMT) | Automatic execution of Z3 over contracts, loop invariants, quantifiers | [Verification](verification/) |
| Static typing and conversion control | Nominal type checking at compile time; narrowing with range control | [Type system](../types/type_system/) |
| Range checks of numbers | An error when leaving the type range, instead of silent overflow | [Numbers](../types/numbers/) |
| Integer overflow control | Built in and **enabled by default**: overflow of signed arithmetic (UB in C++) → catchable `IntMinus` | [Numbers](../types/numbers/#overflow) |
| Stack overflow protection (stack check) | Stack overflow → catchable `stack_overflow` exception, not a segfault | [stack_check](stack_check/) |
| Control of reference invalidation (borrowed) | Prohibition of using dependent references after mutation of the source (dangling references) | [invalidation](invalidation/) |

> The safe memory model has been ported to C++ in a separate project
> [memsafe](https://github.com/rsashka/memsafe).

## Related topics

- **Multithreading and asynchrony** (coroutines, `:AsyncTask`/`:AsyncPool`) is a
  functional language capability, not a safety feature; it is described in
  [Expressions and control flow](../operators/concurrency/). Only the
  race protection provided by the reference model from [Memory](memory/) is related to safety.
- [Implementation status](../status/) — the boundaries of the implementation of the features of this section.
