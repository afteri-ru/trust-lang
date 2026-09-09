---
title: "The economics of code safety, or why Rust is not needed"
slug: code-safety-economy
date: 2026-04-04
tags: [rust, c++, memory-safety, economy, security, programming-languages, comparison]
---


I read the article Parallelism with shared state in Rust and noticed that its general meaning can be expressed by the well-known phrase: "do what must be done, and what must not be done — do not do". In other words, it is exactly the same advice that can be given to a developer of any other programming language, for example C++.

And I decided not to continue the discussion in the comments, but to express my opinion as a short article describing a fundamental economic model of software development that does not contribute (and objectively should not contribute) to a mass transition from C/C++ to "safe" alternatives. Because, due to the peculiarities of cost distribution, the software developer has no economic motivation for the complete elimination of errors, and as a consequence — for the transition to the use of "safe" programming languages.

---

The goal of any commercial company is profit maximization. Software code quality has never been an end in itself, but has always been merely one of the many properties of a product. A business optimizes not the number of errors, but the total cost of ownership relative to the revenue the product brings. Moreover, according to typical software licenses, the developer bears no responsibility for any losses of the user.

Moreover, the presence of a certain stream of errors in a product — non-critical or hard to reproduce, but requiring attention — helps create and maintain a sustainable support ecosystem. This ties the client to the developer and creates an additional reason for post-sale contact. And this, alas, is not a conspiracy theory :-(

There is another very important point: the cost of fixing errors in the overwhelming majority of projects constitutes only a small part of the team's labor costs. Furthermore, the labor intensity of fixing the errors that are theoretically eliminated by switching to memory-safe languages amounts to single-digit percentages of the total development budget.

And if we consider that nothing comes for free, and that with the transition to a safe language the developer will be forced to spend significantly more time "fighting the compiler" — for example, to describe graph data structures, complex mutual dependencies or even to partially redo the architecture — then when a team switches to a new "safe" language, the expected payback period of such a transition through savings on errors will go far beyond the planning horizon of most commercial projects.

But something else is even more important. Defect fixing is performed in parallel with the development of new functionality and is largely absorbed by the general process. In a real project, a typical situation is when a developer works on new functionality and along the way fixes a defect within the same task, since the code is being modified anyway. A product that is actively evolving naturally gets rid of old errors — not because someone purposefully looks for them, but because rewriting improves its quality. And this is true for any programming language.

---

Very often, in discussions about "safe" programming languages, engineering analysis is replaced by marketing slogans. But the costs of software errors always fall on the end users, not on the developers, therefore the latter have no market incentive for their complete elimination.

The goal of any business is profit, therefore the choice of a tool is determined by the total cost of development, and not by the theoretical "safety" of the code. An engineering decision is always made in the context of various constraints — time, financial, personnel — and ignoring them for the sake of ideological purity is not engineering, but religious dogma.
