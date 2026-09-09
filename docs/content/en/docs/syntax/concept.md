---
title: Concept
tags: [syntax, language-design]
description: Concept and rules of the TrustLang programming language syntax
weight: 10
# no_list: true
---

## Fundamental notions

The *TrustLang* syntax has several rules common to the whole language:

- The language is built on **grammar rules**, not on keywords. The lexer and parser
  are single-pass, simple and fast, while the usual keyword notation
  (`@if`, `@while`, `@func`, ...) is implemented on top of the base syntax through
  [macros](macros/).
- Terms of the source text are first recognized by the lexer and processed by the **macro processor**,
  and only then reach the parser. This makes it possible to modify even basic syntax constructs with macros.
- Statements (operators) are separated by a semicolon `;`.
- Code blocks are written in curly braces `{ ... }`.
- Any construct (including a code block) is an expression and
  [returns a value](/en/blog/language-design/expression-statement/), so it can be nested
  inside other expressions.

The *TrustLang* syntax can be conventionally divided into several layers, each responsible
for its own area:

1. **Algorithmic layer** — the minimally required basis describing the algorithm itself:
   literals, [named objects](naming/), operators and control-flow constructs
   ([Operators](../operators/)). This layer is optimized for machine parsing and contains no
   "syntactic sugar".
2. **Formatting layer** — means of formatting and simplifying the notation that do not affect the algorithm:
   [macros](macros/), auxiliary checks.
3. **Attributes** — a separate layer for recording properties of elements that do not change the algorithm,
   but affect the details of its implementation (optimizations, integration with C/C++).
4. **Verification** — the layer of [contracts](../safety/verification/), which does not intersect with the others
   and affects neither the algorithm nor its implementation, but serves to prove the correctness of the algorithm.

## Code blocks {#block}

A sequence of consecutive statements is grouped into a **code block** `{ ... }`, which
is executed as a whole and is terminated by a semicolon `;`. A code block returns the value
of its last statement; the type of that value can be restricted explicitly: `{ ... }:Type`.

Code blocks are used to limit the scope of variables (see
[Code hierarchy](../syntax/hierarchy/)) and can be named — then they act as local
control-transfer labels (see [Return, interrupts and errors](../operators/errors/)).
The function body is also a code block in the namespace of its definition. Capturing and interrupting
the flow inside a block are described in the section [Error handling](../operators/errors/).

A code block with **native syntax** `{% ... %}` contains source text in the implementation language
(C++), which is inserted into the output text directly (see [Native types](../types/native/)).
