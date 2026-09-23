---
title: Correctness proof
tags: [syntax, verification]
description: >
    Trust contracts (pre/postconditions and assertions) and their checking by an SMT solver (AoRTE)
weight: 80
draft: true
---

TrustLang provides **static checking of dynamic expressions** — a formal analysis of
AoRTE ("Absence of Run-Time Errors") following the gnatprove principle in Ada: the compiler checks
the correctness of **user** assertions at compile time, rather than proving the
correctness of the program as a whole.

The analysis does not produce false positives, although false negatives are possible: if there are no
errors at compilation — there are no problems in the checked places; an indication of a possible error does not always
correspond to reality (the tool can be wrong).

## Trust constructs (pre/postconditions and assertions)

Preconditions, postconditions and assertions are specified by three mnemonics of DSL macros (or their
"raw" marker equivalents):

- precondition `trust_pre( <boolean expression> )` ↔ `@{ pre --> <expression> @}`;
- postcondition `trust_post( <boolean expression> )` ↔ `@{ post --> <expression> @}`;
- assertion `trust_assert( <boolean expression> )` ↔ `@{ check --> <expression> @}`;
- invariant `trust_invariant( <boolean expression> )` ↔ `@{ invariant --> <expression> @}`.

Placement rules:

- pre- and postconditions are specified after the function name in its definition;
- an assertion — after the name of a variable/type or as an autonomous statement `@{ <kind> --> ... @};` in the body;
- in the precondition, the parameter names are available, but using the name of the function itself is an error;
- in the postcondition, the function name means the return value and is available along with the parameters;
- the type on which an assertion is specified cannot be inferred automatically (`x := ...`) — an
  explicit annotation is needed (`x :T := ...`).

## How this is checked

The compiler checks trust constructs in two ways:

- **runtime checks** — the conditions are embedded in the code as checks (a unified mechanism of
  `@assert`/trust conditions);
- **SMT verification (Z3)** — from the conditions a task is generated (SMT-LIB 2, `.smt2`) and either
  exported for an external solver, or executed automatically with a message about the
  counterexample on violation.

The behavior is set by the options `-Wsolver` and `--solver-mode`; the values and auxiliary flags
need not be listed here — they are available in the help `trust --help` / `trust -Whelp`.

## Examples

### Function contracts

The precondition `trust_pre`, the postcondition `trust_post`:

```trust
func(x:Int32):Int32
    trust_pre( x > 0 )          # precondition
    trust_post( func > x )      # postcondition; func is the return value
:= { x + 1 };
```

### Loops and invariants

A loop is verified by induction over an explicit invariant before the loop:

```trust
@{ invariant --> i >= 0 @};        # invariant before the loop
@while ( i < n ) { ... };
```

Bounded unrolling of the loop (per loop) — by the term `z3_unroll(N)` inside the invariant
contract:

```trust
@{ invariant --> z3_unroll(3) @};  # unroll the loop for 3 iterations
@while ( cond ) { ... };
```

### Quantifiers and auxiliary values

`z3_forall(i, P)` / `z3_exists(i, P)` — quantifiers over the bound variable `i` (declared
earlier); `z3_old(x)` — the initial value, `z3_result` — the result:

```trust
trust_post( z3_forall(i, 0 <= i and i < count) => arr[i] > 0 )
```

## Limitations and status

Implemented: function contracts (pre/postconditions, assertions; integers → BitVec; body — SSA),
branching (`if`/`else` → `ite`), loops with invariant induction or unrolling `z3_unroll(N)`,
interprocedural calls (contract axiom), arrays (`select`/`store`), autonomous/variable and
type assertions, bitwise operators. Uncovered constructs do not participate in the proof.
More — in [Implementation status and limitations](../status/).
