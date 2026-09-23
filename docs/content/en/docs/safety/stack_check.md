---
title: Stack overflow protection (stack check)
tags: [safety, stack-check, recursion]
description: >
    Turning a stack overflow into a catchable exception instead of abnormal termination
weight: 60
---

*TrustLang* has built-in [stack overflow protection](https://github.com/afteri-ru/stack-check). Before calling a protected function, the free space on the thread stack is checked, and if it is insufficient — instead of abnormal termination of the whole application (`segmentation fault`), a **program exception `stack_overflow`** is created, which can be caught and handled inside the application, after which program execution continues.

A detailed description of the implementation and usage methods will be here.

For now you can look at usage examples: [recursive computation of Fibonacci numbers](/en/playground/?file=fibonacci) and [recursive primality check](/en/playground/?file=prime)



<!--

## The `@[stack_check@]` attribute

Protected functions are marked with the attribute `@[stack_check@]`:

- `@[stack_check@]` — before each call of the function, its maximum stack size from the
  `.stack_sizes` section is checked (`check_stack_limit`, a build with `-fstack-size-section` is required);
- `@[stack_check(N)@]` — it is checked that at least `N` bytes plus the reserve are free
  (`check_overflow(N)`).

An example of recursion protected by the attribute (the overflow is caught rather than crashing the program):

```trust
@[ stack_check @]
%fib(n:BigInteger):Tuple(sum:BigInteger, fib:BigInteger) := {
    if (n < 3) {
        $one:BigInteger := 1;
        return :Tuple($one, $one);
    };
    p := fib(n - 1);
    return (p.0 + p.1, p.sum,):Tuple;
};

@main(argv^:Dict, args^:Dict) := {
    try_error {
        fib(1000000);
    } catch(_) {
        @print('STACK OVERFLOW caught\\n');
    };
    return 0;
};
```

## Control modes (`--stack-check=<mode>`)

A monotonic scale of modes (`trust --help`):

- `off` — control is disabled (attributes are no-ops, recursion is not analyzed);
- `explicit` (default) — checks only for explicitly marked functions and explicit calls;
- `recursion` — like `explicit` plus diagnostics for unprotected recursive functions
  (`-Wstack-check-infer`);
- `auto` — like `recursion` plus auto-marking of recursive functions.

The protection reserve is set by `--stack-check-reserve=<bytes>` (by default — the runtime default `8192`).
Working examples — [playground fibonacci](/en/playground/?file=fibonacci); ready-made stack control tests —
`test/lit/.../e2e/stack/*`.

## Runtime control functions

For explicit control, the native functions `%trust_stack_check(N)`,
`%trust_stack_check_set_reserve(...)`, `%trust_stack_check_get_reserve()`,
`%trust_stack_check_get_limit()` and `%trust_stack_check_set_limit(...)` are available. Full signatures — in the
help `trust --help` / `-Whelp`.
-->
