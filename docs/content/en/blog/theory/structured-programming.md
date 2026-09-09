---
title: "Reflections on structured programming"
slug: structured-programming
date: 2023-12-31
tags: [theory, control-flow, programming]
---

31 Dec 2023 at 11:53

![](/en/blog/theory/structured-programming.jpeg)

Initially I wanted to title the article in some provocative way, for example, "How science can turn into religion", "Trapped in the distortions of the meaning of structured programming" or "What you were not told about structured programming", but in the end I still kept the current title and I hope it does not irritate the readers. And although the other titles are more clickbaity, nevertheless they still reflect the meaning of the article to a greater extent than the neutral "reflections".

The occasion for this article was one of the comments to the previous publication [Memory management and shared resources without errors](/en/docs/safety/memory/), where someone wrote "it was proven mathematically". And I immediately recalled my little investigation, when I tried to understand one of the "mathematical proofs" that we are all told about back in school in computer science lessons.

Everyone probably remembers that any algorithm can be represented in the form of three kinds of algorithmic constructs: sequence, selection and repetition? And sometimes they also add that this theorem was put forward and **proved** by E. Dijkstra in the 70s of the last century, including the widely publicized alleged ban on the use of the goto statement.

<cut />

### Structured programming
Structured programming is a concept that appeared in the late 1960s and influenced the further development of imperative programming. Edsger Dijkstra is considered the founder of structured programming. His merit consists not only in the development of this concept, but also in its promotion.

The main principles of structured programming were formulated by Edsger Dijkstra mainly in [Dahl, O.-J., Dijkstra, E.W., Hoare, C.A.R. Structured Programming](http://pascal.hansotten.com/uploads/dijkstra/Structured%20Programming.pdf).

However, while developing the lexicon for my programming language, I repeatedly had some doubts about the reliability of these facts. After all, exception handling and some other elements that are practically mandatory for all modern programming languages do not fit into this concept (that any algorithm can be represented in the form of three kinds of algorithmic constructs) at all.

### In search of the truth
Therefore I decided to understand Dijkstra's proof, and the results I found surprised me very much. It all turned out like the story of a scientist who raped a journalist, or, in the more tolerant version of the anecdote: "Not Katz, but Rabinovich. Not in the lottery, but at cards. Not a million, but a hundred rubles. And he did not win, but lost".

In other words, everything turned out to be completely different. The context necessary for a correct understanding of this information is absent, the structured theorem was published and proved not by E. Dijkstra at all, and much else that radically changes the understanding of some issues related to structured programming. And since this is in fact a mandatory basis that is used at school when teaching the basics of programming, I think it is necessary to ~~tear off the veils~~ tell about the facts I found.

I started by trying to find the primary source of the publication in which E. Dijkstra put forward and proved the theorem on the sufficiency of three algorithmic constructs. And I did not find it. But in the process of searching I came across a detailed article with an analysis of the information I was interested in and references to primary sources: Avacheva T. G., Prutskov A. V. A modern view of the concept of structured programming.

### Conclusions
The final results of my search:
1. The structured theorem that any flowchart can be transformed into a flowchart consisting of blocks of three kinds: sequence, selection and repetition, was proved in 1966 by the Italian scientists C. Böhm and G. Jacopini (the article was published in Italian in 1965) [Böhm–Jacopini theorem](https://en.wikipedia.org/wiki/Structured_program_theorem). This theorem is **purely theoretical**, proved **subject to some restrictions**, and was mentioned by E. Dijkstra in his work "Go To Statement Considered Harmful".
2. The principles of structured programming proposed by E. Dijkstra are intended to improve the style of program code through the use of control structures and the abandonment of other instructions controlling the course of the algorithm, i.e. this is not a proven theorem, only a **proposal** to abandon the use, and not only of the **goto statement**, but also of the **value assignment statement**: *"The goto statement allows us, by jumping back, to repeat part of the program, whereas the assignment statement can create the necessary difference in state between successive repetitions."*
3. E. Dijkstra in his work formulated the principles of structured programming in an attempt to **automate the proofs of program correctness** (*a program is a sequence of computations. Computations can be written with mathematical formulas. Consequently, one can prove a theorem about the correctness of the program*), but still the final conclusion is *"Testing shows the presence, not the absence of bugs"*.
4. Besides the three kinds of algorithmic constructs (sequence, selection and repetition), a mechanism for handling errors (exceptions) is also required, and sometimes the handling of interrupts and the execution of parallel actions, without which a full-fledged implementation of certain algorithms will be impossible.

### P.S.

Happy New Year!
