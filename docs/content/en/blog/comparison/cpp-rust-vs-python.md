---
title: "Why C++ or Rust can never replace Python"
slug: cpp-rust-vs-python
date: 2026-07-28
tags: [python, c++, rust, syntax, comparison, programming-languages, ai]
---

Articles analyzing the phenomenon of Python periodically appear on Habr — how one of the slowest programming languages became the king of neural networks, and the answer is always the same: thanks to a simple syntax and a developed ecosystem. But the ecosystem of C++ is significantly larger and richer, so it is not the ecosystem, and only the syntax remains. And if the problem really is the syntax, then there should also be an answer to another question: why did C++ **not** become the basis for research in this (or any other) area?

Earlier I already approached the question of an empirical assessment of the complexity of the syntax of programming languages, so to speak "head-on": to take the [source code of a compiler and look at how many lines in it are occupied by the syntax analyzer](/en/blog/programming/complex-prog/). After all, the more complex the syntax, the more code is needed to recognize it. And hundreds of thousands of lines of code just for analyzing the syntax of C++ are measurable evidence of what a monster C++ has turned into over forty years of development.

But there is another way to assess the same thing — moreover, a much simpler and more accessible one, and without a single line of code base analysis. And this way gives an unexpectedly precise answer to the question of why it is Python that became the standard in machine learning, despite its reputation as a "slow" language.

## Two worlds in one language

When a programmer writes code in any programming language, he works with two fundamentally different groups of notions at the same time.

The first group of notions describes — *what the program does* — these are various business rules, applied and domain entities that are used to describe the algorithm. For example: `class Order`, `enum Status { Paid, Pending }`, `if balance < amount: raise InsufficientFunds` — all this can be read and understood by any person at first glance at the code, even without knowledge of the syntax of a specific language.

The second group of notions that the programmer operates with is the description of *how this is executed* — memory management, call dispatching, thread synchronization and various directives to the compiler. This can be `alignas(64)`, `volatile`, `reinterpret_cast`, `__slots__` — and here a non-programmer is already helpless, and this is normal: these constructs are addressed to the compiler (the machine), and not to a person from the domain area.

And such a division of the syntax of programming languages is not decorative. It is a fundamental architectural division that almost every programming language contains without exception. And the more complex the "machine" part of the language, the worse the text of the program is understood by a non-programmer.

## Criteria for assessing the complexity of the syntax of a PL

It may seem that attributing a programming language to the categories "low-level/high-level" or "system/applied" is a purely expert assessment, and a rather relative one. For example, C++ or Rust will be high-level languages compared to Assembler. However, the use of the terms "system" or "applied" depends not even on the language, but on the purpose of a specific program.

But if we talk specifically about the syntax, then one can take the full list of keywords (including built-in annotations) and ask only two questions about each of them:

> 1. *Can an expert of the domain area, i.e. a non-programmer, understand the purpose of this construct without knowledge of the language?*
> 2. *Does this language construct affect the compiler, the runtime or the hardware architecture?*

Then a quite understandable criterion for classifying the keywords of the syntax is obtained: to attribute them to the system or applied category, after which one can compute the percentage of applied terms in the total lexicon of the language.

### C++

The full list of keywords according to the C++20 standard is **92 words**, plus various operators and standard attributes (`[[nodiscard]]`, `[[deprecated]]`, `[[likely]]`, etc.), plus `break`, `continue`. In total **115** lexical constructs, of which:

- Applied (speaking about the domain area) — only **34**. Keywords: `bool`, `enum`, `enum class`, `const`, `struct`, `return`, `override`, `operator`, `if`, `else`, `switch`, `case`, `default`, `for`, `while`, `do`, `try`, `catch`, `throw`, `class`, `public`, `private`, `protected`, `co_await`, `co_yield`, `co_return`, `concept`, `requires`, the textual aliases of logical operators: `and`, `or`, `not` (although they are not recommended for use, they are nevertheless present in the lexicon of the language) and several attributes: `[[nodiscard]]`, `[[deprecated]]`, `[[fallthrough]]`.
- System (speaking about the compiler, memory, hardware) — **81 of 115**. Keywords: `int`, `short`, `long`, `float`, `double`, `char`, `wchar_t`, `char8_t`, `char16_t`, `char32_t`, `void`, `unsigned`, `signed`, etc. The textual aliases of bitwise and compound operators: `and_eq`, `or_eq`, `not_eq`, `xor`, `xor_eq`, `bitand`, `bitor`, `compl` and various attributes: `[[maybe_unused]]`, `[[likely]]`, `[[unlikely]]`, `[[noreturn]]`, `[[carries_dependency]]`, `[[no_unique_address]]`, `[[optimize_for_synchronized]]`.

In total, applied terms in the syntax of C++ are only 34 of 115 (about 30%).

### Python

The official list of keywords of Python 3.12 is 35 words, plus 8 built-in decorators (`@property`, `@staticmethod`, `@classmethod`, `@abstractmethod`, `@override`, `@dataclass`, `@cached_property`, `@functools.wraps`). In total 43 constructs, of which:

- Applied — **35 of 43**. These are the keywords: `True`, `False`, `None`, `def`, `return`, `lambda`, `yield`, `class`, `if`, `elif`, `else`, `for`, `while`, `try`, `except`, `raise`, `finally`, `with`, `import`, `from`, `as`, `async`, `await`, `del`, `not`, `and`, `or`, `is`, `in` and the decorators: `@property`, `@staticmethod`, `@classmethod`, `@abstractmethod`, `@override`, `@dataclass`.
- System keywords of Python: `global`, `nonlocal`, `pass`, `break`, `continue`, `assert`, `type` — only 7 words, and two decorators: `@cached_property`, `@functools.wraps`.

In total, applied terms of Python: 35 of 43 (more than 81%)!

### Rust

The official list of keywords of Rust is 47 keywords (of which 9 are reserved) and 49 built-in attributes, in total **96** terms, of which:

- Applied keywords: `true`, `false`, `enum`, `const`, `let`, `struct`, `fn`, `return`, `trait`, `pub`, `where`, `if`, `else`, `match`, `for`, `in`, `async`, `await` and attributes: `#[derive(...)]`, `#[must_use]`, `#[deprecated]`, `#[test]`, etc. — a total of 26 applied terms.
- System keywords: `type`, `impl`, `dyn`, `Self`, `mut`, `ref`, `static`, `move`, `unsafe`, `extern`, `break`, `continue`, `super`, `as`, `loop`, `use`, `crate`, `mod`, `become`, `box`, `do`, `final`, `macro`, `override`, `priv`, `typeof`, `unsized`, `virtual`, `yield`, `abstract`, `alignof`, `offsetof`, `pure`, `sizeof` — a total of 29 keywords, plus 41 system attributes such as `#[cfg(...)]`, `#[cfg_attr(...)]`, `#[inline]`, etc.

In total, applied terms in Rust are 26 of 96 (only 27%).


### As a result, the applied part of the syntax of a PL looks like this

| Language       | Total constructs | Applied | System | % applied |
| ---------- | ----------------- | ---------- | --------- | ------------ |
| **Python** | 43                | 35         | 9         | **81%**      |
| **Rust**   | 96                | 26         | 70        | **27%**      |
| **C++20**  | 115               | 34         | 81        | **30%**      |
| **C++26**  | 121               | 37         | 84        | **31%**      |

**C++ is rightly considered the most complex.** It has not only the largest syntactic lexicon (121 constructs against 43 in Python), but also a very unfavorable ratio for the applied user: 84 system terms against 34 applied ones. A C++ programmer is forced to keep in mind not only the logic of the domain area, but also memory management (`new`/`delete`, RAII, `alignas`, `alignof`), type casts (several different `*_cast`), compiler optimizations (`constexpr`, `consteval`, `constinit`, `inline`, `noexcept`), dispatching (`virtual`, `override`, `final`) and metaprogramming (`template`, `typename`, `decltype`). No other industrial programming language requires explicit management of such a number of system details at the same time.

The share of applied terms in the Rust grammar is 27%, which is even less than in C++. But despite this, it is still easier to work with. And the secret lies in a successful architectural decision: in Rust the system concepts are not smeared across keywords, as in C++, but moved into attributes with a uniform syntax. Due to which the keywords remain relatively applied (`trait`, `match`, `pub`, `where`), and the system details (`#[repr]`, `#[inline]`, `#[cfg]`) are moved into a separate syntactic layer that is visually easily distinguishable in the source text of the program.

Nevertheless Python is still not simply "simpler" — its syntax is deliberately oriented toward the domain area significantly more than C++ or Rust. And this is not a subjective feeling, but an architectural property of the language: of 43 constructs, 35 speak about the domain area, and only 9 about runtime details. Therefore `if balance > 0 and status == "active"` will be understood by any person even without knowledge of Python.

### Why did Python win in ML?

It turns out that there is nothing surprising in the fact that Python is preferable for use in any experiments and prototypes. A researcher who came from physics or biology reads:

```python
for batch in dataloader:
    logits = classifier(batch.features)
    loss = cross_entropy(logits, batch.labels)
    loss.backward()
    optimizer.step()
```

Here every word is from his professional vocabulary. `for`, `loss`, `backward`, `step` — this is mathematics written as code. The system details (`event loop`, `GIL`, `reference counting`) have no keywords here, because Python deliberately removed them from the syntax.

And here is how the analogous fragment would look in C++:

```cpp
for (size_t i = 0; i < dataloader.size(); ++i) {
    const auto& batch = dataloader[i];
    
    // RAII wrapper for aligned memory
    alignas(64) std::vector<float> logits;
    logits.reserve(num_classes);
    
    // nullptr check before dynamic_cast
    if (auto* derived = dynamic_cast<DerivedClassifier*>(classifier.get())) {
        derived->forward(batch.features, logits);
    } else {
        throw std::runtime_error("Invalid classifier type");
    }
    
    // Computing the loss function
    const float loss = cross_entropy(
        logits.data(), 
        batch.labels.data(),
        static_cast<size_t>(batch.labels.size())
    );
    
    // Backpropagation with overflow checking
    if (!std::isfinite(loss)) {
        throw std::overflow_error("Loss is infinite or NaN");
    }
    
    backward(loss, classifier->parameters());
    
    // Weight update with a mutex for thread-safety
    {
        std::lock_guard<std::mutex> lock(optimizer_mutex);
        optimizer.step(classifier->parameters());
    }
}
```


C++ is saved by nothing: neither generic programming, nor smart pointers. In C++ the low-level system vocabulary dominates: `alignas`, `std::vector::reserve`, `dynamic_cast`, `static_cast`, `std::isfinite`, `std::lock_guard`, `std::mutex`, `try`/`catch`, `std::bad_alloc`, `std::bad_cast`, `std::exception`, `throw`. Every line requires explicit management of types (`const auto&`, `size_t`, `float*`), memory safety (even RAII wrappers), correctness checks (`nullptr`, `isfinite`) and thread synchronization (`mutex`, `lock_guard`).

The researcher sees not the mathematics of learning — he sees the internals of the execution infrastructure. And this is not a shortcoming of C++: it is a record of implementation subtleties that in Python are hidden behind the interpreter. But this is a different level of the language — a language of resource management and full control over execution, but not of the domain area.


For honesty, here is the Python code with error handling:

```python
for batch in dataloader:
    try:
        logits = classifier(batch.features)
        loss = cross_entropy(logits, batch.labels)

        if not loss.isfinite():
            raise ValueError(f"Loss is not finite: {loss.item()}")

        loss.backward()
        optimizer.step()

    except RuntimeError as e:
        logger.error("Forward/backward pass failed: %s", e)
        classifier.zero_grad()
        raise
    except ValueError as e:
        logger.error("Invalid loss value: %s", e)
        raise
```

And even in this case Python remains in the applied vocabulary: loss, backward, step, isfinite — all of this is mathematics and the domain area. The system details (RuntimeError, zero_grad) appear only in the `except` blocks — that is, in exceptional situations, and not in the main flow of the logic. Whereas in C++ the low-level system terms (`std::isfinite`, `std::lock_guard`, `dynamic_cast`, `static_cast`) stand right in the main flow, next to `loss` and `backward`.

A slightly different situation is obtained with a program in Rust:

```rust
for batch in &dataloader {
    let logits = classifier.forward(&batch.features)?;
    let loss = cross_entropy(&logits, &batch.labels)?;

    if !loss.is_finite() {
        return Err(TrainingError::InvalidLoss(loss));
    }

    loss.backward()?;
    optimizer.step(classifier.parameters())?;
}
```

This variant reads *almost* as easily as Python — `for`, `loss`, `backward`, `step` remain in place. But the system vocabulary still leaks through: the `&` before each argument is an explicit borrow, which the *borrow checker* requires to be always designated. The `?` after each call is an explicit handling of `Result<T, E>`, which cannot be silently ignored. And practically any specialist of the domain area (i.e. a non-professional programmer) will stumble precisely here: not on the logic of machine learning, but on why one cannot write simply `classifier.forward(batch.features)`.

The very same Rust code with explicit management of ownership, synchronization and error handling:

```rust
for batch in dataloader.iter() {
    // Explicit borrow: the borrow checker requires knowing
    // who owns the data and for how long
    let logits = match classifier.forward(&batch.features) {
        Ok(output) => output,
        Err(e) => {
            eprintln!("Forward pass failed: {}", e);
            classifier.reset_gradients();
            return Err(TrainingError::ForwardFailed(e));
        }
    };

    let loss = match cross_entropy(&logits, &batch.labels) {
        Ok(l) if l.is_finite() => l,
        Ok(l) => {
            return Err(TrainingError::InvalidLoss(l));
        }
        Err(e) => return Err(TrainingError::LossFailed(e)),
    };

    // backward() consumes loss - ownership is transferred,
    // after this line loss cannot be used
    loss.backward().map_err(|e| {
        classifier.reset_gradients();
        TrainingError::BackwardFailed(e)
    })?;

    // Weight update through a Mutex - explicit synchronization,
    // the lock lives exactly until the end of the block (RAII)
    {
        let mut params = classifier
            .parameters()
            .lock()
            .map_err(|_| TrainingError::PoisonedMutex)?;

        optimizer.step(&mut params).map_err(|e| {
            TrainingError::OptimizerFailed(e)
        })?;
    } // the lock is released here automatically
}
```



In Rust every construct carries a concrete system guarantee, but for this Rust pays with the fact that its system vocabulary is visible even in applied code (exactly as in the case of C++). And this happens everywhere, even in the little things. The simplest operation — checking the finiteness of loss:

```python
# Python: hidden behind a framework exception
loss.backward()
```
```cpp
// C++: explicit check + explicit exception + explicit type
if (!std::isfinite(loss)) {
    throw std::overflow_error("Loss is infinite or NaN");
}
```
```rust
// Rust: the compiler requires handling Result explicitly
if !loss.is_finite() {
    return Err(TrainingError::InvalidLoss(loss));
}
```

In Python one does not need to check anything — the framework will do it itself or crash with an understandable message, whereas C++ requires an explicit check, because arithmetic with `double` does not throw exceptions by the standard, and this is knowledge about the internal structure of the numeric representation, and not about the task. Rust goes even further: it forbids ignoring an error at the level of the type system.


It is precisely in such details that the difference lives between the 80% applied constructs of Python and all the other languages: not in syntactic sugar and not in the length of the code, but in how much system knowledge one needs to keep in mind in order to write a seemingly very simple thing.

## All languages evolve toward the domain area

Almost every major update of industrial languages over the last ten years added mainly applied, and not system, terms. The general trend is unambiguous: system mechanisms go into automatism, applied constructs become more expressive.

This is especially noticeable in C++ — but with a paradoxical result. C++20 brought `concept` and `requires` — constructs that speak about intention, and not about a mechanism. C++23 added `std::expected`. C++26 takes the boldest step: static reflection through `std::meta` removes macros and code generators, the `pre`/`post` contracts turn business constraints into part of a function signature, `std::execution` hides asynchrony behind a readable pipeline.

```cpp
double sqrt(double x)
    pre(x >= 0.0)
    post(result: result * result <= x);
```

This will be understood by any person with a mathematical education. But the same contracts have several checking modes, interact with `noexcept` and behave in a special way during inheritance. `std::meta` requires an understanding of `constexpr` computations and the new `^` operator. `std::execution` introduces five new abstractions: sender, receiver, scheduler, operation state, completion signatures.

Here is the paradox: every applied addition drags along a new layer of system concepts. When Python adds `@dataclass`, a beginner uses it the next day. When C++26 adds `std::meta`, between "understood the idea" and "apply it correctly" lie weeks. The language becomes more expressive for those who already know it deeply — and more complex for everyone else. This is not a failure of the committee, but the honest price of universality: a language that simultaneously manages bytes and expresses business contracts cannot be simple by definition.

In short, it looks something like this:

![](/en/blog/comparison/cpp-rust-vs-python.png)

## Summary

Disputes of "Python vs C++" or "C++ vs Rust" are traditionally conducted in terms of performance, type safety or the size of the ecosystem. But there is one more dimension that is rarely named explicitly: **for whom is the syntax of the language designed?**

When a non-programmer reads Python code and understands it without immersion in runtime details — this is not an accident, but the result of the language design. When a C++ programmer writes `[[nodiscard]] constexpr auto compute() noexcept` — every word is addressed to the compiler. This is not bad: it is an honest record of system guarantees, but it is not an applied, but a system vocabulary.

