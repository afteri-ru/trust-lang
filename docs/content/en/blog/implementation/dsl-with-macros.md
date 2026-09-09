---
slug: dsl-with-macros
title: DSL (domain-specific language) implementation with macros
date: 2023-03-01
tags: [dsl, macros, syntax, parser, language-design]
---


![Featured image](/en/blog/implementation/dsl.jpeg)

{{% pageinfo %}}

Attention!!!

This article contains a description of the syntax of the previous version of NewLang.

The current version of the language syntax can be viewed [here](/en/docs/).

{{% /pageinfo %}}


The release of the **[TrustLang](/en/)** language is approaching with a fundamentally new "feature", a reworked version of the preprocessor that makes it possible to extend the syntax of the language to create various DSL dialects through macros.

And, as always, using the previously found lifehack Habr — a chamber of wisdom, I would like to get feedback from readers about the approach proposed below, which is planned to be implemented in the new **TrustLang** preprocessor.

### What is this about?
> [DSL](https://en.wikipedia.org/wiki/Domain-specific_language) (Domain-specific language) is a programming language specialized for a specific area of application. It is believed that the use of DSL significantly raises the level of abstraction of the code, and this makes it possible to develop more quickly and efficiently and significantly simplifies the solution of many tasks.

#### Conventionally, two approaches to implementing DSL can be distinguished:
- Development of independent syntax translators with the help of lexer and parser generators for defining the grammar of the target language by means of BNF and regular expressions (Lex, Yacc, ANTLR, etc.) and the subsequent compilation of the obtained grammar into machine code.
- Development or embedding of a DSL dialect on a general-purpose language (metalanguage), including through the use of various libraries or special parsers / preprocessors.

Further we will talk about the second variant, namely, about the implementation of DSL on the basis of general-purpose languages (metalanguages) and the new variant of implementing macros in **TrustLang** as a basis for developing DSL.


## Two extremes
It probably makes sense to start with a description of the two extremes in implementing DSL on the basis of a general-purpose language (metalanguage):

### Limited grammar
If a programming language is limited by its own fixed grammar and does not allow its extension,
then when implementing DSL the developer will be forced to use the already existing grammar, and the rules for writing operations and indeed the whole syntax will remain the same as in the implementation language. For example, when using C/C++ as the base language or applying various libraries and frameworks in other general-purpose programming languages.

*In this case, the term "DSL" will hide simply a set of specific terms of the domain area, redefined macros and/or operators, but their use will be limited by the grammar of the implementation language.*

### Unlimited grammar
If, however, the language (metalanguage) allows modifying its own grammar (for example, at the level of the [AST](https://en.wikipedia.org/wiki/Abstract_syntax_tree)), then DSL will no longer be rigidly limited by the syntax of the base programming language, and as a result its grammar can be anything. Up to the point that "for each new project one will have to learn a new language…".
This can be done with the help of specialized metalanguages (Lisp, ML, Haskell, Nemerle, Forth, Tcl, Rebol, etc.)

I very much recommend reading the excellent article by @NeoCode about metaprogramming: Metaprogramming: what it is and what it should be.


## The following implementation of macros is proposed for discussion
"There is no perfection in the world", and after the release of **TrustLang 0.2** I received a lot of feedback (mostly negative)
about the first variant of [implementing macros](https://github.com/afteri-ru/trust-lang/blob/v0.3.0/docs/syntax.md#%D0%BC%D0%B0%D0%BA%D1%80%D0%BE%D1%81%D1%8B) and [DSL based on them](https://github.com/afteri-ru/trust-lang/blob/v0.3.0/docs/syntax_dsl.md). And if we put our hand on our heart, this criticism was often justified.
Therefore I decided to try to rework the macros a bit, in the hope of obtaining a "golden mean" between the two extremes described above when describing DSL.

### Terminology used
Macros in *TrustLang* are one or several terms that are replaced by another term or by a whole syntactic construct (a sequence of lexemes).
Macros are at the same time both an extension of the base syntax of the language, when implementing one's own DSL dialects, and syntactic sugar.

The main feature of macros is that they make it possible to change expressions even before they are evaluated at runtime.
The expansion of macros occurs during the operation of the **lexer**, which makes it possible to substitute them for any other terms and even to modify the very syntax of the language.

Therefore, if no modifier is specified before the name of a **TrustLang** object (**\**macro, **$**local_variable or **@**module),
then first the object will be searched among macros, then among local variables and last among modules (module objects).
Due to this, one can use terms without mandatory modifiers to indicate specific types of objects.

#### Defining macros
The exact same [syntax](/en/docs/operators/create/) as for other language objects is used to define macros
(the operators "**::=**", "**=**" or "**:=**" are used, respectively, to create a new object, to assign a new value to an already existing one, or to create an object / assign a new value to an object regardless of its presence or absence).

In general form, a macro definition consists of three parts **<**macro name**>** **<**creation/assignment operator**>** **<**macro body**>** and a terminating semicolon "**;**".

#### Macro body
The body of a macro can be a valid language expression, a sequence of lexemes (which are enclosed in double backslashes, i.e. **\\\\**lexeme1 lexeme2**\\\\**) or an ordinary text string (framed in triple backslashes, i.e. **\\\\\\** text string **\\\\\\**).

For joining two lexemes into one (an analog of the ## operation in the C/C++ preprocessor), the syntax **\##** is used by analogy. A similar operator is used to wrap a lexeme in quotes **\#**, for example, `\macro($arg)  := \\ func_ \## \#arg(\#arg) \\;`? then the call macro(arg) will be transformed into `func_arg ("arg")`;

#### Macro name
The name of a macro can be a single identifier with the macro prefix "**\**" or a sequence of several lexemes. If a sequence of lexemes is used as the macro name, then among them there must be at least one identifier and there may be one or several patterns.

A pattern is a special identifier that, when matching, can be replaced by any single term. With the help of patterns, a search by pattern is performed and the given sequences of lexemes are replaced by the macro body.

To specify a pattern at the beginning of an identifier, a dollar sign must be placed (which corresponds to writing the name of a local variable), i.e. **\\\\**one_lexeme**\\\\**, **\\\\**three whole lexemes**\\\\** **\\\\**lexeme *$pattern1* *$pattern2* **\\\\**.

Macros are considered identical if their identifiers are equal, the number of elements in their names coincides, and the identifiers and patterns are located in the very same places.

#### Macro arguments
Terms or patterns in a macro name can have arguments, which are specified in parentheses. The passed arguments in the macro body are written in the place for expansion as the name of a local variable, but a backslash must be added before the name, i.e. `\$name`.

An arbitrary number of parameters of a macro is marked with an ellipsis "...", and the place for inserting these arguments is marked by the lexeme **\$...**. If a macro has several identifiers with arguments, then to insert arguments from a specific identifier a lexeme is used with the indication of the required identifier, for example, **\$name...**.

To insert the number of actually passed arguments, the lexeme **\$#** is used, or with the indication of the required identifier, for example, **\$#name**.

*Macros work with lexemes that contain various information, including about the data type, if it is specified. But at the current moment the data types in macro arguments are not processed in any way, and this is one of the mandatory features that will be implemented in the future.*

### Examples:
```
   \macro1 := 123;
   \macro2(arg) := {func( \$arg ); func2(123);};
   \\macro of(...) lexemes\\ := \\ call1(); call2( \$... ); call3() \\;
   \txt_macro := \\\ string for the lexer \\\;

    # Ordinary macros (the macro body is a valid expression)
    \macro      := replace();
    \macro2($arg)   := { call( \$arg ); call()};
    # The number of arguments and the arguments themselves are passed to the function
    \\func name1(...)\\  := name2( \$#, \$name1... ); 

    # Macro bodies from a sequence of lexemes
    \if(...)    := \\ [ \$... ] --> \\; # The expression may be incomplete
    \else       := \\ ,[ _ ] --> \\; # The expression may be incomplete
 
    # Macro body from a text string (as in the C/C++ preprocessor)
    \macro_str  := \\\ string - macro body \\\; # A string for the lexer
    \macro($arg)  := \\\ func_ \## \#arg(\#arg)\\\; # macro(arg) -> func_arg ("arg")
```

## What capabilities does this give?
Thus one can define macros in the following combinations:
```
No.          Macro name                      Macro body
----------------------------------------------------------------
   1.       \identifier                     expression
   2.       \identifier             \\lexeme1 lexeme2\\
   3.       \identifier             \\\string for the lexer\\\
   4.   \\lexeme1 lexeme2\\                expression
   5.   \\lexeme1 lexeme2\\        \\lexeme1 lexeme2\\
   6.   \\lexeme1 lexeme2\\        \\\string for the lexer\\\
```
Each of the combinations listed above has its own properties and limitations:  
1. The classical replacement of one term by another term or a whole expression. It is processed once by the lexer and parser at definition time. The expression in the macro body must be correct from the point of view of syntax, and if there are errors in it, a message about this is formed immediately, already at the definition of the macro.

2. The classical replacement of one term by a sequence of lexemes, including incomplete syntactic constructs. It is processed once by the lexer at the definition of the macro. The macro body is analyzed by the parser when it is used, therefore possible syntax errors will be noticed only when the macro is expanded.

3. The classical replacement of one term by a text string that is fed to the input of the lexer. Only the macro name is processed once by the lexer at its definition, which makes it possible to modify the macro body and to change/combine/modify lexemes before they are fed to the analyzer. Syntax errors will be noticed only when the macro is expanded.

4, 5 and 6. Replacing a sequence of several lexemes (patterns) by an expression, a sequence of lexemes or a text string, respectively.

## Purpose and examples of use
Macros are also used to transform the base syntax of *TrustLang* into a more familiar keyword-based syntax, since such text is much easier to perceive when subsequently reading the source code.

If no modifier is specified before the name of a TrustLang object (**\**macro, **$**local_variable or **@**module), then first the macro name is searched, then the local variable name and last the module name (module object). Due to this it is possible to define the syntax of DSL in the familiar notation without mandatory prefixes for different types of objects.

For example, the notation of a conditional statement in the basic **TrustLang** syntax:
```
    [condition] --> {
        ...
    } [ condition2 ] --> {
        ...
    } [ _ ] {
        ...
    };

# With the help of macros
    \if(...)    := \\ [ \$... ]--> \\;
    \elif(...)  := \\ ,[ \$... ]--> \\;
    \else       := \\ ,[ _ ]--> \\;

# Turns into the classical notation
    if( condition ){
        ...
    } elif( condition2 ) {
        ...
    } else {
        ...
    };
```

Or a loop up to 5:
```
count:=1;
[ 1 ] <-> {
    [count>5] --> {
        ++ 42 ++;
    };
    count+=1;
};
```

will look more familiar using the corresponding macros:
```
\while(...) := \\ [ \$... ] <-> \\;
\return(...) := ++ \$... ++;
\true := 1;

count := 1;
while( true ) {
    if( count > 5 ) {
        return 42;
    };
    count += 1;
};
```

### Deleting macros
To delete a macro, you must assign to it an empty sequence of lexemes `\macro_str  := \\\\;`. Also, for deletion you can use a special syntax: `\\\\ name \\\\;` or `\\\\ \\ two terms \\ \\\\;`, i.e. specify the macro name between four backslashes.

The necessity of using a separate syntactic construct for deleting macros is caused by the fact that macro names are processed by the lexer even before the parsing stage.

## What is the profit?
1.  The base syntax of the language can be diluted with additional keywords and turned into the familiar "keyword-based" one.
2. The definition of macros corresponds to the lexicon of the language, and the macros themselves are processed as ordinary objects.
3. Simplicity of analyzing the source code and of debugging it.
4. The use of DSL terms and metaprogramming techniques can be made explicit, for example, always specifying a prefix before the macro name. In this case the compiler will unambiguously know that a macro expansion is required.
5. Despite the fact that the syntax of the language can be significantly modified at one's own risk, this can only be done within certain restrictions (the AST cannot be modified directly), which does not allow one to go too far and, for example, crash or hang the compiler.
6. Despite the very large capabilities for modifying the syntax, a very simple, fast and unambiguous implementation is obtained. And this has a positive effect on the speed of analyzing the sources, detecting and handling possible errors, and at the same time is a reasonable compromise between the complexity of implementing this functionality and the capabilities of defining one's own DSL dialects.
7. If desired, there is room to develop the capabilities of metaprogramming. In the future one can add pattern matching (for example, based on regular expressions), make the string parameterizable for generating syntax in the macro body, including at runtime, and many other various ways to elegantly shoot yourself in the foot or the foot of your comrade.

## Conclusion
I will be grateful for any feedback on this implementation of macros. And twice grateful if, besides criticism, suggestions for its improvement and refinement are also expressed, in case some point was missed.

Original publication

