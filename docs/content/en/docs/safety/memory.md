---
title: Memory management and shared access
tags: [syntax, memory-safety, concurrency]
weight: 40
---


No single memory management approach is the best or universal: different approaches win under different conditions:

- Manual memory management can do anything, except one thing — guarantee the absence of errors.
- Ownership and borrowing analysis at compile time makes it possible to prove the correctness of code without additional
runtime checks, but some scenarios and algorithms cannot be proven statically at compile time.
- Counting strong and weak references at runtime is convenient for shared ownership, but has a non-zero
cost and does not protect against cyclic and cross dependencies.
- Garbage collection simplifies working with complex cyclic structures, but has the most expensive overhead
and non-deterministic execution.
- None of the listed memory management approaches includes the issues of inter-thread
memory access synchronization.

Therefore, choosing only one memory management approach in a programming language means deliberately limiting its capabilities and adding unnecessary overhead. For these reasons, the memory management concept of *TrustLang* allows extending the implementation of the memory management model with minimal impact on the language syntax and is defined by the requirements (guarantees) for working with memory.

## Memory management model {#concept}

- The language semantics divides variables not by the method of internal implementation, but by semantics, i.e. a variable by value may store data in the heap, but when copying, a copy of all its data located in the heap must also be created.
- A complete description of an object's lifetime always includes several **orthogonal** axes and no abbreviated symbolic notation can in principle encode the whole set of combinations (reference kind, access, region membership (lifetime), presence of a destructor for the object, etc.). Therefore, the abbreviated symbolic notation of variable types is intended only for variables whose lifetime the compiler can verify automatically at compile time (mainly for **local scopes**).
- The identity of data types includes not only reference types, but also the methods of inter-thread access synchronization.
- Since interaction with C++ code is required, it is impossible to completely abandon raw pointers to data, but strict restrictions are introduced for them at the level of the compiler (static analyzer) in order to [limit errors when working with memory](https://github.com/rsashka/memsafe).
- Working with memory must also include [protection of the stack from overflow](/en/docs/safety/stack_check/).

The current memory management model is at the testing stage and will be documented in [version 0.7.0](/en/docs/status/).
These and other project concepts can be discussed on Reddit https://www.reddit.com/r/trust_lang/

<!--
In all other cases, a semantic description of the object lifetime is used.
The memory management concept in *TrustLang* relies on the requirements (guarantees) that must be provided by the language and consists of the following points:

 and includes [RAII](https://en.wikipedia.org/wiki/Resource_acquisition_is_initialization)

## Lifetime model: orthogonal axes {#rules}

The lifetime of an object is described by several **orthogonal** aspects; each is expressed by its own attribute (one attribute — one aspect).

| Aspect | Attribute | Meaning | Part of the type identity |
|---|---|---|---|
| Ownership (reference kind) | `@[reftype(<kind>)@]` | who owns the object and how | yes |
| Access/synchronization | 2nd/3rd argument of `reftype` | how access is synchronized | yes |
| Object dependency  | `@[borrowing@]` | who owns the object and how | yes |
| Lifetime region | `@[lifetime(<area>[, <name>])@]` | how long a non-owning view is valid | no (analysis only) |
| Destruction | `@[deleter(D@)]` | how the resource is released | yes for `unique`, erased for `shared` |
| Mutability | `@[readonly@]` / `^` | constness of access | qualifier |
| Address stability | `@[pin@]` | the address is part of the identity | **not implemented** |

The reference kind is specified by the attribute `@[reftype("...")@]` (or the symbolic marker `&&`/`&*`/`&?`) before the **type** (i.e. after `:`)
or before the **variable** when automatic type inference of the variable is used.
Nested references are expressed by a chain of separate reference types, not by a "reference to a reference" in one declaration.

## Reference kinds {#variables}

All variants of the reference kind (`RefType`):

| Kind (`@[reftype("…")]`) | Marker | Meaning | C++ type | Copyability |
|---|---|---|---|---|
| `value` | — | ownership of a value (no reference) | `T` | by the type `T` |
| `shared` | `&*` | shared ownership (reference counter) | `trust::Shared<T>` | copied (the counter grows) |
| `weak` | `&?` | weak (non-owning) reference to `shared` | `trust::Weak<trust::Shared<T>>` | copied |
| `unique` | `&&` | exclusive ownership (single owner) | `trust::StaticUnique<T>` (inline) / `trust::Unique<T,D>` (with a deleter) | **move-only** |
| `ptr` | `@[reftype("ptr")@]` | raw pointer (explicit only) | `T*` | copied |
| `ref` | `@[reftype("ref")@]` | native reference (explicit only) | `T&` | — |
| `mptr` | — | pointer to member | `T C::*` | copied |
| `rref` | — | rvalue reference | `T&&` | **reserved, not implemented** |
| `ptrptr` | — | pointer to pointer | `T**` | **reserved, not implemented** |
| `locker` | — | access guard to `shared`/`weak` (result of `lock()`) | `trust::Locker<T>` | move-only |

Examples:

```python
s : @[reftype("shared")@] Int32 := 5;    # canonically: the kind is on the type -> trust::Shared<int32_t>
u : @[reftype("unique")@] Int32 := 10;    # trust::StaticUnique<int32_t> (inline, zero-cost)
w : @[reftype("weak")@] Int32 := s;      # trust::Weak<trust::Shared<int32_t>>
x : &*Int32 := 5;                        # marker on the TYPE: the same as shared
&* y := 7;                               # marker on the VARIABLE, type omitted (auto-inference) -> shared
&? z := & x;                             # weak from shared (auto-inference)
&* bad : Int32 := 5;                     # ERROR: the kind is on the variable, but the explicit type has no kind
&* dup : &*Int32 := 5;                   # WARNING -Wref-kind-dup (redundant qualifier)
```

**Copyability.** `shared`/`weak` are copied; `unique` is **move-only** — copying (initialization/
assignment from another `unique`) is forbidden, ownership transfer is only `a :=: b` (swap) or `a :=: _`.

**Native kinds.** Raw `ptr`/`ref` are specified only by an explicit `@[reftype(...)]` (a transitional compatibility axis
with C++) and are not mixed with smart references.

## Access policies (the access and synchronization axis) {#access}

A policy is the 2nd (and, if necessary, the 3rd — the implementation class) argument of `reftype`; it is part of the type.
Custom access policies are specified only for `shared`/`weak`. `unique` also has a policy, but it is
**fixed** and not parameterizable: exclusive (monopolistic) access of the
single owner.

```python
s : @[reftype("shared", AccessMutex)@] Int32 := 5;    # trust::AccessShared<int32_t, AccessMutex>
```

| Kind | Policies | Access form |
|---|---|---|
| `shared`/`weak` | `AccessMutex`, `AccessRwMutex`, `AccessSingleThread` | guarded capture `lock()`/`lock_const()` |
| `unique` | fixed: exclusive (monopolistic) access | direct access `*u` (zero-cost) |

## Deleter: releasing external resources {#deleter}

The attribute `@[deleter(D)@]` specifies a deleter functor (`D::operator()(V*)`) for **owning** kinds:

```python
h : @[reftype("unique")@] @[deleter(FileDeleter)@] File := open_file(...);
```

- It is allowed only on owning kinds (`unique`/`shared`); on non-owning kinds it is an error.
- For `unique`, `D` is **part of the type**: `trust::Unique<T, D>` (like `std::unique_ptr<T, D>`), therefore
  `unique<T, D1>` and `unique<T, D2>` are different types.
- For `shared`, `D` is **erased** from the type (`trust::Shared<T>`) and is applied at creation/`adopt`.

## Lifetime regions and `@[lifetime@]` {#region}

`@[lifetime(<area>[, <name>])@]` marks a **non-owning view** (`ptr`/`ref`) with a validity boundary (a region). The contract is validated; static region analysis and escape rules for returning from a function are under development. Details — in [Status](../status/).

## Address stability (`@[pin@]`) {#pin}

`@[pin]` (the address is part of the object identity) is **not implemented** in the current version.

## Dereferencing and capture {#lock}

Access to the value of a reference is the dereference operator, which simultaneously performs capture (for
`shared`/`weak` — with locking during synchronization):

- `*ref` — capture for reading/writing;
- `*^ref` — capture for reading only (const);
- `with(l = *ref) { ... }` — holding the capture for the whole block; several references — a group capture.

A repeated capture of the same object in one expression (`*a + *a`) is a warning `-Wdouble-capture`
(for synchronized references this is self-locking).

Guarded access to a synchronized object is provided by the guard `trust::Locker<T>` (for `shared`/`weak`;
the result of `lock()`/`lock_const()`). For `unique` a policy is also defined — exclusive
(monopolistic) access: there is only one owner, therefore access `*u` is direct, without a runtime guard;
raw `ptr`/`ref` are also without a guard.

## Move and exchange of ownership {#move}

The operator `:=:`:

- `a :=: b` — exchange of the references (ownerships) themselves of the same kind;
- `*a :=: *b` — exchange of values through the references;
- `x :=: _` — move to discard (the owner remains moved-from; for `unique`/`shared` the resource is
  released automatically);
- `@move(x)` → `x :=: _`, `@swap(a, b)` → `a :=: b`.

-->

