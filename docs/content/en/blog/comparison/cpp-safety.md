---
title: "C++ safety and Buridan's mice in IT"
slug: cpp-safety
date: 2026-04-02
tags: [c++, memory-safety, security, programming-languages, economy, rust, comparison]
---


In IT there is a strange form of "informational inertia": when a phrase once landed well on reality, became a quote, turned into a marker of "one of us" — and then continued to live on its own. Reality moved on, practices changed, tools became different, the economics of development flipped over, but the cliché of the formulation remained. And it is repeated especially willingly by those who need to write quickly and convincingly, but have no time (or competence) to sort out the technical details.

And the problem is not that the old theses were originally meaningless. The problem is that they continue to be presented as eternal laws of nature — without a date, without context and without allowances for the fact that the industry has fundamentally changed.

For example, the phrases about the "silver bullet" and "unsafe C++", which sound in almost every popular text about development and safety, and both are often turned around so that they become an outright untruth.

<a id="silver-bullet"></a>

### "There is no silver bullet": the meme remained, but the meaning was distorted

Brooks's thesis was not a statement that "manifold improvements do not happen". It was directed against the expectation of a *universal miracle* that would give an order-of-magnitude productivity gain "everywhere and at once" — simply through the use of a new technology. It is precisely this clarification that usually disappears in retellings, and only the slogan remains: "there is no silver bullet". A very short and emotionally convenient phrase, which is then applied as a general brake: do not look for opportunities, do not count on technological leaps, "magic does not happen" anyway.

However, the "silver bullet" has long existed, is used, and looks prosaic: *to take ready-made components for free (or almost free)* — libraries, frameworks, infrastructure services, standardized protocols and implementations — and thereby not "speed up writing code", but repeatedly reduce the total amount of project work, and not perform again the work that the market has already done and packaged into a reusable form. This really saves labor **manifold** — sometimes by an order and even several orders of magnitude, which the author of the original expression did not even dream of.

This is the real "silver bullet", about which Brooks, in later comments and reprints, wrote much more conciliatorily: there is no universal miracle, but componentization, code reuse and mature platforms are capable of giving enormous gains where previously people really rewrote the same thing many times over, spending time and resources on it.

### "C++ is the most unsafe language": substituting the subject of the conversation

The statement "C++ is unsafe" is often pronounced as an assessment of the language _in general_, without specifying the threat model, the nature of the code and the development process. In this form it is almost useless: safety is a property of a *system and the practices used*, and not a label on the name of a language.

The technically correct starting point is different: *C++ is not memory-safe by default* and allows undefined behavior. Consequently, in the absence of instrumental code control, the probability of critical defects is higher than in languages where a significant part of memory management errors is excluded constructively or semantically.

But it does not follow from this that C++ is "the most unsafe". Firstly, the comparison "the most" makes no sense without definitions: which classes of vulnerabilities are considered, what restrictions on the environment, what type of project (embedded systems, high-performance backend, UI, cryptography, drivers), what level of trust in the input data, what requirements for fault tolerance. Secondly, C++ provides a sufficient set of mechanisms to noticeably reduce typical risks — provided that the team writes the code and organizes its own work.

Here a comparison with "Buridan's mice" comes to mind, when a paralysis of choice arises: at the same time one wants to have both "freedom" and "minimal changes" and "zero cost" and "development speed" and "the absence of vulnerabilities". But these are often mutually exclusive requirements.

And when, as a result, neither a strict C++ development regime (with restrictions and checks) nor migration to where safety is provided constructively (Ada, Rust) is chosen, then the third remains: to continue ~~eating the cactus~~ writing code in the usual way, regularly getting memory management errors or UB, and then declaring safety problems a property of the tool and continuing to use unsafe practices and complain about the language as the root cause of all problems.

Therefore the problem with "unsafe C++" is not that it _does not allow_ writing code safely, but that the team _does not want_ to pay for safety with what it is paid for in C++: discipline, restrictions and automated checks..

### When a phrase has expired or lacks context

Established opinions in IT often turn into "eternal truths" only because they are convenient to repeat. But the technical world is not obliged to adjust to quotes from thirty years ago.

"Silver bullets" in the form of LLMs, the reuse of ecosystems and services exist, and they really reduce complexity and give manifold gains in the amount of work compared to the development of code of many years ago.

Likewise, the presence of errors in C++ code does not turn it into the "most unsafe language". Yes, C++ does not guarantee safety automatically. It requires discipline and instrumental control, and C++ can be sufficiently safe for many systems tasks, especially where control, performance, integration and predictability are important.

Of course, it is easier to say about C++ "the language is bad" than to admit: the problem is often that "Buridan's mice" choose neither safety as a goal, nor process as a means — they choose the cactus of habit and then turn their pain into a worldview.

