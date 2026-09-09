---
slug: reference-solution
title: A possible solution to the problem of references in programming languages
date: 2024-04-23
tags: [memory-safety, language-design, programming-languages]
---

![](/en/blog/language-design/reference-solution.jpeg)

Any programmer is familiar with the notion of a "reference". This term usually means a small object whose main task is to provide access to another object physically located elsewhere. Because of this, references are convenient to use, they are easily copied, and with their help it is very easy to gain access to the object that the reference refers to, and one can gain access to the same data from different places in the program.

Unfortunately, references, or more precisely manual memory management, are the most frequent cause of various errors and vulnerabilities in software. And all attempts at automatic memory management with the help of various managers run into the need to control the creation and deletion of objects, as well as to periodically run the garbage collector, which does not at all have a positive effect on application performance.

Nevertheless, references in one form or another are supported in all programming languages, although this term often means not fully equivalent notions. For example, by the word "reference" one can understand a reference as an address in memory (as in C++) and a reference as a pointer to an object (as in Python or Java).

Although there are programming languages that try to solve these problems through the concept of "ownership" ([Rust](https://www.rust-lang.org/), [Argentum](https://aglang.org/) or [TrustLang](/en/)). The possible solution to these and other existing problems with references will be discussed further.

## What kinds of references are there?
For example, in the C language there are pointers, but working with them is not very convenient and at the same time very dangerous because of the presence of address arithmetic (the ability to directly change the address of a pointer to data in computer memory). In C++, a separate entity appeared — reference, and in C++11 references received further development, rvalue references appeared.

Whereas in C++ there are several kinds of references at once, the Python developers probably deliberately tried to "simplify" working with references and abandoned them altogether. Although de facto in Python every object is a reference, although some types (simple values) are automatically passed by value, whereas complex types (objects) are always by reference.

## Cyclic references
There is also a global problem of cyclic (circular) references, which affects almost all programming languages (when an object points to itself directly or through several other objects). Often, language developers (primarily of languages with garbage collectors) have to resort to various algorithmic tricks to clean the pool of created objects from such "hung" and cyclic references, although usually this problem is left to the developers, for example, in C++ there are strong (`std::shared_ptr`) and weak (`std::weak_ptr`) pointers.

## Ambiguous semantics of references
Another no less important, but often ignored, problem of references is the semantics of the language for working with them. For example, in C/C++, to access data by reference and by value, separate operators are used: the star "\*", the arrow "**->**" and the dot "**.**". But working with reference variables in C++ happens as with ordinary variables "by value", although in fact this is not the case. Of course, with the explicit typing of C++, the compiler will not let you make a mistake, but when simply reading the code, you will not be able to distinguish a reference variable from an ordinary "by value" one.

But in Python it is very easy to confuse how a variable will be passed as a function argument, by value or by reference. Since this depends on the data itself contained in the variable. A special piquancy here is added by the fact that Python is a programming language with dynamic implicit typing, and in the general case it is not known in advance what value is stored in the variable.

## Who is to blame and what to do?
It seems to me that the main reason, at least of the ambiguous semantics, is the constant growth of the complexity of development tools, and as a consequence — the complication and refinement of the syntax of programming languages for new concepts and capabilities while maintaining backward compatibility with old legacy code.

And what if we start from a clean slate? Here, for example, is a universal concept of managing objects and references to objects that does not require manual memory management from the user (programmer), for which no garbage collector is needed, and errors when working with memory and references to objects become impossible due to full control of memory management already at the stage of compiling the application source code!

#### Terms:
Object — data in computer memory in machine (binary) representation.
Variable — a human-readable identifier in the body of the program, which is unambiguously determined by its name and identifies an object (the immediate value of the object or a reference to it). Variables can be:
 - An owner variable — the only permanent reference to an object (shared_ptr).
 - A reference variable — a temporary reference to an object (weak_ptr).

#### Possible operations:
- creating a new owner variable and initial initialization of the object's value.
- creating a reference variable to an existing owner variable.
- assigning a new value to the object by the name of the owner variable.
- assigning a new value to the object pointed to by the reference variable.
- assigning to a reference variable a new reference to another owner variable.

An example of source code and conditional abbreviations:
- "**&**" — creating a reference to a variable
- "**\***" — accessing the data of an object

```python
# variables - owners
val1 := 1; 
val2 := 2;
# val1 = 1, and val2 = 2

val1 = val2; # Error - there can be only one owner!
*val1 = *val2; # OK - the value is assigned
# val1 = 2, and val2 = 2

*val1 = val2; # Also OK - an owner variable is used in an expression as an rvalue


# variables - references
ref1 := &val1;
ref2 := &val2;
# ref1 -> val1, and ref2 -> val2

ref1 = 3; # Error - the variable is a reference!
*ref1 = 3; # OK - the value is assigned "by reference"
# val1 = 3, and val2 = 2

*ref2 = *ref1;  # OK - one value is assigned to another value "by reference"
# val1 = 3, and val2 = 3

ref1 = ref2; # OK - one reference is assigned to another reference
# ref1 -> val2, and ref2 -> val2

ref1 = 5;
# val1 = 5, and val2 = 5

# Getting data "by reference" for the fields of a structure (class).
class A {
  A ref;
};
A owner := A();
owner.ref := &owner;  # A cyclic reference :-)
# owner.*ref -> owner

A link := &owner;
# *link.ref  - reference field
# *link.*ref -> owner
```

With such a syntax everything becomes simple, visual and understandable.
But if I missed something somewhere, please write in the comments on Habr or in a personal message.

