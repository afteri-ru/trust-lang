---
title: "The Achilles' heel of C++ and the future r̶e̶ evolution"
slug: cpp-evolution
date: 2026-04-10
tags: [c++, comparison, programming-languages, compiler, memory-safety]
---


Recently I published an opinion about a fundamental economic model of software development that does not contribute (and objectively should not contribute) to a mass transition from C/C++ to "safe" programming languages [The economics of code safety, or why Rust is not needed](/en/blog/comparison/code-safety-economy/).

But to remain honest with the readers, I decided to also publish a counter-argument article describing the other side of the coin, that is, why C++ will still be replaced sooner or later, and at the same time to try to analyze what the new programming language that will inevitably replace C++ will look like.

## The problems of C++ and the dead end of evolution

The impossibility of safe memory management and undefined behavior (UB) have become part of the C++ standard. They are built into its foundation. Macros, header files, partial specialization, SFINAE, initialization rules — any attempt to change C++ often runs into the fact that there is already a lot of legal code that depends on the current rules. And one must either preserve the old behavior, because abandoning UB or moving to a strict verifiable memory management model immediately breaks many assumptions in existing code, or make a new concept in parallel with the old one, which [further increases the complexity of the language](/en/blog/programming/complex-prog/).

Therefore, behind the success of C++ there is a paradox: the more established the language standard becomes, the harder it is to make changes to it. Organizationally, this means the need to synchronize a large number of participants in the process: compiler and library developers, company representatives and independent experts, each of whom represents different interests. Some fight for memory safety, some for zero overhead, and all of this happens taking into account obligations to an employer and personal ambitions.

As a result, C++ has trapped itself. Its problems have been known for decades, proposals are discussed for years, and it is possible to quickly accept into the standard only additions that conflict with almost nothing and, accordingly, affect nothing :-(. And for a language that claims to be the main tool of systems programming, this is no longer a shortcoming. This is *the Achilles' heel*.

<a id="cpp-killers"></a>

## The paradox of modern "C++ killers"

Therefore it is not surprising that languages-"C++ killers" regularly appear. Almost every new systems language at some point positioned itself as a candidate to replace C++: safer memory, better concurrency, clearer errors, a more modern development model. Many of these languages are indeed successful in their niches and often surpass C++ in ergonomics and safety. But as a replacement for C++, in the sense of mass displacement of it from existing large code bases, they fail. And the reason here is often not the syntax and not even the quality of the compiler.

The main barrier is compatibility. C++ is not only a language. It is libraries, ABI, calling conventions, binary interfaces, build tools, linkers, debuggers, profilers, sanitizers, certifications, ancient make/CMake/Bazel scripts, tons of headers and macros, millions of lines of "embedded coding culture". When a new language comes with its own compiler (even if it uses LLVM as a backend), it brings with it a new set of boundaries: how to mix modules, how to link with existing libraries, how to debug the call stack, how to guarantee compatibility of exceptions, RTTI, name mangling and calling conventions on different platforms.

In theory it is easy to say "we have FFI". In practice, FFI is `extern "C"`, which is insufficient for interacting with C++ with its templates, inlines, overloads, SFINAE, ADL and the culture of header-only libraries. A large part of the modern C++ ecosystem is not a convenient C API, but a set of headers that assume the consumer is also compiled as C++ and lives by the same rules.

For some reason, all the C++-killer languages forgot a very useful historical lesson. The early C++ compiler (then still "C with classes") — **cfront** — was implemented as a transpiler that turned a C++ program into ordinary C source code. This meant that any existing C compiler instantly became a way to "compile C++". Any existing linker, debugger and profiler remained in place. The risk of using a new language decreased many times over, because one could start using new capabilities without retraining developers and without rebuilding the entire production process.

However, all modern "C++ replacements" often choose the opposite path: they create their own compiler and their own execution platform (even if based on LLVM). Technically this is an understandable choice: it is easier to provide an integral system and better diagnostics. But the price of such a choice is the creation of a compatibility barrier. Where Cfront removed the problem of integration with old source code, the new compiler creates it anew: one has to solve the question of binary boundaries, debugging, mixing with legacy libraries, building, deployment and update policy. As a result, a new language may be aesthetically beautiful, but it becomes a possible alternative only for new projects, and not at all a "replacement for C++" in the sense of migrating gigantic code bases.

## A new language to replace C++

Therefore the most realistic scenario for the emergence of a true replacement for C++ looks different. It will grow out of a transpiler that generates C or, more likely, C++ as the target language. A transpiler gives the main thing that is needed at the scale of C++: incrementality. One can start with one component, one file, one subsystem, without rewriting everything. The build remains the same, the linking remains the same, the ABI remains the same, the tools remain the same.

A "new language" becomes a frontend that gradually conquers territory inside the existing world, rather than requiring a new world to be built next to it. At the same time, one can introduce "by default" safety, leaving explicitly marked "unsafe" sections where interaction with low-level reality is needed. And most importantly: one can be compatible not only with C as the least common denominator, but with the C++ ecosystem as a whole.

Of course, transpiling is not a panacea, and such an approach creates its own difficulties: the quality of the generated code, readability, correspondence of debug information, the accuracy of displaying the original abstractions and the need to use C++ compilers. But these difficulties may be more acceptable than a full-scale change of the compiler and the entire infrastructure. Therefore a transpiler is a way to reduce the risks of transitioning to a new language to a controllable magnitude and to make migration to a new language economically feasible.

From this one can conclude that, most likely, the winner will not necessarily be the "purest" or "most correct" language, but the one that has the lowest barrier to use in the already existing ecosystem. C++ once won in exactly this way — not by the quality of the language, but by the simplicity of integration with already existing solutions. Therefore, if a replacement for C++ ever does appear that truly becomes mass-scale, it will most likely start with a transpiler: as a layer on top of C++, which is first and foremost **compatible**, and only then *more convenient* and *safer*.

