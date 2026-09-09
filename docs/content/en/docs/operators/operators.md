---
title: Operators
tags: [operators, control-flow]
weight: 20
---

## Arithmetic operators

The arithmetic operators of *TrustLang* are applicable to [numeric types](../types/numbers/) and to assignment
forms. Each binary operator has a paired analog `op=`, which performs the operation and
assigns the result to the left operand (`a += 1;`):

| Operator | Action |
|---|---|
| `+` / `+=` | addition |
| `-` / `-=` | subtraction |
| `*` / `*=` | multiplication (also applicable for repeating strings) |
| `/` / `/=` | division (the result is a floating-point number) |
| `//` / `//=` | integer division rounded toward the smaller value (as in Python) |
| `%` / `%=` | remainder of integer division |

There is no separate exponentiation operator (the power is computed by a function/repeated multiplication).
Arithmetic of rational numbers and `:BigInteger` is a separate strict branch: mixing with floating-point
numbers requires an explicit conversion (see [Numbers and tensors](../types/numbers/)).

### Bitwise operators {#bitwise}

The notations `.<.` (shift left), `.>.`/`.>>.` (shift right), `.&.`, `.|.`, `.^.`, `.~.`
(AND/OR/XOR/NOT) are recognized by the lexer and **supported in SMT verification of contracts**
(translated into the BV operators `bvshl`/`bvashr`/`bvand`/`bvor`/`bvxor`/`bvnot`). Generation of
**executable** C++ code for them is **not yet implemented** (planned) — in `.cppt` the operator
is output as is. Status — in the section [Implementation status](../status/).

## Arithmetic comparison

The arithmetic comparison operators are the classic `<`, `>`, `<=`, `>=`, `==`, `!=`.
They are used to compare scalars and rational numbers; the equality operators `==` and `!=`
also allow comparing composite values.

## Value comparison

- `==` and `!=` — comparison with automatic casting of compatible types for any objects.
- `===` and `!==` — exact comparison for any objects (no automatic casting is performed);
  for objects, the placement addresses are compared.

## Type comparison

The type comparison operators return `Bool` and differ in the strictness of the check:

| Operator | Meaning |
|---|---|
| `<~` | **nominal** check: the type of the left operand matches the right type or is present in its inheritance hierarchy |
| `~~` | **duck** (structural, non-strict): the left operand has all fields of the pattern on the right side |
| `~~~` | **strict**: nominal identity of the type or an exact match of the pattern field set |

The right operand is a type name (`:Class`), a string literal with a type name or a pattern dictionary
(`(field1=_, field2=0,)`; an empty dictionary is `(,)`). The left operand is a value (its type is checked)
or a type name (`:Base <~ :Derived`).

```python
dog <~ :Animal;             # type of dog is Animal or its descendant
dog ~~~ :Dog;               # exact type identity
:Dog <~ :Animal;            # Dog is a subtype of Animal (compile-time check)
:Animal <~ :Dog;            # false (the reverse relation — by swapping the operands)
d ~~ (field1=_,);           # d has the field field1
d ~~~ (field1=_, field2=0,);# exact set of fields
```

These operators have no negations — the `Bool` result is checked as usual: the `else` branch
in `if`/`match`, or a comparison with `false`.

### Static and dynamic checking

- **Static (the main path):** if the type of the left operand is inferred at compile time, the result
  is folded into the constant `true`/`false` (inheritance — by the type registry). Zero cost
  in the generated code.
- **Dynamic:** checks that require the runtime type of a value (erased `Any` values/elements of a dictionary
  with an unknown type), as well as a type name from a string **variable**, are **not yet implemented** —
  an explicit compilation diagnostic is issued.
