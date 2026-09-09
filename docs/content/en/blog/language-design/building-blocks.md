---
slug: building-blocks
title: Building blocks of programming languages
date: 2024-05-03
tags: [programming-languages, syntax, language-design, parser]
---

As a result of the feedback on the article with [reflections on structured programming](/en/blog/theory/structured-programming/), there were many comments and disputes in the comments, for which I want to say a big thank you to everyone.

And under the impression of the discussion of the previous article, I asked myself whether there exists a minimal set of lexemes, operators or syntactic constructs with the help of which one can build an arbitrary grammar for a modern general-purpose programming language?


## Introduction
Almost all programming languages are built either on the principle of similarity (make it like that one, only with its own blackjack), or for the implementation of some new concept (modularity, purity of functional computations, etc.). Or both at the same time.

But in any scenario, the creator of a new programming language does not take his ideas randomly out of thin air. They are still based on his previous experience, obsession with a new concept and other initial attitudes and limitations.

I will admit right away that I cannot unambiguously list the minimal set of basic operators and constructs that would be sufficient for a *modern* programming language. Moreover, I am not sure that such a set is possible at all, because many constructs can be represented with the help of others of a lower level (for example, a conditional/unconditional jump). I remember about the Turing machine, but I am interested in real programming languages, and not in machine instructions of an abstract executor.

Therefore, as the basic bricks of programming languages, one can safely adopt those capabilities that were invented and implemented by the developers of mainstream languages. And it is probably better to start with criticism of individual and well-known fundamental concepts. And no, it is not the goto statement!

### Strange increment and decrement (++ and \-\-)

In my opinion, the most ambiguous operators are the operators for increment and decrement, i.e. the arithmetic increase or decrease of the value of a variable by one. They introduce serious confusion into the strict grammar of the language, which, in my opinion, is simply obliged to be as transparent and *unambiguous* as possible.

The main problem with these operators is that, being arithmetic operators, they *modify* the value of a variable, whereas all the other arithmetic operators operate on *copies* of values without changing the variable itself directly.

They may object to me that the operators +=, -=, \*= or \= also change the value of a variable, but I want to note that this is only a simplified notation of a combination of two operators, one of which is precisely intended for assigning a new value to a variable, therefore the objections are not accepted. :-)

And if we also recall that the increment and decrement operators can be prefix and postfix, then in combinations with address arithmetic (\*val++ or, God forbid, some ++\*val++), a brain explosion with possible errors is simply guaranteed.


### Too few value assignment operators
Yes, you are reading it all correctly! I really do criticize the value assignment operator from a single equals sign "**=**", since I believe that it is not quite complete. But unlike increment and decrement, which the lexicon of the language can easily do without, one cannot do without the assignment operator at all!

But my criticism is directed not at the operator itself, but at its incompleteness and the creation of additional confusion in some programming languages. For example, in Python itself it is impossible to understand whether a variable is being created (i.e. the first use of the variable) or whether it is an assignment of a value to a variable that already exists (or the programmer made a typo in the variable name).

If we recall the royal "criticize — propose", it seems to me that it would be correct to make two different operators: an *assignment of a value* operator and a *creation of a variable* operator (in C/C++ the logic of creating a variable is performed by specifying the type of the variable at its first use).

In other words, instead of one "creation and/or assignment of a value" operator, it is better to use two or even three operators: creating a new variable (**::=**), only assigning a value to an existing variable (**=**) and creating/assigning regardless of the existence of the variable (**:=**) — i.e. an analog of the current **=** operator.

And in this case, the compiler, already at the level of the source syntax, could control the creation or reuse of a previously created variable according to the programmer's intentions.

Also, I would add a "swap values" operator, some **:=:**. In fact, it is an analog of std::swap() in C++, only at the level of the language syntax.

### An always-superfluous data type

In all mainstream programming languages, as a rule, there are numbers of different bit widths. This is a forced necessity, since the bit width of computations is determined by the hardware level and language developers cannot fail to take this into account.

It is a different matter with the boolean (logical) data type. In the description of one language I even came across this:
```
Bool        1 Byte truth value
(Bool16)    2 Byte truth value
(Bool32)    4 Byte truth value
(Bool64)    8 Byte truth value
```  
And when you dig a little deeper, it all comes down to a single bit, which can represent two opposite states: YES/NO, true/false, 1/0...

But excuse me, if it is 1 or 0, then why not define right away that the logical type is a number with one bit? (warm greetings to LLVM!).

After all, there is no work worse than the meaningless work of converting numbers into logical values and back:

> In Java there are rather strict restrictions with respect to the boolean type: values of the boolean type cannot be converted to any other data type, and vice versa. In particular, boolean is not an integer type, and integer values cannot be used instead of boolean ones.

And also, in some programming languages that support the value "Empty/None", the boolean data type can even turn into a tribool, for example in the case of default function parameters, when a state "not set" is added to a boolean argument. But from the point of view of using uninitialized variables, this is at least understandable and logically explicable.

### Null pointer
In all mass programming languages, in one way or another, there is a data type called a *reference*. And in some languages the reference types can be of several kinds at once.

However, the presence of reference data types immediately adds several uncertainties, such as memory management and shared resources (more in the article [Memory management and shared resources without errors](/en/docs/safety/memory/)). In addition, with the presence of address arithmetic (explicit or implicit), it immediately becomes necessary to use a special reserved value called a "null pointer", **NULL**, **nil**, **nullptr**, etc., depending on the language.

The presence of such a value forces language developers to significantly complicate the syntax and logic of working with pointers through the control of the explicit/implicit possibility of storing a null pointer in a reference variable.

But in the case when the language compiler itself manages and controls reference data types and shared resources, then the very notion of a "null pointer" becomes unnecessary and will be hidden from the programmer in the details of the implementation.

### The result of the last operation

There are situations when a system variable with the value of the result of the last operation is missing. Some analog of `$?` in bash scripts, but at the level of the source code of Python or C/C++.

But I do not mean a concrete physical variable, but a certain generalized identifier with the result of the last operation. A pseudo-variable that is managed by the language compiler. In other words, so that the type of this pseudo-variable changes depending on which operation was the last.

This could simplify the solution of frequently arising tasks, for example, getting the last value after exiting a loop.

Or with the help of such a pseudo-variable one could significantly simplify the syntax of exception handling, where the capture is implemented on the basis of types. But at the same time as the type of the exception being captured, one also has to define a variable, even if it is not used further in any way.

###  Pure functions

Also, I would sometimes like to have the ability to create pure functions in C/C++ or Python, so that the compiler itself controls the ban on accessing global variables or non-pure functions at the level of the language syntax, and this would be checked at compile time.

###  Empty variable name

And finally I want to say that in C++ an empty variable "**_**" (as in Python) was very much missed. But in the latest standard proposals it seems to have been brought in, so starting from C++26 we will have happiness :-)


### Total

While writing this article, I tried to abstract myself and approach my more than thirty years of development experience without bias, but I am not sure that I succeeded, therefore I will be glad to receive any remarks and objections in the comments.

If it is not difficult for you, write in the comments which capabilities in modern programming languages, in your opinion, hinder more than help, or, on the contrary, which operators/syntactic constructs you are missing.

It is always interesting to learn what you missed, forgot or failed to take into account.

