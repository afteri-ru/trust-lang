---
title: "Programming language guarantees as a basis for secure software development"
slug: language-guarantees
date: 2025-11-10
tags: [security, programming-languages, development, verification]
---


Errors in composing programs for computers appeared even earlier than the very first programming languages were invented. Actually, programming languages were invented precisely so that programs would be written more simply, and the number of errors in them would be as small as possible.

To reduce the number of errors, many methods were developed, including the creation of specialized source code analysis tools and even entire programming languages.

But after many decades, the problem of purely technical errors in software still remains unsolved to this day, however the approach proposed in the Rust language changes everything.

### Paradoxes of safe memory management

Lately hardly anyone has failed to mention the Rust programming language, which, through the innovative model of "ownership and borrowing", guarantees memory and thread safety, making it possible to exclude many kinds of errors *at compile time*. However, for some reason they always forget to tell that the very concept of "ownership and borrowing" also has several fundamental limitations.

For example, the implementation of any algorithms with **multiple ownership** requires the mandatory use of `unsafe` blocks or the redesign of the application architecture, which limits the use of Rust in legacy systems, where a full refactoring of the code is economically inexpedient.

In addition, the analysis of cyclic graphs (cross-references) in its classical form has no solution *at compile time* in principle, therefore it always requires the manual use of smart reference counters (`Rc`, `Arc`), which also increases the risk of memory leaks due to implementation errors.

But continuing to use C++ is not an option either! The very name of the C++ language has actually become a synonym when it comes to various errors in software. And this is despite the fact that it has long had a full set of tools for safe memory management: smart pointers (`unique_ptr`, `shared_ptr`, `weak_ptr`), RAII and move semantics.

But the absence of strict rules for their use at the level of the language syntax turns such safety mechanisms into "optional" ones. Developers can consciously or accidentally bypass the protective mechanisms, using raw pointers or uncontrolled memory allocation.

However, any attempts to introduce strict rules into C++ (for example, through the introduction of syntax similar to Rust, like *Safe C++*) meet with the expected resistance, since such changes violate backward compatibility and are rejected both by the standardization committee and by the developers themselves, especially those who work with inherited (legacy) code.

These problems create a paradox: developers are forced to spend time and resources rewriting existing code in Rust and at the same time sacrifice safety for the sake of functionality, since, due to its architectural limitations, Rust cannot guarantee the absence of errors for some use cases, which in fact nullifies all its advantages.

### The current situation in the field of secure development

And these are only the most obvious problems, which concern only safe memory management. Whereas technical errors in software can also include various kinds of overflows: of the bit width of numbers, a shortage of RAM, a stack overflow, which is always allocated in advance and has a fixed size, and so on.

Of course, in C++ the situation is gradually changing for the better, including through the introduction of various application security mechanisms at the level of code generation by the compiler ("hardening"), but the main problem is that for programming languages there is in principle no unified theory (or approach) to assessing secure development. A general theory that would make it possible to assess the possibility of implementing typical algorithms and compare programming languages with each other in terms of software code safety.

Currently, many different tools are used to check for errors in source code, ranging from static analyzers to various kinds of testing. But several decades ago Edsger Dijkstra said in [Dahl, O.-J., Dijkstra, E.W., Hoare, C.A.R. Structured Programming](http://pascal.hansotten.com/uploads/dijkstra/Structured%20Programming.pdf): "Testing shows the presence, not the absence of bugs".

Moreover, each tool or approach checks only its own area, but it is often unclear what remains "outside the brackets". This situation is similar to a multicolored patchwork quilt, where each of its pieces is responsible for its own specific part of safety, but there is no understanding of its overall size.

And if earlier such a situation was the norm, since when creating programming languages their authors tried to implement as many capabilities as possible in order to simplify and speed up the writing of programs, now the trends have changed significantly. The silver bullet that Brooks searched for unsuccessfully (about a tenfold reduction in development cost) was found long ago, is used, and its name is Free Software and Open Source. And with the advent of LLMs, the cost of developing typical solutions has decreased even more.

Therefore, at the present moment it is not the speed of creating software that is becoming more relevant, but the quality of the result obtained. But to manage the quality of the software being created, one needs not only the ability to measure and compare it, but also to understand the capabilities and limitations of the tools used (the programming languages used) in the field of secure development. And from this point of view, the use of Rust, despite its limitations, will still be preferable due to at least some guarantees, than continuing to use C++ with its permissiveness and the ability to make even the stupidest mistakes out of the blue.

<a id="trust-lang"></a>

### Development safety through programming language guarantees

The modern approach to ensuring software safety is largely fragmented, and the main efforts are concentrated on detecting and fixing various classes of vulnerabilities already **after they appear**.

This may be partly justified in cases where the vulnerabilities or attack vectors are not directly related to the source code of the software. But if vulnerabilities arise because of purely technical errors and features of the programming language, then fixing them becomes very expensive when detected in the later stages of the development life cycle.

Responsibility for testing, searching for and implementing measures to minimize errors and vulnerabilities always lies on the shoulders of the developers, who are required to be experts not only in their domain area, but also in cybersecurity issues.

And yet Rust showed a remarkable approach to ensuring secure software development! But not in the part of memory management, but in the change of the very paradigm of ensuring safety at the level of the source code, when **safety is ensured on the basis of programming language guarantees**.

Such an approach — the use of the language and its compiler as the main tool for preventing entire classes of vulnerabilities — shifts the focus from *detecting* vulnerabilities to *preventing* them at the lowest level — at the level of writing program code.

Secure development based on programming language guarantees provides a number of strategic advantages:
*   **Automatic elimination of vulnerabilities:** Instead of searching for individual errors, entire classes of vulnerabilities are eliminated at the system level due to the very fact of using such an approach.
*   **Reducing the cognitive load on developers:** Developers can focus on the business logic, fully trusting the compiler and the type system in matters of basic safety.
*   **Increasing predictability and reliability:** Safety becomes a measurable and provable property of the system, rather than the result of a confluence of circumstances and independently of the use of external tools.
*   **Economic efficiency:** Preventing vulnerabilities at the stage of writing code is orders of magnitude cheaper than detecting and fixing them in the production environment.

### Conclusion

The existing model of "patching holes in a patchwork quilt" in software safety has exhausted itself. To create truly reliable and protected systems, a transition to built-in safety is needed, that is, to the creation of a **theory of secure development based on programming language guarantees**, which is not simply academic research, but an urgent necessity.

This will make it possible to lay safety into the very foundation of software, making it an inalienable property of the source code of the program, and the implementation of secure development guarantees in any programming language — a matter of technique.

An example of implementing safety guarantees when working with memory for C++ can be seen [in this project](https://github.com/rsashka/memsafe?tab=readme-ov-file#%D0%BC%D0%BE%D1%82%D0%B8%D0%B2%D0%B0%D1%86%D0%B8%D1%8F)

