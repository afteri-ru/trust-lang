---
title: Macros
weight: 50
---

Macros are also used to transform the original code of *TrustLang* into a more familiar syntax based on [keywords](/en/docs/syntax/dsl/), 
as such text is much easier to understand when reading the original code later.

In **TrustLang**, macros are one or more consecutive terms that are replaced with another term or a whole syntactic construction (a sequence of lexemes).

Macros are processed during the operation of the **lexer**, i.e., before passing the sequence of lexemes to the parser, 
allowing fragments of the language syntax to be modified using macros, for example, when implementing custom DSL dialects.

### Defining Macros

The definition of macros is similar to the [definition](/en/docs/ops/create/) of other objects and consists of three parts 
**<macro name> <creation/assignment operator> <macro body>** and ends with a semicolon "**;**", i.e., 
normal operators **::=**(**::-**), **=**, or **:=**(**:-**) are used to create a new or redefine an existing object, 
and the macro name is indicated between two symbols **"@@"** and can contain one or more lexemes (terms).

All macros belong to the global namespace, so the first term in the macro name must be unique, 
otherwise it will override local and global variables during [name lookup](/en/docs/syntax/naming/#name-lookup)
if they are written in the program text without [qualifiers (sigils)](/en/docs/syntax/naming/#sigil).

Using the operators **::-** and **:-** creates pure (hygienic) macros, arguments and variables in which are guaranteed not to intersect with the program's namespace.

The body of a macro can be a valid language expression, a sequence of lexemes (enclosed in double at symbols **"@@"**, 
i.e. **`@@ lexeme1 lexeme2 @@`**), or a regular text string (which should be specified between triple at symbols **"@@@"**, 
i.e. **`@@@ text string @@@@`**).

In the macro name after the first term, one or more templates may be present. 
A *template* is a term that, when matching a sequence of lexemes with the macro identifier, 
can be replaced by any other single term (effectively, this is pattern/template matching).

To create a template term, a dollar sign should be placed at the beginning of its identifier (which corresponds to a qualifier of a local variable), 
i.e. the macro name `@@ FUNC $name @@` will correspond to the sequence of lexemes as `FUNC my_func_name` as well as `FUNC other_name_func`.

To remove a macro, a special syntax is used: `@@ name @@@@;` or `@@ two terms @@@@;`, 
i.e. the macro name is given after the opening `@@`, and the removal is finished by the universal terminator `@@@@`.

```python
    # Macro body from a text string (as in C/C++ preprocessor)
    @@ macro_str @@@ string - macro body @@@@; # String for the lexer

    # Removing macro @macro_str
    @@ macro_str @@@@;
```

### Macro Arguments and Expansion {#args}

Macros can be defined with arguments (parameters in parentheses) or without them. 
If a macro was defined with arguments, their validation will be performed by the macro processor during definition and expansion of the macro. 
If a macro was defined without arguments, the presence of arguments will be ignored by the macro processor.

The **first term of a macro name is the key of a macro *group***: in one group there can be **many
macros with the same first term but different arity** (different number/composition of additional
terms), e.g. `break`, `break $label`, `break $a $b`. Such macros coexist and do not conflict.

At expansion, among the macros of the group the **longest (most specific) match** is chosen -
the one that consumes the most terms of the input buffer. The "duplication" diagnostic is emitted
**only when the full signature (all terms) matches**, not when only the first name coincides.
Different arities of the same group are not duplicates.

Macros with and without call arguments (parentheses) are also different signatures: a call form and
a non-call form with the same first term can coexist (the non-call form matches a call too, consuming
only the first term; the call form matches the full call).

```bash
    @@ macro @@ := term; # Macro without arguments
    @@ macro $value @@ := term(@$value); # Macro with one extra template term (different arity)

    macro;        # OK -> term;          (arity-1 form)
    macro 42;     # OK -> term(42);      (arity-2 form, longest match)

    # But
    @@ call() @@ := term(); 

    call(); # OK -> term();
    call;   # OK -> term;  (the non-call form `call` also exists if defined)
```

If arguments are specified when defining a macro, the place for their insertion in the body of the macro is written 
as the name of a local variable with the symbol **"@"** added before it, i.e. **@$arg**.

The place for inserting the number of actual arguments passed is marked by the lexeme **"@$#"**.
If it is necessary to insert the passed arguments as a dictionary, the place for insertion is marked by the lexeme **"@$\*"**.

If the macro takes an arbitrary number of arguments (the macro arguments are terminated by an ellipsis), 
the place for their insertion in the body of the macro is marked by the lexeme **"@$..."**.

Analogous to the C/C++ preprocessor, to concatenate two lexemes into one, the operator **"@##"** is used in the body of the macro, 
and to convert a lexeme into a text string, the operators **@#**, **@#"**, or **@#'** are applied, for example, 
`@@macro($arg)@@ := @@ func_ @## @$arg( @#" arg ) @;`, then the call `macro(name);` will be transformed into `func_name ("name");`

Examples of using macros:
```python
    # Ordinary macros (the body of the macro is a correct expression)
    @@ macro @@        := replace();
    @@ macro2(arg) @@  := { call(@$arg); call()};

    # The body of the macros from a sequence of tokens
    @@ if(...) @@    := @@ [ @$... ]--> @@; # The expression may not be complete
    @@ elif(...) @@  := @@ ,[ @$... ]--> @@;
    @@ else @@       := @@ ,[...]--> @@;
 
    # Writing a conditional statement using
    # the macros defined above
    @if( condition ){
        ...
    } @elif( condition2 ) {
        ...
    } @else {
        ...
    };
```

For example, a loop up to 5:
```python
    count := 1;
    [ 1 ] <-> {
        [ count > 5 ] --> {
            -- 42 --;
        };
        count+=1;
    };
```

This will look more familiar:
```python
    count := 1;
    @while( true ) {
        @if( count > 5 ) {
            @return 42;
        };
        count += 1;
    };
```