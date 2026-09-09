---
slug: parser-nuances
title: Nuances of developing a parser for your own programming language
date: 2023-03-29
tags: [parser, compiler, syntax]
---


![Featured image](/en/blog/langs.jpeg)



Recently I read an article on Habr, "My own language, or how I got tired of assembler and C", and my eye involuntarily caught on one paragraph:

> I decided not to bother too much, so I used the parglare library. It is very lightweight and convenient, I recommend it to everyone. To describe the syntax, the parser accepts a string in the corresponding format and uses regular expressions (do not judge regexes, they are omnipotent!).

As a result, I decided to publish an article based on my old notes from back in the days when the idea of [TrustLang](/en/) had not yet fully crystallized, but I already wanted to write real code and test different concepts.

After all, in the life of almost any programmer there may come a moment when a bright idea comes to his mind — to develop his own programming language. Maybe not for the sake of conquering the world, on par with C/C++, Python or at least PHP, but as a personal pet project, with which, on long winter evenings, he will hone his own skill.

And since for any language (not only a programming one) everything starts with the analysis of its grammar, the very first task of the creator will be the choice of tools for the syntax analysis of the source text.

This is a story — notes for memory about the torments of choosing a lexer-parser bundle for parsing the grammar of TrustLang. And also an attempt to describe and systematize conclusions about the features of the different analyzers that I had to work with when choosing a parser for parsing the grammar of my own programming language.

### Terms used.
So that it is clear what will be discussed further.

> Lexer — a computer program or library whose task is to split the input data stream into separate, unrelated fragments, which are customarily called tokens or lexemes.
> 
> Parser — based on a sequence of tokens, performs syntax analysis, for example builds an abstract syntax tree (AST).
   
## Attempt No. 1 — Flex + Bison

[GitHub - westes/flex: The Fast Lexical Analyzer - scanner generator for lexing in C and C++](https://github.com/westes/flex)
[Bison - GNU Project - Free Software Foundation](https://www.gnu.org/software/bison/)

After reading some clever books, I started with the classics: Flex + Bison. These are old and long-refined applications with the widest configuration capabilities, with which one can describe the syntax and obtain the source files of the lexer and parser for a language with almost any grammar.

Unfortunately, these old-timers have a very high entry threshold and a heavy legacy. The definition of a custom lexer class through `#define` alone is worth something, as well as the lack of proper C++ support. Forced dances with a tambourine for parsing a single line, when one does not need to analyze the whole file, and other not always obvious problems and various unclear conventions.

In other words, after a few weeks of unsuccessful torment, I decided to look at alternatives, and since the initial lexicon file after experiments with Flex + Bison had already been made somehow, the next bundle was Flex + lemon.

## Attempt No. 2 — Flex + Lemon

[The Lemon LALR(1) Parser Generator](https://www.sqlite.org/lemon.html)

Here everything turned out to be primitively simple and understandable. A really very fast start with working examples, a very visual and understandable way of writing rules (compared to Bison). Everything is fine except for one thing: the good ends quickly if one has to analyze more than one line.

In essence, Lemon versus Bison is like Yin and Yang. Lemon is simple and convenient for working with a single line (that is what it was created for), while Bison is a super-duper-mega combine for parsing files of any size.

Therefore the search for a lexer + parser bundle continued, and after reading another article saying that language parsers can be made on regexes, I decided to look at what exists in this direction:

## Attempt No. 3 — a parser on regexes, re2c

From the description of [re2c](https://re2c.org/):
> In essence, with this thing one can write lexical analyzers on the fly in a few minutes.

```c++
    /*!re2c
        re2c:define:YYPEEK       = "*cursor";
        re2c:define:YYSKIP       = "++cursor;";
        re2c:define:YYBACKUP     = "marker = cursor;";
        re2c:define:YYRESTORE    = "cursor = marker;";
        re2c:define:YYBACKUPCTX  = "ctxmarker = cursor;";
        re2c:define:YYRESTORECTX = "cursor = ctxmarker;";
        re2c:define:YYRESTORETAG = "cursor = ${tag};";
        re2c:define:YYLESSTHAN   = "limit - cursor < @@{len}";
        re2c:define:YYSTAGP      = "@@{tag} = cursor;";
        re2c:define:YYSTAGN      = "@@{tag} = NULL;";
        re2c:define:YYSHIFT      = "cursor += @@{shift};";
        re2c:define:YYSHIFTSTAG  = "@@{tag} += @@{shift};";
    */
```

It seemed that this was the most convenient way to specify templates for the parser! It is enough to describe the necessary templates in the comments, run a regex compiler over the source file and everything is ready! But no, indeed, it only seemed so.

After all, for simple parsers or regular expressions **re2c** may indeed be good, but debugging the syntax of a large language on regexes is too troublesome an occupation.

After that I decided not to bother with it and to look at alternatives to the classical lexers and parsers.

## Attempt No. 4 — flex (flexcpp) + bisoncpp

Whereas the traditional flex + bison has C++ support implemented ~~through one place~~ at the "it works, do not touch it" level, I decided to look at their alternative implementation [flexcpp](https://gitlab.com/fbb-git/flexcpp) + [bisoncpp](https://gitlab.com/fbb-git/bisoncpp) with native C++ support.

The first impression was that it was just what the doctor ordered!

The syntax for writing the lexicon, although slightly different from the classical one, is not fundamentally so. But there is native and logical C++ support without wrappers and tricks, and as an additional plus — a more convenient syntax for specifying rules in the parser. But even here there were some rough edges.

In the rule templates, bisoncpp does not support Unicode (although the lexer-parser itself copes with it perfectly), and the situation with support is completely unclear. As I understood, the developer seems to be a single person, but I never managed to communicate with him about errors when processing Russian characters.\*

Then another unclear behavior surfaced in another place. As a result, I decided to abandon the unfriendly support and look at what other parser options exist.

---
\*) Two years later my ticket was closed with a comment that only ASCII characters are supported.

## Attempt No. 5 — the foreign parser ANTLR

I decided to refuse the use of [ANTLR](https://www.antlr.org/) (from English ANother Tool for Language Recognition) — a generator of top-down analyzers for formal languages — right away because it is written in Java.

There are no religious preferences in this, since I was looking for a parser generator that could be made embedded directly into the runtime environment, and in the case of ANTLR and the JRE this would be difficult.

Thus I again returned to the old-timers Flex and Bison, with which everything had begun.


## Attempt No. 6, the last one — returning to Flex + Bison

All the experiments described above took a few months in total, as a result of which kilobytes of source code and test examples were written, dozens of articles were read, and as a result — a return to the original Flex + Bison bundle, but now with a solid baggage of experience in applying various variants of lexers-parsers.

But most importantly, with an understanding of what one ultimately wants to get, and a very large base of test examples of syntax.

## Conclusions for memory

As a result, I decided the following for myself: if a simple templater is needed, then the ideal option is **re2c** (if for some reason **regexp** does not suit). If it is required to analyze syntax more complex than ordinary regexes, but on a single line, then the ideal would be the flex+lemon bundle, and if serious artillery is needed, then here it is unambiguously flex + bison.

I abandoned the flexcpp + bisoncpp bundle altogether. The support situation is unclear, the syntax differs from the classics not very much (although one also has to rack one's brains), and working around the identified flaws is not worth that syntactic sugar.

And based on the results of many experiments with different syntax variants, it was possible to formulate a couple of important architectural points that can greatly simplify the life of programming language creators:

## Strategy for handling syntax errors

Usually it is customary to handle syntax errors directly in the parser, and there is a certain reason for this — in this case there is no need for any further actions; only a fully described correct grammar is needed.

But if the grammar of a language is very complex (hello C++), and its description becomes a difficult task, then one can also abandon the analysis of syntax errors directly in the parser! That is, it is better to make the lexicon as broad as possible (even with those variants that are erroneous for the language), but to catch these errors already during the AST analysis!

In this case, maintaining the description of the language grammar becomes significantly simpler (fewer syntax rules, simpler formal description, etc.), and most importantly, when describing the grammar one does not need to think about lval or rval, where a reference can be specified and where not — i.e. one can specify everything and everywhere, and the analysis of the admissibility of using specific terms will be performed later during the AST analysis.

*Abandoning the full analysis of the language grammar at the level of the lexer-parser and transferring the checking of the correctness of syntactic constructs to the stage of parsing the syntax tree makes it possible to reduce and significantly simplify the description of grammar rules many times over, if not by orders of magnitude!*

Such an assumption is very useful at the initial stage of creating a language (one can focus on the general concept, instead of constant edits to the grammar), and also significantly simplifies the future maintenance and/or extension of the syntax.

## Macros and modification of the grammar at Runtime

However powerful flex+bison may be, this bundle has one architectural problem. The logic of flex and bison is built on finite state machines, and it is impossible to change the grammar of the language during the execution of the application, all the more so because Bison itself calls the lexer to obtain the next portion of data, and it is very difficult to slip modified data to it right during operation. And one so wanted to make it possible to expand macros and modify the syntax in a single pass of the analyzer!

For this, I had to redo the logic of flex+bison so that the parser received data from the lexer not directly from yylex, but through a function — a proxy. This intermediate function puts the read lexemes into an internal buffer. The data in the buffer is analyzed for the presence of macros, and only after their expansion are the lexemes given to the parser from the top of the buffer one at a time. More about TrustLang macros can be read [here](/en/blog/implementation/dsl-with-macros/).

## The most important thing in developing a grammar!

But the most important advice was given to me by a friend who once participated in a project to develop a parser for a programming language. And in the correctness of his advice — **write tests for the grammar** — I have become convinced many times. Even so, **WRITE TESTS FOR THE GRAMMAR**.

Tests for the language grammar are a much more important thing than any of the tools used. Only tests make it possible to make sure that a new feature in the language has not broken the old work. And if it did break it, then first of all one needs to add a new test that would pin down the broken scenario, and only after that can one calmly continue experiments with new grammatical constructs.

And good luck to all language writers!


Original publication

