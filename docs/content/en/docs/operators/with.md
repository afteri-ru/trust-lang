---
title: Context manager (with)
tags: [operators, context, raii, references]
description: >
    Group capture of references and automatic resource release (RAII)
weight: 40
---

The single statement for capturing a reference and synchronizing access to an object (`*ref`, `*ref(...)`)
is executed for one action. Capturing a reference/synchronization object is a relatively slow
operation, so performing it for each action is inefficient. The **context manager** (`with`)
captures several references/objects at once and performs a group of actions under a common lock,
and also serves for **automatic resource release (RAII)** when leaving the block.

In the base syntax — the operator `**( ... )`; in DSL it corresponds to the macro `@with`
(see [Keyword syntax (DSL)](../syntax/macros/)). Comparison of the notation forms using
the example of capturing-locking a reference:

<table>
<tr><th>DSL macros</th><th>Base syntax</th></tr>
<tr><td><pre><code>@with(lock = *ref) {
    ...   # critical section
};</code></pre></td><td><pre><code>**( lock = *ref ) {
    ...   # critical section
};
</code></pre></td></tr>
</table>

## Capturing and locking references {#lock}

Reference variables are declared with the attributes `@[reftype("shared")@]`/`@[reftype("weak")@]` and
are transpiled into `trust::Shared`/`trust::Weak`; the take operator `*ref`/`*^ref` captures a reference
(`lock`/`lock_const`) and returns a guarded access `trust::Locker<T>`. More about reference
types and the reference model — in [Memory management](../safety/memory/).

`with` stores a list of `(lock, binding)` pairs and binds the captured references to names inside the body:

```python
@with(lock = *ref) {     # capture-lock of the reference ref
    ...                  # critical section
} @else { ... };         # capture error (ref == nullptr) → else
```

- `with(lock = *ref)` — creates a temporary lock `auto __cap = ref.lock();`, the binding `lock`
  is automatically dereferenced to the value; calling `lock()` throws `IntMinus` on failure → the `else` branch.
- `with(_ = *ref)` — capture of the reference for the whole body without binding the value.
- `with(*ref)` — a temporary lock: it is released immediately, the body is executed without a lock.
- Binding a reference variable without `*`/`*^` (`with(v = ref)`) — a compiler warning.

## Automatic resource release (RAII) {#raii}

Besides capturing references, the context manager allows allocating and releasing resources: when leaving
the block the [object destructor](../types/class/) is called (for objects by value). The release
occurs before entering the `else` branch, if there is one:

```python
@with(name = expr, ...) {  # RAII capture of values
    ...
} @else { ... };           # on an exception in the initializer expr
```

This guarantees the closing of files, the release of captured resources, etc., regardless of how
the nested code finished (normally, by an interrupt or by an error).

## Implementation status

The forms `&`/`&?`/`&&`, the `__timeout__` parameter, inter-thread synchronization and the types `:Class`/`:Thread`
are **not implemented** in the current version (see [Status](../status/)). Working: reference variables
`@[reftype(...)]`, capture of references `with(...)` with the `else` branch and RAII capture of values `with(name=expr)`.
