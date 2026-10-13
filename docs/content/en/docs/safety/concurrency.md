---
title: Multithreading and asynchrony
tags: [concurrency, thread, async]
description: >
    Multithreading and asynchrony: concept and implementation status
weight: 75
# draft: true
---

## Model of multithreaded and asynchronous programming

Multithreaded and asynchronous programming serve to increase the performance of a single application
and the completeness of using computing resources. The choice of the model depends on the type of load:

- **CPU-bound** — processor load: multithreading is suitable (thread switching is performed by the OS);
- **I/O-bound** — waiting for data: asynchrony on coroutines is suitable, where switching
  occurs only at the moments of waiting.

## Asynchrony and coroutines

Asynchronous code preserves the linear structure of the algorithm without callback functions: a function
returning an Awaitable object suspends when there is no data and resumes later.
To distribute tasks across cores, `:AsyncTask`/`:AsyncPool` are provided.

## Data synchronization

Access to shared data is provided by the reference model ([Memory](../safety/memory/)): capturing
a reference synchronizes access to the object.

## Library async layer {#async-layer}

The types are declared in the stdlib prelude (`async.src`) and are available without an import:

| TrustLang | C++ | Purpose |
|---|---|---|
| `AsyncTask<T>` | `trust::Task<T>` | lazy task; starts on `co_await` or `sync_wait` |
| `AsyncGenerator<T>` | `trust::AsyncGenerator<T>` | pull generator; `next()` → `Task<std::optional<T>>`, single consumer |
| `AsyncPool` | `trust::ThreadPool` | thread pool; `co_await pool.schedule()` resumes the coroutine on a worker |
| `AsyncMutex` | `trust::AsyncMutex` | async mutex with FIFO waiting |

`trust::sync_wait(task)` blocks the current thread until the task completes and rethrows its
exception; it must not be called on a `ThreadPool` worker (the worker would be blocked).

The headers live in `@stdlib/async/*.hpp` and use standard C++ only (`<coroutine>`, `<thread>`,
`<mutex>`, `<condition_variable>`). `-pthread` is linked on demand — only when `AsyncPool` is used.

## Status

The language-level coroutine mechanism is **not implemented** yet: there is no `:Thread`, no
`co_await`/`co_yield`/`co_return` code generation and no awaitable type deduction (`@[awaitable]`).
Coroutine functions are marked with the `@[coroutine]` attribute (the marker is registered).
The library async layer (`trust::async`) is usable from native declarations and C++ embed
blocks — see [Library async layer](#async-layer).
The current list is in [Status](../status/).
