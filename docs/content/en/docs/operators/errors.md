---
title: Return, interrupts and error handling
tags: [operators, errors, control-flow]
weight: 50
---

A sequence of commands (a [code block](../syntax/#block)) can be interrupted at any point by
special interrupt statements. *TrustLang* uses two kinds of interrupts, which in
meaning correspond to their notation:

- **"positive"** interrupt `++` — return of a value/through-flow (analog of `return`);
- **"negative"** interrupt `--` — error (analog of `throw`).

An interrupt can return a value: `++ 'String' ++` — return a string, `-- err --` — "throw"
an error (a rough analog of `throw err`). If no value is specified, the interrupt returns nothing
(an empty value).

## Named interrupts: local jump and return {#named}

Named [code blocks](../syntax/#block) can act as local labels for control-flow
transfer. If a namespace/block label is specified before the interrupt statement, such a
**named** interrupt acts only for the specified block:

- `block ++` — **break** by the label `block` (exit from the block);
- `block -+` — **continue** by the label (jump to the first statement of the block).

The same fragment — a named loop with an exit by label and a jump to the beginning of the block — in two
forms:

<table>
<tr><th>DSL macros</th><th>Base syntax</th></tr>
<tr><td><pre><code>outer {
    @loop {
        @break outer;     # break by label outer (like outer ++)
        @continue outer;  # continue by label outer (like outer -+)
    };
};</code></pre></td><td><pre><code>outer {
    [1] &lt;-&gt; {
        outer ++;   # break by label outer
        outer -+;   # continue by label outer
    };
};
</code></pre></td></tr>
</table>

The function body is a code block whose name matches the full name of the function. Therefore a "positive"
interrupt by the function label returns the result from it (`func_name ++ value ++`); in DSL this
corresponds to `@return`.


## Capturing interrupts (try/catch) {#catch}

Unnamed interrupts can be "caught". For capturing, code blocks with a special
semantics are used, which wrap the body in `try/catch` by the interrupt class:

- `{+ ... +}` — captures positive interrupts (`IntPlus`);
- `{- ... -}` — captures negative interrupts (`IntMinus`);
- `{* ... *}` — captures both types (`IntAny`).

Implementation status (important clarifications):

- an **unnamed** `++ value ++` *always* throws `IntPlus` (positive/through), and
  `-- err --` — `IntMinus` (error), regardless of the presence and depth of handlers;
- a **named** `func ++ value ++` — an ordinary `return` from the function (the label must be the current
  function or the global `::`);
- an uncaught class is propagated through; a through `++` passes through function calls up to
  the nearest `{+}`/`{*}`;
- capturing a value (for example, via `match`) is not yet implemented — a captured interrupt
  is absorbed.

## Error handling

The "negative" interrupt is used to return an error only conventionally — both kinds of interrupts
are equivalent. Joint processing of a captured value in the classic try/catch style —
through capture blocks (`{*}`, `{-}`, `{+}`) and the [expression evaluation](flow/) operator — is **not yet
implemented**: a captured interrupt is absorbed (see [Status](../status/)).

## See also

- [Context manager `with`](../operators/with/) — RAII and resource release when leaving a block.
- [Multithreading and asynchrony](../operators/concurrency/) — planned coroutine interrupts
  (`@co_yield`/`@co_await`/`@co_return` are not implemented).
- DSL equivalents (`@return`, `@break`, `@continue`) — [Keyword syntax (DSL)](../syntax/macros/).
