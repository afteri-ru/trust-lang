---
slug: inter-prog
title: International programming in natural languages
date: 2020-12-05
tags: [programming, programming-languages, syntax, language-design]
---


![Featured image](/en/blog/langs.jpeg)


{{% pageinfo %}}

Attention!!!

This article contains a description of the syntax of the previous version of NewLang.

The current version of the language syntax can be viewed [here](/en/docs/).

{{% /pageinfo %}}



Lately I often come across articles about new programming languages,
as well as various ratings and forecasts related to the popularity of computer languages.

New tooling also announces itself,
which in its work uses its own formats for describing configuration files or sequences of executed commands,
which also brings them very close to the notion of a "programming language".

The purpose of writing this article is to formulate the expectations and a possible implementation of an abstract programming language
that could become a universal tool for communication between a computer and a human.

## About programmers
If we start from the very beginning, then a long time ago I heard a paraphrased statement,
"every programmer must write his own database, text editor and programming language".
And while I wrote the first two things long ago, with a programming language it has not worked out yet.

After all, how are programming languages usually created?

Every programmer always has some previous experience:

- knowledge of one or several programming languages (how could it be otherwise)
- negative experience from using them (otherwise, if everything suits you, why invent something new?)
- the desire to get new capabilities (when something is missing in the existing languages)

And before describing the syntax, choosing keywords and starting the main work:
lexer, parser, base libraries, one needs to answer the main questions:

- Compiler/interpreter/transpiler (JIT)?
- Static or dynamic typing?
- Manual memory management or automatic with a garbage collector?
- Programming model: OOP, functional, structural or something new?
- Are inserts from other programming languages allowed, and so on?

I, probably like most readers, have experience using several programming languages.
Therefore a practice has long been established that to solve a task it is better to take a known language or even learn a new one, instead of
starting to write one's own.

All the more so because one does not want to invent yet another language just for the sake of a checkmark or for the sake of the language itself.
I believe that the purpose of its creation must be beyond the needs of the developer himself.

And it seems to me that I managed to identify an area for which the development of a programming language can be in demand,
and the effort spent on it can bring real benefit.

## About non-programmers
This area is programming for "non-programmers" in a "natural" language.
I deliberately put the words "non-programmers" and "natural" in quotes, since these terms are very conventional.

After all, if a non-programmer starts programming, then, without realizing it, he automatically becomes a programmer ;-).
And a programming language cannot be "natural" by definition.
More precisely, for computers the "natural" language will most likely be Assembler or a set of machine instructions.

> Therefore, the maximum goal is to bring the programming language closer to the natural human language.

This will not only make the reading of the program text more understandable for non-professionals,
but will also make it possible to start composing programs simply by mastering written speech, using the very minimum of basic rules.

But this formulation hides a very big problem!

> Any programming language is international, since its syntax does not depend on the natural language in which the programmer communicates.

And if the program text is in a "natural" language, then it will become understandable only to those who know that language,
at the same time becoming incomprehensible to everyone else.

As an illustration: one or two.
If we fantasize about the wants for such a language, the following requirements and limitations are seen:


- Since every user is a speaker of his native natural language (or even several), it is impossible to rigidly fix keywords, from which it follows that the basis of such a language must be only the rules of punctuation, and by no means the lexicon or grammar.
- The compiler/translator must be able to convert the source text of the program not only into machine code for the computer, but into another variant of the "natural" language, so that the user can work with the source text in a "natural" language known to him.
- One very much wants to see in the new language ~~tolerance~~ forgivingness toward typos. Such a "feature" is present in writing in a natural language, and despite the presence of typos, the meaning is almost always preserved. Naturally, in this case one should not go to the point of fanaticism. The compiler does not read minds and cannot really "understand" what the user meant, and yet quite often one can ignore typos in the program text based on the context (albeit with the output of warning messages).


Nevertheless, such a language must remain precisely a programming language with all the capabilities of creating programs of any level of complexity,
including functional and object-oriented programming and an unambiguous understanding of what is written.

## About a hypothetical language
If we take the rules of written speech as a basis, then the main conventions and punctuation for the new language might look something like this:

- Any text consists of sentences and comments. Sentences are processed, and comments are ignored.
- A sentence consists of a sequence of terms, literals and symbols separated by spaces and punctuation marks and ends with an end-of-sentence symbol.
- A term is a solidly written sequence of letters, digits and the symbols ":" and "_".
- A literal is a constant included directly in the program text whose type is determined unambiguously. These are character strings in quotes, integer and real numbers, and some special formats (time, date).
- Symbols are all the other symbols that do not belong to punctuation marks, whitespace characters, digits and letters.
- Punctuation marks are punctuation symbols that have a special meaning when parsing the program text:
    - ".", ";", "!", "?", "…" — end of sentence.
    - "=" — assignment of a value.
    - "" (quotes) — definition of a character string.
    - "()" — passing parameters/arguments or grouping operators to define the priority of performing operations.
    - "[]" — an array or access to an array element.
    - "{}" — inclusion in the source code text of a program in an ordinary programming language.
    - "$" — a system variable.
    - "@" — a system function.
    - "," (comma) — enumeration.
    - ":" (colon) — a list or a logical connection.

If with the assignment symbol, quotes, parentheses and square brackets everything should be more or less clear,
since their purpose corresponds to the analogous one in the overwhelming majority of programming languages,
then about the purpose of the remaining symbols (curly braces, colon, comma and system functions/variable), a little should be explained.

Since the goal of the hypothetical programming language is still the writing of programs,
it is necessary to provide the possibility of making inserts of ordinary program code without taking into account all the capabilities and ambiguities
that are inherent in any natural language.

This capability is also required for implementing low-level functions and for interacting with external libraries.

When creating such inserts, curly braces can be used, and all the text between them will be inserted into the final file practically without processing.

The symbols "$" — a system variable — and "@" — a system function — serve similar purposes. If such a symbol is placed at the beginning of a word,
then it will denote an object with the corresponding purpose. For example "@exit" will mean a function,
and "$var" a variable with the corresponding names, and the objects themselves will become available both in ordinary code
and in program inserts inside curly braces.

In a similar way, access to individual fields/methods of objects is organized:
"object@method" or "object$field".

The comma symbol "," is used to indicate a sequence of equal logical blocks in one sentence or to create lists.

The colon symbol ":" is used to create lists and to denote a logical connection between two parts of a word/text, including to specify the full path of a module.

For example, creating a list:


`To_string: element 1, element 2, the last element.`
```
Formatted_list:
- element 1;
- element 2;
- the last element.
```

A logical consequence/indication of a connection:

```
module:calc //the term "calc", which is in the module "module"
super:module:example$var //the variable "$var" which is in the specified hierarchy.
```
As can be seen, the use of punctuation marks is taken from their direct purpose, accepted in written speech,
which should provide a certain compromise between the syntax in standard programming languages and writing in a natural language.

## About computers
Since we are still talking about a programming language, one cannot do without standard algorithmic constructs: sequence, selection and loops.

Sequence is easily described by the ordinary rules of writing in a natural language.
In the case of sequential execution within one sentence, operations and function calls are written sequentially separated by commas.
If they are located in different sentences, they are written just as one after another.
Moreover, formatting into paragraphs serves only for a better perception of the text and the logical separation of individual fragments.

When creating conditional and cyclic control constructs, keywords will already be required.
But since, according to the initial wishes for the language, ordinary terms cannot be reserved for writing algorithmic constructs,
it is enough to put the symbol of a system function before the keywords, which will make it possible to distinguish an ordinary term from a keyword (control) word.

Naturally, when programming, these terms can be used, but doing so is not at all mandatory.
Since when tuning to a specific natural language, system functions and keywords must be assigned specific terms and then used, for example:

```
goto = @goto,
label = @label,
continue = @continue,
break=@break, etc.
```

And the last construct in order, but probably the most important in essence: the passing of parameters in function calls.
If we strive for a fully natural syntax, then we will get that very natural language which is very difficult to analyze.

Nevertheless, it seems to me that both approaches can be combined if we abandon the mandatory use of parentheses,
where it is permissible by the syntax.

```
Computer-like: function(parameter1, function2(), parameter3=value).
Natural-like: function parameter1 function2 parameter3=value.
```

But:

```
Computer-like: function( function2(parameter) ).
Natural-like: function function2(parameter).
Or like this: function (function2 parameter).
```

In other words, for the natural order of specifying arguments, the parentheses for functions and the commas between parameters can be omitted.
Although their use should be determined first of all by the target natural language, and not by the syntax.

## About objections
I foresee well-founded objections to the use of such a language from programmers.
A program in it will in any case turn out significantly more verbose than with the use of the strict formal syntax of ordinary computer languages.

Therefore let me remind you of its mandatory property — the ability to convert the program text from one language to another.
This makes it possible to write programs both with the use of a strictly formal syntax without using redefined terms in a natural language,
and to convert the source text into a "natural" language for a "non-programmer".

Original publication

