---
slug: unusual-prog
title: An unusual concept of the syntax of a programming language
date: 2021-05-24
tags: [syntax, programming-languages, language-design, overview]
---

![Featured image](/en/blog/langs.jpeg)

{{% pageinfo %}}

Attention!!!

This article contains a description of the syntax of the previous version of NewLang.

The current version of the language syntax can be viewed [here](/en/docs/).

{{% /pageinfo %}}


I want to present to the readers' discussion a somewhat unusual concept of a programming language, which lacks the problem
inherent in almost all industrial languages — the constant increase in the complexity of the language syntax due to its natural development as new versions are released and new features are added.
This problem is described in the article ["Simple complex programming"](/en/blog/programming/complex-prog/) and [What is the "ideal" goal of the development of programming languages?](/en/blog/language-design/lang-final/)

After several experiments with the syntax, I want to test the developed idea on a wide audience of Habr, which is the best fit for these purposes.

Language features:
- A low entry threshold and a natural limitation of the level of complexity over a long time, even under the condition of constant development of the language itself.
- The ability to write program code both in the declarative and in the imperative paradigm using the procedural, modular and object-oriented approach.

And, in accordance with my own observation Habr — a chamber of wisdom, I will be glad to receive any comments and suggestions that will help test or improve the proposed solution.

## Introduction
The first unusual feature of the language is the complete absence of reserved keywords.
More precisely, it is planned to use only one single keyword (the name of the language), which can be both the main entry point into the application
and a way of accessing the language settings for a specific domain area, for example if it is necessary to implement the [DSL paradigm](https://en.wikipedia.org/wiki/Domain-specific_language).

At the present moment the name of the language has not been chosen, therefore for the examples simply the word _lang_ is used, which will be changed in the future.

This feature (the absence of reserved keywords) is achieved by the fact that the grammar of the language is based on the use of commonly used symbols and classical punctuation marks,
while all the other letter-symbol sequences are considered "tokens" when parsing the program.

The language compiler is implemented as a [JIT](https://en.wikipedia.org/wiki/Source-to-source_compiler) — the conversion of the source code of a program
written in one programming language into equivalent source code in another language.
I started the first experiments with the syntax in Python, but after realizing the need to develop not only an interpreter,
but also a compiler directly into executable code, I decided to settle on C++. Although in principle, the implementation language can be anything.

The use of [transpilation](https://en.wikipedia.org/wiki/Source-to-source_compiler) when implementing the compiler solves many obvious and not so obvious tasks at once.
The need to develop a low-level compiler into executable processor code immediately disappears,
and in my case the main bonus of such an approach becomes the possibility of using the imperative programming paradigm through inserts of code directly into the source code of the application in the implementation language.

And it is precisely this point that makes it possible to naturally separate the declarative and imperative ways of writing a program.
In this case, in the declarative paradigm only one of the three basic control constructs is implemented,
necessary and sufficient when implementing [any algorithm](https://en.wikipedia.org/wiki/Structured_programming) — _sequence_.

In other words, in the declarative style only the _sequence_ of operations that follow one after another is described,
but there is no possibility to program loops or branching (jumps) by condition.
Because of this, in the declarative style one can write only the following types of language constructs:

- comment
- definition of a variable and assignment of a value to it
- creation of an object and assignment of default values to its properties
- creation of a function (a method for the whole class of objects)
- call of a function or iterator
- direct insertion of code in the implementation language

Since the goal of the current publication is to test the general concept, I will start right away with examples without a long description of the details, and I will comment on some nuances along the way.
All the more so because the most important goal, "a low entry threshold", by itself implies the possibility of figuring out the syntax on one's own.
Nevertheless, one cannot do entirely without knowledge of the implementation language, and initial knowledge of programming in C/C++ is still necessary.

## "Hello, world!" in the imperative paradigm
The definition of an ordinary function is performed using the assignment operator ":=", and the program code in the implementation language (in this case in C++)
is enclosed in curly braces. A simple example of printing a string to the screen looks something like this:

```cpp
    print(str="") := { printf("%s", static_cast<char *>($str)); }
```
The **print** function with a default argument in the form of an empty string internally calls the ordinary printf from the standard library.

Accordingly, "Hello, world!" in the imperative programming paradigm will look trivial:

```cpp
    #!/bin/lang
    print(str="") := { printf("%s", static_cast<char *>($str)); };
    @print("Hello, world!\n");
```
From the example it can be seen that access to arguments inside the C++ code occurs using the symbol **$**, which is specified at the beginning of the named argument.
In addition, to access the arguments of a function one can refer to them by their ordinal numbers starting from the first ($1, $2, $3, etc.).
The reserved argument $0 contains the object itself whose method is being called, or _nullptr_ if the function does not belong to an object.

_Ordinary_ functions are precisely ordinary functions in the C/C++ understanding.
Inside them one can write absolutely any code, including condition checks, loops, calls of other functions, etc.
Technically, the code of such a function is parsed for the replacement of the used arguments,
its name is decorated in a special way, and special markers are added to identify the contents.
After that the source text is ready for building by an ordinary C++ compiler to be turned into a dynamic library,
and after it is loaded the function can be called at any moment (to call a function, the symbol **"@"** must be specified before its name).

The direct execution of a file in interpreter mode occurs in two stages. At the first stage, a temporary C++ file with the source code of all functions is generated from the program text; this file is compiled by gcc and a dynamic library is built.

At the second stage, the built dynamic library is loaded by the runtime environment, and the source text of the program begins to be executed sequentially by the interpreter (all lines, except for function definitions).

There is also a variant where, instead of interpreting the program code, a C++ file is generated not only for the functions, but also for the main part of the application.
Then the output of the compiler will already be an ordinary binary file, although in that case it will no longer be possible to promptly fix the program text.

## Logic programming in the declarative paradigm
Since the most famous logic programming language in the declarative style is considered to be Prolog,
I will give a simple example of the Brother program (searching for brothers) in Prolog and the equivalent code in the new language.

Prolog:

```bash
    male("Tom").
    male("Tim").
    male("Jake").
    female("Janna").
    parent("Tom","Jake").
    parent("Janna","Jake").
    parent("Tom","Tim").

    brother(X,Y):- parent(Z,X),parent(Z,Y),male(X),male(Y),X\=Y.
```
Output: **(Jake, Tim) (Tim, Jake)**

The very same example:

```bash
    #!/bin/lang
    human:=@term(sex=,parent=);
    Tom:=@human(male);
    Janna:=@human(female);
    Jake:=@human(male, (Tom, Janna,));
    Tim:=@human(sex=male, parent=(Tom,));

    human::brother(test=human!) &&= $0!=$test, $0.sex==male, @intersec($0.parent, $test.parent);

    human.brother?
```
Output: **[Tim.brother(Jake), Jake.brother(Tim),]**

I hope that the syntax is intuitively understandable, especially given the equivalent code in Prolog, but just in case I will clarify a few points.

```bash
    human:=@term(sex=,parent=);
```
In this line an object with the name "human" is created with two properties "sex" and "parent",
whose default values are undefined, and whose parent is the system object "term".
The use of the symbol **"@"** at the beginning of a term denotes a call of an existing function with the parameters specified in parentheses.
In this case the constructor of the system object "term" is called, and the returned result is the new term "human",
which can be perceived both as a single instance of a class and as the name of a whole class if it acts as a parent for other objects.

```bash
    Tom:=@human(male);
    Janna:=@human(female);
    Jake:=@human(male, (Tom, Janna,));
    Tim:=@human(parent=(Tom,), sex=male);
```
The first two lines create the objects "Tom" and "Janna", whose property "sex" is set to the values "male" and "female" respectively.
And in the last line, when creating the object Tim, the property values are set with the indication of their names.

The constructs _(Tom, Janna,)_ and _(Tom,)_ are the definition of a constant literal of the dictionary type, which are assigned to the specified properties.

So that the definition of a dictionary is not confused with the indication of arguments in a function call, it contains a mandatory trailing comma before the closing parenthesis.
This rule (a trailing comma before the closing parenthesis) also applies when defining an array literal, only for its notation not round but square brackets are used (for example **[,]** is an empty array).
The main difference between a dictionary and an array lies in the ways of accessing their elements.
To access an element of an array an integer index is used, while in a dictionary both an index and the name of an element (if present) can be used.
There are more differences between a dictionary and an array, but they are not fundamental right now.


And the penultimate line in the example:

human::brother(test=human _!_) **&&=** _$0_ != _$test_, _$0_.sex==male, @intersec(_$0_.parent, _$test_.parent);

is the definition of a <i>simple pure</i> function "brother" for all objects derived from "human".
This function accepts one named argument "test", whose default value is an _iterator_ of objects of the class "human".

The operator **&&=** means the definition of a _simple pure_ function, i.e. a function without external dependencies,
which has no access to the global context, and the result of whose execution is a logical value,
which is computed by the scheme of [logical AND](https://en.wikipedia.org/wiki/Logical_conjunction) for all conditions specified in the function body separated by commas.

The execution and output of the result of the program's execution occurs in the line
`human.brother?`

The exclamation and question marks denote an [iterator](https://en.wikipedia.org/wiki/Iterator). The iterator "**!**" returns one current element from the collection and moves the pointer to the next one,
while the iterator "**?**" returns the whole collection of objects at once.

In other words, when executing the program line _human.brother **?**_ a sequential enumeration of the whole collection of objects of the type "human" occurs,
where for each object the function "brother" is executed with default arguments.
And since the iterator _human **!**_ is specified as the default argument, each of the elements of the specified class is sequentially passed as the argument.
In essence, when this line is executed, a complete enumeration of all possible combinations of objects of the class "human", each with each, occurs.

> A more detailed description of the operation of the iterator and the reason for choosing such a syntax was published in a separate article Laconic iterator for declarative syntax

The result of the execution will consist of those pairs of objects for which the function "brother" returns true, i.e. the result of the execution will be an array of two pairs of objects **[Tim.brother(Jake), Jake.brother(Tim),]**.


## Formulation of the concept being tested
The concept being tested is as follows. The syntax of a programming language consists, as it were, of two parts,
each of which is relatively independent and at the same time they are connected with each other.

The first part is complex — it is intended for describing algorithms in the _imperative style_ in an ordinary programming language,
therefore inside functions all the capabilities of the implementation language are available to the developer.

The second part is simple (compared to the first) — it is intended only for the logical description of a task in the _declarative style_.
It is precisely this part that is used for the initial level of use, and the natural limitation to only sequential execution of statements
(i.e. the absence of loops and branchings) does not cause excessive difficulties in understanding the source text of the program even in the future as the language develops.

The connection between the two variants of the syntax is transparent and is realized through the joint use of variables and functions,
whose declaration and reference occur in a unified style in both cases due to the unification of the syntax at the level of the use of punctuation marks.

Right now I deliberately do not give the full syntax and a detailed description of the remaining capabilities of the language, since the compiler is not yet ready for a public presentation.
But I will be extremely grateful for any response or useful suggestions regarding the proposed concept.

This especially concerns possible conflicts in the syntax in the presented examples.

