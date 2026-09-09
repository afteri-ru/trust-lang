---
title: Multithreading and asynchrony
tags: [concurrency, thread, async]
description: >
    Multithreading and asynchrony: concept and implementation status
weight: 75
draft: true
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

## Status

Multithreading and asynchrony are **not implemented** in the current version: there is no `:Thread`, coroutines,
`:AsyncTask`/`:AsyncPool`, Awaitable objects and the operators `@co_yield`/`@co_await`/`@co_return`.
The current list is in [Status](../status/).
