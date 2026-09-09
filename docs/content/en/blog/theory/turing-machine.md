---
title: "Reflections on the Turing machine and the causes of errors in programming languages"
slug: turing-machine
date: 2025-11-15
tags: [theory, errors, programming-languages, compiler]
---

A couple of years ago I wrote the article [Reflections on structured programming](/en/blog/theory/structured-programming/), in which I tried to sort out the misconception that Edsger Dijkstra proved that any algorithm can be built from just three constructs (sequence, selection, loop). Whereas in reality:
- This theorem was proved (with conditions and restrictions) by the Italian scientists **Böhm and Jacopini**, and Dijkstra merely referred to it.
- The three mentioned basic algorithmic constructs are **insufficient** for modern programming (for example, exception handling is required).
- The famous criticism of the `goto` statement by Dijkstra (as well as of the assignment statement, which is always forgotten to mention) was not a strict ban, but merely a **recommendation for improving code style**, so that programs would be easier to analyze.

And now the time has come to write about some problems of the Turing machine — the fundamental basis of all information technology.

### Why?

The occasion for the current article was my dialogue with a user on Reddit while discussing the article [Language guarantees as a basis for secure software development](/en/blog/safety/language-guarantees/) (there I also get feedback from readers, like on Habr, only from the English-speaking audience).

They tried to prove their position to me, arguing it with theoretical calculations based on reasoning about a hypothetical Turing machine, without understanding some of its features and limitations. As a result, it became obvious to me that, in order to move on, this point also had to be clarified.

<a id="MT"></a>

### What is wrong with the Turing machine?

The Turing machine is the foundation of computer science and the theory of computation, a purely abstract executor (a mathematical model of computation) capable of imitating all executors (by specifying transition rules) that in some way implement the process of step-by-step computation.

_Whatever reasonable understanding of an algorithm there may be, any algorithm corresponding to such an understanding can be implemented on a Turing machine._

However, like any *mathematical abstraction*, it has some limitations and assumptions, for example the presence of an infinite tape for storing data and the ignoring of the issues of accounting for the resources used (the amount of memory and the program execution time), and as a consequence, the possibility of infinite recursion, whereas in the real world any storage medium is finite, and the execution time of the algorithm also matters, in connection with which the Turing machine determines _theoretical_, and not _practical_ computability.

Because of this, the Turing machine (and consequently any computer we can imagine) does not allow doing some things:
- Solve uncomputable problems — those for which there **exists no algorithm** in principle. It does not matter how much time or memory we give the machine, it will never be able to guarantee a correct answer for all possible inputs.
- It is impossible to create a program that will analyze any other program and its input and say whether that program will ever finish (halt) or run forever (loop). By the way, this is most likely what E. Dijkstra's words confirm: _"Testing shows the presence, not the absence of bugs"_.
- Solve NP-hard problems in a reasonable time.
- The thought experiment "Chinese room" shows that the Turing machine does not allow "creating understanding" or "consciousness" in the human sense; it only simulates its external manifestations.
- Adequately model certain processes. The Turing machine is always the processing of input data followed by halting and producing a result. It is not designed to model constantly running systems that interact with the outside world and use interrupts.

Of course, all this does not diminish the significance of the Turing machine, and its simplicity is its strength, which makes it possible to rigorously prove fundamental theorems about the possibilities and limits of computation.

If you are interested in this topic, I recommend reading [Surprisingly Turing-Complete · Gwern.net](https://gwern.net/turing-complete) or its translation Surprisingly Turing-complete everywhere.

### What does the Turing machine have to do with it?

In the context of the previous article about [Language guarantees as a basis for secure software development](/en/blog/safety/language-guarantees/), the properties of Turing-complete programming languages allow us to draw the following conclusions:
- Any programming language that provides guarantees of secure development is **obliged** to limit the possibilities of implementing some algorithms (at least those that contain errors), and this means that any **safe** programming language cannot be Turing-complete by definition (since one cannot write a program with an error in it).
- The reverse conclusion is also interesting: the Turing completeness of a programming language is a sufficient reason for such a programming language to be unable to be classified as safe :-)
