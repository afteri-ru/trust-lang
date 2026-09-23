---
title: "The main problem of using AI (Illusions of Intelligence) in software development"
slug: ai-illusions
date: 2025-12-11
tags: [ai, development]
---


I am often struck by how technically literate people argue about whether LLMs have intelligence or whether it is just mathematical computations by a certain algorithm without the rudiments of reason. And what is most interesting, sometimes the opponents in the dispute **for** the presence of intelligence in generative neural networks are people who in this way advertise their IT solution, not understanding that by doing so they only create problems for themselves.

After all, by creating a reasoning illusion of intelligence, developers are in fact putting an end to the possibility of any real application of such solutions in serious tasks, since the imitation of reason turns a potentially useful working tool into a game with a tambourine without any guarantees of the quality and reproducibility of the results obtained.

### False expectations from the introduction of AI

The overwhelming majority of articles about breakthroughs in the field of AI pursue one simple goal — to sell their solution, and there are at least two target audiences:

The first is people with insufficient qualifications, who are told that with the help of AI they will be able to perform the work of higher-class specialists, which creates a false sense of expanding their own capabilities and allegedly makes it possible to write programs on their own without any programming skills (this is the very vibe-coding), which shatters against the very first real and non-trivial task.

And the second, more serious audience, is the management of companies, to whom the idea of cost optimization is sold: why pay a team of expensive employees if they can be replaced by one specialist with low qualifications and a subscription to an AI service? The calculation is simple: to cut the payroll fund by replacing ordinary but expensive human labor with cheap but unpredictable AI-agent-based labor.

In the field of software development, this trend manifested itself especially vividly. Some companies succumbed to the general euphoria and began massively cutting experienced teams and replacing them with cheaper solutions involving AI. But the assessment of the first results turned out to be sobering. The development process turned into unpredictable and uncontrolled shamanism. As a result, the opposite trend has emerged in the market: companies that, having already encountered the chaos after the introduction of AI, are beginning (without noise and pathos) to bring back previously laid-off employees.

### My unfulfilled hopes

The occasion for writing this article was yet another psychological war (I cannot find a more suitable definition) with an LLM in an attempt to solve a relatively simple task, namely to write a project build system on CMake instead of a custom Makefile. The project itself includes a couple of C++ files that are compiled with clang. One produces a compiler plugin (an ordinary so-file), and the second produces an executable file with unit tests for the Google Test Framework.

Considering that initially the build runs correctly and all tests pass, I hoped that such a task would be within the reach of any AI assistant. Unfortunately, the expectations completely diverged from reality, because not only did the build of the project with CMake still not work, but the Imitation of Intelligence also arbitrarily created a new file, even though I am already experienced and the prompt contained an explicit prohibition on such actions (not to create new files except those explicitly listed). In addition, the project license was spontaneously changed from LGPL to MIT, and the documentation file was completely changed, where a detailed description of the project was replaced by a couple dozen lines describing the build process, which, by the way, still did not work!

This was the last time I tried to use AI to refactor existing code, since I cannot imagine how one can in principle work with tools if they can arbitrarily produce either complete nonsense or normal code without any possibility of control on the part of the user and without the possibility of repeating previous successful attempts.

<a id="divination"></a>

### Software development is not shamanism!

Software development has long been engineering with quality control and reproducibility of results. And one of the main processes in software development is code debugging, which often consists in repeatedly repeating the same scenario in order to find the cause of the incorrect behavior of the program.

Modern large language models (LLMs) do not "understand" the task in the engineering sense. They are probabilistic systems that do not compute the single correct answer, but on the basis of the input data and the request (prompt) generate the most probable sequence of words (tokens) based on a gigantic array of data on which they were trained.

And now let us imagine a scenario: a developer uses AI to generate a code fragment. He writes a prompt, on the basis of which he gets working code and integrates it. A week later he needs to make a small change. He writes a new prompt to modify the code and everything stops working. He tries to fix the original prompt and... also nothing works. What is the reason: is it only the change of the request? Or did the model simply generate a different variant because of the changed "phase of the moon" (a new `SEED`, a changed system prompt at the provider or fine-tuning of the model)?


The same prompt sent to the same model can produce different data, and there can be no question of any reproducibility of results here, since many additional factors influence this:
*   **There are many providers and their models** Models from OpenAI, Google, Anthropic or GigaChat will give different code for the same request, because their architectures and training data differ.
*   **Model updates** — the provider can update the model without notifying the user. The version that yesterday generated ideal code may today, after an update, produce a completely different result.
*   **Hidden settings** — the system prompt (the internal instructions that the model receives before processing your request), the censorship and security settings are constantly changed by the provider, and this directly affects the generation of the final result.
*   **Temperature** — a parameter that controls the degree of "creativity" and randomness of the answer, and even a small change to it radically changes the result.
*   **SEED** — the initial value for the pseudo-random number generator, and if it is not fixed, then each launch of the model with the same data will also be unique.

As a result, working with AI turns into real shamanism. Did you get a good result? Great! But you cannot guarantee that you will get it again. The lack of repeatability of the result makes software development impossible because of the unpredictability of even the slightest refinements of existing code and the impossibility of debugging prompts!

<a id="reproducibility"></a>

### Reproducibility of results as a mandatory requirement for AI

Before starting to use any AI models as a serious tool in software development, it is necessary to solve the problem of reproducibility (repeatability) of results at least within one version of the model.

The user must have some mechanism that will guarantee that the same request can produce the same answer (whether it is correct or not), otherwise, without the possibility of re-reproducing requests, AI will forever remain a toy, and not a working tool of engineers.

The simplest and most obvious variant of implementing such a mechanism is, at the beginning of a session or when generating an answer, to give out along with it a special token that includes (or in some way identifies) all the internal settings of the provider's session.

It may include a hash of the system prompt, the security and censorship settings, the initial `SEED` for the random number generator, etc. Then, on a repeated call to the API, the user will be able to pass this token along with the original request, and the provider will use the very same internal settings so that the user gets the very same result.

Such functionality will require reworking the systems already in use. Moreover, it may be of no interest to the mass user who decided to just play around or who does not need reproducibility of results (for example, when working with ordinary text), but in software development the repeatability of the results of requests for a specific prompt is very much needed (even if one has to pay separately for it).

And since I was persuaded to add the "AI Season in Development" badge to this article, it would be logical on my part to suggest that the GigaChat developers try to implement a mechanism of result reproducibility within their service. This would turn GigaChat from an interesting experimental product into a professional tool that can be integrated into serious software development processes without fear.

