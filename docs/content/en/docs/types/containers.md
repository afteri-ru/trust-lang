---
title: Ranges, arrays and tensors
weight: 11
tags: [types, collections]
---

Ranges, arrays and (in perspective) tensors are container (collection) types for working with
sequences and multidimensional data.

## Ranges (:Range)

A range is a type, an approximate analog of a "generator" in Python: it alternately yields elements in a given
interval with a given step. In the text, a range is specified by two or three elements separated by two dots:
`1..5` — from one to five with the default step 1; the step is specified by the third element: `0..10..2`.
As the boundaries and the step, not only literals but also variables and rational
numbers can be used: `0.1..$stop..0.1` — from 0.1 to `$stop` with step 0.1; `0..100:Rational` — a range of rational numbers.

## Arrays (:Array)

One-dimensional arrays are supported by the literals `[1, 2, 3,]` (→ `std::vector<Elem>`), the constructor
`:Array(...)`, access by index `a[i]` and array methods (`count`, `at`, `first`, `push_back`, ...).
Variables with an array type can be created both by value and as reference variables.

The remaining positions of the array can be filled in with the operator `... expr ...` (as the last element) —
the size is taken from the sized target type: `v:Int32[10] := [2, 3, ... 42 ...,];` (see
[Creating values: filling tensors](../operators/create/)).

## Tensors (:Tensor)

Tensors are multidimensional arrays (based on LibTorch). In the current version **multidimensional arrays and full-fledged
tensors are not implemented** (see [Status](../status/)) — the one-dimensional `:Array` and the type `:Range` are implemented.

## Indexing

Access to the elements of an array is performed by an integer index starting at 0:

```python
a := [10, 20, 30,];
x := a[0];   # 10
```

Negative indices (as in Python) are **not supported**: the reference `a[-1]` throws a runtime exception.
Ranges in the index (`a[1..3]`), `:None` values and the ellipsis `...` in the index are also not yet implemented.

## Iterating over collections

Iterating over the elements of an array, range or dictionary is performed by the loop `@while(collection)` with
destructuring of the current element (a ready example — in the
[playground](/en/playground/?file=foreach); more — [Loops](../operators/flow/)).

The standard library provides the iterator type `Iterator<T>` — a class marked with `@[borrowed]`.
It maps onto C++ `trust::AnyIterator<T>` (an erased unified cursor) and is available without explicit
import (loaded by the prelude; disabled by `--no-stdlib`). Any variable of the type `Iterator<T>`
is automatically dependent on the source object: use after mutation of the source —
the diagnostic `-Wborrowed` (see [Control of reference invalidation](../safety/invalidation/)). The attribute
is inherited by derived types (`Iterator<Int32>` and others).

## Links

- [Numbers and tensors](numbers/)
- [Dictionaries and sets](dicts/)
- [Type conversion](type_system/)
- [Type system](type_system/)
