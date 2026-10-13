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

`:Tensor` is a built-in (pre-declared) type; its LibTorch implementation is provided by the
`tensor.trust` provider and the `tensor_cpu` toolchain plugin. The compiler always knows the type;
without the `with-torch` capability (`-DWITH_TORCH=ON`) using the provider is diagnosed before linking.

The type is defined by its element type and dimensions: `:Float32[2,3]` (more than one dimension is
always a tensor). Alternatively `:Tensor[2,3]` infers the dtype from the initializer (element
annotation `1:Float32`, trailing `]: Float32`, or value types). Values can be created by the
constructor `:Float32[2,3](1,2,3,4,5,6)` or by a literal
`m :Tensor[2,3] := [1,2,3,4,5,6,]:Float32`. One-dimensional `:Float32[3]` remains an array.

Supported: indexing `t[i]`, `t[i,j]` (including negative indices), slicing `t[a..b..step]`,
element-wise arithmetic `+ - * /` (tensor⊗tensor with the same dtype and shape, tensor⊗scalar),
`matmul`, `sum`, `reshape`, and the `rank/size/shape/at` methods. Tensor comparisons and sparse
operations are not implemented yet (explicit diagnostic).

```python
m :Float32[2,2] := [1,2,3,4,];
n := m * 2;
x := n[1,-1];      # 8
```

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

The standard library provides the single iterator type `Iterator<T>` — a class marked with
`@[borrowed]`. It maps onto C++ `trust::Iterator<T>` — a single type-erased cursor available without
an explicit import (loaded by the prelude; disabled by `--no-stdlib`). The cursor is created from a
Trust object `Iterator(obj)` (through `begin()/end()`) or from a pair of standard C++ iterators
`trust::Iterator<T>(first, last)` (used by code generation, including reverse via `rbegin()/rend()`).
The interface is C++-unified: `*it`, `++it`, `it++`, `--it`, `it--`; completion is checked with
`it.%hasNext()` (C++ `explicit operator bool`). Any variable of the type `Iterator<T>` is
automatically dependent on the source object: mutation of the source while the cursor is alive is
the diagnostic `-Wborrowed` at the mutation point (see [Control of derived-view invalidation](../safety/borrowing/)).
The dependency is released by `it = _;` or by binding it to another source. The attribute is
inherited by derived types (`Iterator<Int32>` and others).

## Links

- [Numbers and tensors](numbers/)
- [Dictionaries and sets](dicts/)
- [Type conversion](type_system/)
- [Type system](type_system/)
