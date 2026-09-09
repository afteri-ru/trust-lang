---
title: "Getting rid of Segmentation fault errors due to stack overflow in C++"
slug: stack-overflow
date: 2026-01-07
tags: [memory-safety, c++, verification, runtime]
---


January 7 at 14:53 2026



Developing the idea of a [trusted programming language](/en/blog/safety/language-guarantees/), I came to the conclusion that, through restrictions of the syntax and the creation of corresponding checks in the static code analyzer, it is possible to protect against practically all technical errors except two — the control of dynamically allocated memory and stack overflow.

Moreover, while solutions exist for reference counting at runtime, stack overflow control cannot be done not only during the analysis of the source text of the program, but it is practically impossible even during the execution of the application! After all, a stack overflow error is always fatal, since there is no way to catch and handle this error from inside the running program in order to then continue its execution as if nothing had happened.

Is there at least a theoretical possibility to protect against stack overflow errors and turn it into an ordinary error (exception) that can be caught (handled) in the application itself, so that it is possible to continue the execution of the program without fear of a subsequent segmentation fault or stack smashing?

[Skip the lyrics and go straight to the description of the solution](#body)

## A bit of technical information about the stack

The program stack is a small area of RAM in which local variables and function return addresses are stored. When the depth of calls or the size of local data exceeds the limit allotted to them, the next write to the stack goes beyond its boundaries, which is the cause of the error. Moreover, if the error handler code also needs stack, but it is already exhausted, then it is impossible to reliably perform the error handling — since there is no memory in the stack for temporary data.

Some operating systems deliberately create a protective lower boundary of the stack, an attempt to go beyond which leads to an immediate abnormal termination of the application; and even if the programming language allows catching the stack overflow error, the state of the thread is already violated: the data is damaged, and continuing the work of the application is unsafe.

That is precisely why a stack overflow almost always ends in an abnormal stop of the thread or of the whole process. Only a correct termination of the application with a report/memory dump is allowed, without continuing the program's work.

The problem is further aggravated by the fact that it is impossible in principle to compute the stack size for a function during AST analysis, since this is information exclusively of compile time:
- The stack size of a function can depend on the input data of the function (for example alloca and VLA in the C language)
- The compiler may add unused space between variables for their alignment (Padding).
- The registers that the function must restore before returning (`callee-saved registers`), the function return address and the return data are saved on the stack. Moreover, all this depends not only on the features of a specific hardware platform, but also on the [function calling convention](https://en.wikipedia.org/wiki/Calling_convention).
- The compiler may create unnamed temporary variables on the stack to store intermediate results of computations.
- The optimizer may completely remove some variables or store them only in registers, without using stack memory for this.
- There is also no universal method for determining the free space on the stack, since it depends both on the hardware platform and on the specific operating system.

Moreover, information about the stack size of a function is usually not available during the execution of the application either, because a program is machine instructions, and to find out the size of the stack used by a specific function one needs to decode (disassemble) its code for analysis.

# Causes of stack overflow and protection mechanisms

At the basis of the theory of algorithms lies the abstract Turing machine, which has infinite computing resources (infinite memory and unlimited execution time), thanks to which any algorithms are allowed, including those with infinite recursion depth.

However, the resources of real computers are always finite: memory, and accordingly the stack size, are finite. Because of this, although recursive algorithms exist, **unlimited recursion** is impossible in practice. In addition, the problem of determining the termination of an arbitrary program (the halting problem) for a Turing machine is in principle unsolvable, therefore no analysis of a program at compile time is capable of reliably predicting the correctness of the program in the general case, nor the maximum stack depth required for its execution.

Therefore the stack size is always set in advance:
- When linking/loading the process into RAM or when creating an application thread.
- The maximum stack size is reserved at the OS level and cannot be increased during the life of the thread (although sometimes [attempts are made to dynamically increase the stack size during the operation of the application](https://learn.microsoft.com/en-us/cpp/c-runtime-library/reference/resetstkoflw), [Segmented Stacks in LLVM](https://releases.llvm.org/3.0/docs/SegmentedStacks.html) and [Split Stacks in GCC](https://gcc.gnu.org/wiki/SplitStacks))
- In some programming languages, the runtimes can set custom stack sizes, but this does not change the limits for the system thread and still runs into the overall memory limit.

<spoiler title="There are many stack protection mechanisms:">

Software, at the compiler level:
- Stack canaries (stack cookies): inserting protective markers in function prologues, checked in the epilogue.
- Probing/step-by-step "probing" of the stack for large frames: guarantees that the OS manages to activate the guard pages, preventing a "collision" with other memory regions.
- Stack-clash protection checks for large local allocations.
- Runtime sanitizers: detecting overflows of local buffers, going beyond their boundaries
- Separating the "safe" and the general stack (safe stack) to minimize the possibilities of overwriting sensitive data
- Tail-call optimization and tail recursion elimination: converting tail calls into iterative jumps without stack growth.
- An alternative stack for signal/exception handlers, so as to correctly record an emergency report when the main stack overflows.

Hardware and system stack protection mechanisms:
- MMU/page protection and guard pages at the stack boundary: early detection of going beyond the limits.
- Separation of execution and data: prohibiting the execution of code from the stack (NX/DEP), so that an overflow does not turn into the execution of arbitrary code.
- Address space layout randomization (ASLR): complicates the predictability of the stack location, reducing the risk of targeted attacks.
- A hardware-level shadow stack for protecting returns (shadow stack), preventing the substitution of return addresses.
- Control-flow integrity at the processor level, reducing the probability of exploiting overflows.
- OS policies for stack limits per thread and process, providing predictable behavior and abnormal termination before neighboring memory regions are damaged.

</spoiler>

<anchor>body</anchor>

All the stack protection methods listed above make it possible only to detect the very fact of its overflow, when the program's data is already destroyed, but for the purposes of [provable programming](/en/blog/safety/language-guarantees/) I am interested in the task of preventing stack overflow and the possibility of implementing software handling of this situation from inside the running application without violating data integrity.


## Implementation of stack overflow protection for C++

The source code of the project for preventing the abnormal termination of the application due to stack overflow can be viewed on [GitHub/stack-check](https://github.com/afteri-ru/stack-check).

Its main idea consists in checking the free space on the stack before calling a protected function, and if the free space is insufficient, then a program exception `stack_overflow` is thrown, which can be caught and handled from inside the application, without waiting for a segmentation error to occur due to the overflow of the program/thread stack and the subsequent destruction of data.

The code of the project is implemented as a single header file `stack_check.h`, which contains all the necessary software primitives for manual use. There is also a plugin for Clang, which at the stage of generating IR code automatically inserts calls of the stack overflow control functions before the protected functions.

For manual marking of functions and methods of classes, before calling which a check of the free space on the stack is required, custom C++ attributes are used, which are expanded with the help of the macros `STACK_CHECK_SIZE(size)` and `STACK_CHECK_LIMIT`:

- The attribute `STACK_CHECK_SIZE(size)` accepts one argument in the form of an integer — the size of the free space on the stack, which will be automatically checked before calling the protected function.
    
- The attribute `STACK_CHECK_LIMIT` also checks the size of the free space on the stack, which is set **at the compilation of the application**. _The stack usage size for each function can be found by specifying the `-fstack-usage` option during compilation, which saves to a `*.su` file the list of all functions and the stack size required for them_.
    
The automatic insertion of code before a protected function can be canceled. For this, a call to the auxiliary static method `ignore_next_check(const size_t)` must be inserted in the C++ code, to which the number of the following code insertions is passed, which will be skipped (removed) from the generated (executable) file.

An example of the code for using the library:

```cpp
#include "stack_check.h"

using namespace trust;
const thread_local stack_check stack_check::info;

// A function without automatic stack overflow checking
int func() {
    ...
}
 

// Before each call of the function, code checking the specified free space on the stack will be inserted
[[stack_check_size(100)]]
int guard_size_func() {
char data[92];
    ...
}

// Before each call of the function, the minimum size of free space on the stack will be checked
STACK_CHECK_LIMIT
int guard_limit_func() {
    ...
}

int main() {

    // Here code for stack overflow control will be automatically added
    guard_size_func();
    

    stack_check::ignore_next_check(1); // The next automatic insertion of the stack check will be ignored
    guard_size_func();
    

    // Here code for checking the minimum size of free space on the stack will be automatically added
    guard_limit_func();
    
    stack_check::check_overflow(10000); // Manual check of the free space on the stack
    func();
    
    return 0;
}
```

After that the file is compiled with the connection of the clang plugin:

```bash
$ clang++ -std=c++20 -Xclang -load -Xclang stack_check_clang.so -Xclang -add-plugin -Xclang stack_check -lpthread filename.cpp
```
## Implementation details


The final stack size for a function call depends on many factors, such as the target platform, the degree of optimization of the program, the calling convention of a specific function, etc., which is why it cannot be computed with the help of a static code analyzer based on the AST or by analyzing the IR representation, but can only be determined at the stage of generating machine instructions for a specific target platform.

Moreover, for the purposes of automatic control (for functions marked with the `stack_check_size` or `stack_check_limit` attribute) the minimum size of free space on the stack cannot be less than a certain fixed threshold, which is required for creating a program exception with error information. The size of such a threshold depends on the implementation, and it is affected by the target platform, the operating system, the degree of optimization of the program and other factors.

The main functionality of stack overflow control is located in the class `trust::stack_check`. Information about the stack size is stored in static fields of the class, individually for each thread (_Thread Local_ — thread-local storage, TLS), which makes it possible to request the stack parameters once for each thread during the initialization of the structure, and, when checking the size of the free space on the stack using the method `stack_check::check_overflow(N)`, to compare the current stack pointer with the lower boundary of the memory area allocated for the stack.

To use the library in an application, one must define the static variable `const thread_local trust::stack_check trust::stack_check::info`, and to specify the minimum limit of free space on the stack, assign the corresponding value to the macro `STACK_SIZE_LIMIT`.


## Overhead and impact on the speed of the program

The stack overflow check, even in the case of maximum optimization, cannot be shorter than two machine instructions (a comparison operation and a short jump instruction), which, of course, adds time to the function call.

_As a "speed meter" I used a program for finding prime numbers by the recursive method from the file `prime_check.cpp` (as tests I also tried the towers of Hanoi and a recursive algorithm for counting the sum of digits of a long number, but the stack depth for the overflow check in the first case requires a very long duration of the algorithm's operation, and the record of a long number at which a stack overflow occurs takes several screens, which is also inconvenient for testing purposes)._

```shell
$ ./prime-check-O3
Usage: ./prime-check-O3 <start_number> [count]

$ ./prime-check-O3 9999999999999999
Stack overflow exception at: 104588 call depth.
Stack top: 0x7fffdc634000 bottom: 0x7fffdbe36000 (stack size: 8380416)
Query size: 10000 end frame: 0x7fffdbe386a0 (free space: 9888)

$ ./prime-check-O3 10000000000 1000
10000000019
10000000033
10000000061
...
10000022899
10000022909
Max recursion depth SAFE: 100000
Number of recursive calls SAFE: 117539009
Execution time SAFE: 18227634 microseconds

Max recursion depth: 100000
Number of recursive calls: 117539009
Execution time: 17925080 microseconds
Difference in execution time: 1.68788 %
```

An assessment of the impact of stack overflow control on the speed of the application: without optimization (-O0) — the execution time increases by about _1%_-_5%_, and with maximum optimization (-O3) — by about _0.5-2%_ (the total execution time of the application is about _20 seconds_).

In an ideal form (if we strive for minimal overhead) it is best to compute the stack size for all functions of the program and always use the maximum value (after all, loading a value into a register before the comparison operation also requires processor cycles and memory access). In this case, for any sequential calls of functions in one block it will be enough to check the free space on the stack only before calling the first function.

## What does AI have to do with it?

I honestly admit that, even having some experience of developing plugins for clang behind me, without using an LLM I would not have managed such a project in such a short time (about a month of background work).

As it turned out, the functionality of clang AST plugins does not make it possible to connect plugins for LLVM directly without some "shamanism", and making changes at the stage of generating object code with the help of a plugin turned out to be generally impossible without modifying the LLVM sources. Because of this I had to abandon the automatic computation of the stack size, since patching LLVM only to verify the concept seemed excessive to me.

Nevertheless, despite the very large help of generative neural networks in studying the topic, writing real code with their help turned out to be a useless undertaking. The best algorithm for using LLMs in development turned out to be the following: to configure the parameters of the LLM for working with code and to create with its help separate small projects with limited functionality, and necessarily with working tests, in order to be sure of the correct operation of the written example. After that, to thoughtfully study the resulting code, maybe even chat in dialog mode to clarify some points, and after the picture in the head comes together — one can transfer the necessary fragments to the main project.

All attempts to organize the work differently on the full code base ended in fiasco. Any LLMs began to lose context and switched to secondary, and often non-existent, problems. After which the project completely broke down, since the LLM decided to check the build system, for which it made a "stub", i.e. deleted the existing working code, made sure that the build went fine, and everything started all over again.

With prolonged use of an LLM, the emotional encouragement of users and attempts to ask clarifying questions became especially annoying: "You asked a good question", "You formulated the problem correctly", "Thanks for the remarks. Please clarify ...", as well as the embedded advertising of their own solutions:

![](/en/blog/safety/stack-overflow.png)

But even despite some disappointment from the constant errors when using LLMs and the attempts to impose a subscription to their own software products, the use of generative neural networks made it possible to save a bunch of effort and time on studying code examples.

I have now generally stopped writing scripts by hand, editing configuration files for VSCode or dealing with errors in CMakeLists.txt. Of course I do not think that LLMs will soon really be able to replace a programmer, but they can already really make the work of developers easier.


## Integration status in TrustLang

The functionality of stack overflow control is built into the [TrustLang](/en/docs/) compiler as
control **at the code generation stage** (the role of the clang plugin is taken by the transpiler, and the runtime
primitives `trust::stack_check` — the public runtime header `trust/stack_check.hpp`).

- **Function attributes** (insertion of the check BEFORE each call of a marked function):
  - `@[stack_check@]` — without an argument = **limit**: before each call of `check_stack_limit()`
    (free ≥ `m_stack_limit` from the `.stack_sizes` section + reserve);
  - `@[stack_check(N)@]` — an integer `N`: `check_overflow(N)` (explicit size of free space).
- **Individual functions** (real C++ functions of the runtime, explicit calls in the code, `%` prefix):
  - `%trust_stack_check(N)` — `check_overflow(N)` (free ≥ N + reserve);
  - `%trust_stack_check()` — `check_stack_limit()` (free ≥ m_stack_limit + reserve);
  - `%trust_stack_check_set_reserve(N)` — `set_reserve(N)` (min. reserve for all functions);
  - `%trust_stack_check_get_reserve()` — `get_reserve()`;
  - `%trust_stack_check_get_limit()` — `get_limit()` (the current `m_stack_limit`);
  - `%trust_stack_check_set_limit(f1, f2, ...)` — `set_limit({&c_f1, &c_f2})` (restrict `m_stack_limit`
    to the listed functions; empty = all functions of `.stack_sizes`).
- The **reserve `reserve`** (min. stack size for creating an exception, default 8192) is ALWAYS added
  to the checks: `stack_check` = `N + reserve`, `stack_check_limit` = `m_stack_limit + reserve`.
- **Control**:
  - `--stack-check=<off|explicit|recursion|auto>` (default `explicit`), `-Wstack-check=<mode>`,
    `-Wno-stack-check`; from code — `@__OPTION__("stack-check", "...")` + `@__OPTION_PUSH__/POP__`;
  - the minimum reserve — `--stack-check-reserve=<bytes>` (default 8192) / `-Wstack-check-reserve=` /
    `@__OPTION__("stack-check-reserve", "...")`;
  - `--stack-check-functions=<names>` — restrict `m_stack_limit` to the listed functions (empty = all);
  - `recursion` — the diagnostic `-Wstack-check-infer` for unprotected recursive functions;
    `auto` — auto-marking of recursive functions.
- The generated program is compiled with `-fstack-size-section` and `-lpthread`; the TLS `info`
  is defined in the generated entry `_main.cppt`.

Example:
```trust
@[stack_check@]
@func recurse(n:Int32):Int32 {
    @if(n <= 0) { @return 0; };
    @return recurse(n - 1) + 1;
};
```

