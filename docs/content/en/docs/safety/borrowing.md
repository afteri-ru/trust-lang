---
title: Control of reference invalidation (borrowed)
tags: [safety, references, borrowed, borrow]
description: >
    Tracking of dependent references and prohibition of their use after mutation of the source
weight: 70
draft: true
---

Control of reference invalidation (the attribute `@[borrowed]`, the diagnostic `-Wborrowed`) protects against
**dangling references**: a reference/pointer obtained from the data of an object becomes
invalid when that data changes. *TrustLang* tracks such "dependent"
variables and reports their use **after mutation of the source** — at compile time, not
at runtime.

## Model

- **Source** (main variable) — the object from whose data the dependent reference was obtained.
- **Dependent** — a variable pointing into the source's data.
- **Invalidation**: mutation of the source's data makes a previously obtained **view into that data**
  invalid (dangling). Using such a dependent after mutation is the diagnostic
  `-Wborrowed=ignore|warning|error`.
- **Attribute copying**: a variable initialized or assigned from a dependent also
  becomes dependent on the same root source; reassigning with an independent value
  removes the dependency (release).

> **Distinction (important).**
> - **Invalidation / borrowed** — about a **view into data**: an internal field/buffer returned
>   by a method with the `@[borrowed]` contract. Mutation of the source makes such a view dangling. (The index `obj[i]`
>   is NOT included here — it is a copy of the value.)
> - **Lifetime (refcount)** — `shared`/`weak`: a weak observer expires when the
>   last owner is released. This is **not invalidation**, but the normal behavior of the reference counter.
> - **Borrow violation (borrow)** — mutation of the owner/data while a **live borrow** exists:
>   exclusive access cannot be combined with mutation (aliasing XOR mutability). This is diagnosed by
>   the borrow-checker (`-Wborrow-*`), and **not** by borrowed.

## What is tracked

Borrowed tracks only entities with an explicit `@[borrowed]` contract:

- a **class/type** marked with `@[borrowed]` (by contract it holds a view/pointer to someone else's data —
  any variable of such a type is dependent). The attribute is **inherited by derived types**
  (specializations/aliases, e.g. `Iterator<T>`); no separate marking of methods is required;
- a **method** marked with `@[borrowed]` (it returns a view into internal data — the result is dependent
  on the object);
- a **variable** marked with `@[borrowed]` (it explicitly declares the variable dependent).

There is NO auto-tracking by expression form: value-copies (including `obj[i]` — a **copy** of an element) and smart
references (`shared`/`weak`/`unique`) are not dependent. For smart references, mutation of the owner with a
live borrow is a borrow violation (borrow-checker, `-Wborrow-owner-mutated`).

## Example

The `@[borrowed]` contract can be attached to a variable (explicit dependency), to a class/type (all
variables of the type are dependent) or to a method (its result is dependent on the object):

```trust
a := [1, 2, 3,];
@[borrowed@] w := a;        # w is dependent on a
a = [4, 5, 6,];             # ERROR -Wborrowed: modification of the source with a live borrow
```

Removing the dependency — by reassigning with an independent value:

```trust
a := [1, 2, 3,];
b := [9, 9, 9,];
@[borrowed@] w := a;
w = b;                      # borrow removed
a = [4, 5, 6,];             # OK
```

> **Status.** The borrow `&` is defined only for `shared` (→ a weak observer); for the exclusive `unique`
> a borrow is FORBIDDEN (`& u` → error; only move/swap are allowed). Data dependencies (view-objects such as
> iterators/span) are tracked by the `@[borrowed]` attribute (severity `-Wborrowed=`, removed by reassignment).

Tests: `test/lit/.../borrowed/value_copy_not_tracked.src` (a value-copy is not tracked);
borrow diagnostics — `test/lit/.../borrow/*`.
