---
title: Numbers and tensors
weight: 2
tags: [types, numbers]
params:
  math: true
---

Numeric types are simple (scalar) data types of *TrustLang*. The basics of typing are described on the page
[Type system](type_system/), while arrays, ranges and (in perspective) tensors — in the section
[Ranges, arrays and tensors](containers/).

## Integers

The names of integer types correspond to their bit width: `:Int8`, `:Int16`, `:Int32`, `:Int64`.
An integer literal gets the minimum signed type into which its value fits
(for example, `5` → `:Int8`), and a value that does not fit into `:Int64` becomes `:BigInteger`.
The type of a literal can be set by the explicit annotation `literal :Type` (for example, `1 :Bool`, `255 :Int16`) —
when leaving the type range, a compilation error occurs, not a silent overflow.

For interaction with native C++ code, integers have unsigned synonyms
(`:Byte`, `:Word`, `:DWord`, `:UInt16`/`:UInt32`/`:UInt64`, ...), which are used when calling
[native functions](native/). In arithmetic, signed types are preferred — this way the classic problems of
unsigned arithmetic are avoided (for example, a subtraction yielding a huge "negative"
value).

### Integer overflow control {#overflow}

Arithmetic of signed machine integers (`+`, `-`, `*` and the compound `+=`, `-=`, `*=`) is controlled for
overflow: the result of the operation is checked by the built-in function `__builtin_*_overflow`, and on
overflow a **program exception `trust::IntMinus`** is thrown (caught by the trust block
`{- ... -}` and the `try` construct), rather than undefined behavior (UB), as in C++.
The exception message contains the source code location in the format `file:line:` (as with `@assert`),
for example `rational.src:8: integer overflow in '*='`.

The option `-foverflow-check` / `-fno-overflow-check` (enabled by default) controls the checking.
Only **signed** machine integers are checked (`:Int8`…`:Int64`); unsigned types by
definition wrap, `:BigInteger`/`:Rational` have arbitrary precision, and `/`,
`//`, `%` and bitwise operations do not overflow and are not checked.

Additionally, **unary minus** is controlled (`-x`: overflow at `-INT_MIN`) and
**integer division** `//`/`//=` — safeguards against division by zero and against `INT64_MIN / -1`
(undefined behavior in C++). All violations also lead to a catchable `trust::IntMinus`.

### Boolean type {#bool}

The boolean type `:Bool` accepts only the values `0` or `1` (`false`/`true`). It is singled out into a separate
category (the values `@true`/`@false` in DSL are `1 :Bool`/`0 :Bool`) and, depending on the operation, can
behave as an integer. An explicit `0 :Bool`/`1 :Bool` annotation creates exactly a logical value.

## Floating-point numbers

Floating-point types correspond to the bit width: `:Float16`, `:Float32`, `:Float64` (synonyms
`:Single`, `:Double`). The digits of a number can be separated by an underscore for readability: `10_000`.

## Rational numbers and arbitrary precision {#rational}

For calculations with unlimited precision, **rational numbers** are used: it is the fraction
\\( \\frac{m}{n} \\), where the numerator *m* is an integer and the denominator *n* is a natural number (greater than zero).
The backslash is used as the fraction separator: `1\1` — one, `-55\3` — the fraction
\\( \\frac{-55}{3} \\). The arithmetic of rational numbers is built on `:BigInteger` (GMP) and does not lose precision;
mixing with floating-point numbers requires an explicit conversion.

### Displaying long numbers {#bigint-display}

The full value of a number of arbitrary precision can be very long. For **display** (in logs,
reports, diagnostics), `:BigInteger` has the method `display(L)`, which returns a string of the form
`head...tail (N)`, where `L` is the number of digits at the beginning and end, and `N` is the total number of decimal digits:

```python
x :BigInteger := 123456789012345678901234567890;
@print('{}', x.display(3));   # 123...890 (30)
```

If there are few digits (`N <= 2L + 1`), the full number is returned. The sign is output in front, and `N`
counts digits without taking the sign into account.

The method `display(L)` is intended **only for display**: its result is a string from which the exact value of the number cannot be
recovered (it is not a round-trip representation). The exact decimal
representation is given by the ordinary output of the value (`{}` in the format string).

A similar method also exists for rational numbers `:Rational`: `display(L)` shortens **each**
component of the fraction independently (the numerator and the denominator are `:BigInteger`), for example
`123...890 (30)\45...678 (5)`.

## See also

- [Type system](type_system/)
- [Type conversion](type_system/)
- [Ranges, arrays and tensors](containers/)
