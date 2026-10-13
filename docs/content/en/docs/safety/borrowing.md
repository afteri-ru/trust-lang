---
title: Derived view invalidation control
tags: [safety, references, borrowed, borrow]
description: >
    Tracking of derived views (iterators/observers) and prohibition of source mutation while
    they are alive
weight: 70
# draft: true
---

Invalidation control protects against **dangling/stale views**: a view (pointer, iterator) obtained
from an object's data becomes invalid when that data changes. *TrustLang* tracks such dependent
variables and reports **mutation of the source while dependents are alive** — at compile time,
not at runtime.

> **Important: this is not the Rust borrowing model.** TrustLang has no "borrow" as a language
> entity: there are no exclusive/shared loans, two-phase borrows, loan liveness, or the
> "aliasing XOR mutability" rule. `borrow` is the name of the mechanism; what is checked
> is the **invalidation of a derived view when the source is mutated/moved**. The full
> specification is `include/semantic/ref/BORROW_CHECKER.md` in the sources.

## Model

- **Source** (owner) — the object whose data the view was obtained from.
- **Derived view** (dependent) — a variable addressing the source's data: a view object
  (iterator/span), the result of a `@[borrowed]` method, a native reference/pointer, or a `&?`
  observer (`weak`) obtained from `shared`.
- **Invalidation**: mutating/moving/resetting the source makes a previously obtained view invalid.
  Therefore the diagnostic is reported **at the mutation point**, not on later use.
- **Release**: `dep = _;`, rebinding (`dep = other;`), or the view leaving its scope.
- **Ordering**: a view created **after** a mutation is valid; only views created before it are
  invalidated.
- **Flow-sensitive analysis**: view liveness is computed over the CFG (branches, loops,
  `break`/`continue`/`return`/`throw`); for `&?` observers the last use is taken into account.
  Same-named variables in different scopes are never conflated.
- **Escape**: returning a `@[borrowed]` view or storing it into a non-view place is reported as
  `borrow-escape`; a `&?` observer is refcount-safe.

## Three distinct mechanisms

| Mechanism | What it controls | Diagnostic |
|---|---|---|
| The `@[borrowed]` attribute | the contract "this object holds a view into someone else's data"; set on a class/type, method, or variable | `-Wborrowed=ignore\|warning\|error` |
| Smart-reference views | a `&?` observer from `shared`: source mutation/move while the observer is alive | `-Wborrow-owner-mutated`, `-Wborrow-owner-moved`, `-Wborrow-region-mismatch`, `-Wborrow-escape` |
| `Shared::borrow()` | a **runtime** observer (`SharedBorrowedRef`): expiry is checked at runtime (`valid()`/`access()`) | — (not a static analysis) |

## What the `@[borrowed]` attribute tracks

- a **class/type** marked with `@[borrowed]` (by contract it holds a view/pointer to someone else's
  data — any variable of such a type is dependent). The attribute is **inherited by derived types**
  (specializations/aliases, e.g. `Iterator<T>`); no separate marking of methods is required;
- a **method** marked with `@[borrowed]` (it returns a view into internal data — the result is
  dependent on the object);
- a **variable** marked with `@[borrowed]` (it explicitly declares the variable dependent).

There is NO auto-tracking by expression form: value-copies (including `obj[i]` — a **copy** of an
element) are not dependent. For smart references (`shared`/`weak`) their views are tracked by a
separate analyzer (`-Wborrow-*`); `unique` is monopolistic ownership and has no views/aliases.

## What is NOT a violation

- Mutating a `shared` source while a `weak` observer is alive is normal refcount behavior
  (the observer expires safely; `-Wborrow-owner-mutated` does not fire for `shared`);
- a value-copy (`obj[i]`, copying an object) is not a view;
- expiry of `weak`/`SharedBorrowedRef` is a runtime check (`valid()`/`access()`), not static
  invalidation.

## Examples

The `@[borrowed]` contract can be attached to a variable (explicit dependency), to a class/type
(all variables of the type are dependent), or to a method (its result is dependent on the object):

```trust
a := [1, 2, 3,];
@[borrowed@] w := a;        # w is dependent on a
a = [4, 5, 6,];             # ERROR -Wborrowed: source mutation with a live view
```

Removing the dependency — by reassigning with an independent value:

```trust
a := [1, 2, 3,];
b := [9, 9, 9,];
@[borrowed@] w := a;
w = b;                      # dependency removed
a = [4, 5, 6,];             # OK
```

A view from `shared` (`&?` — a weak observer); moving/resetting the source while the observer is
alive is `-Wborrow-owner-moved`:

```trust
&* x := 5;
&? w := & x;
x :=: _;                    # ERROR: source moved while a view is alive
```

## Regions and lifetimes

The view's lifetime must not exceed the source's lifetime (`borrow-region-mismatch`). The region is
inferred from the storage: local/temporary — `scope`, `@[thread_local]` — `thread`,
static/global — `program`. For a refcount observer, exceeding the source's region is not UB
(expiry is safe) — it is a contract check; native references are governed by D3–D7.

> **Status.** The operator `&` is defined only for `shared` (→ a weak observer); for the exclusive
> `unique` a borrow is FORBIDDEN (`& u` → error; only move/swap are allowed). The analysis is
> flow-sensitive (CFG, branches/loops/early exits); interprocedural effects are not modelled
> (parameters are passed by value).

Tests: `test/lit/.../borrowed/value_copy_not_tracked.src` (a value-copy is not tracked);
invalidation diagnostics — `test/lit/.../borrow/*`.
