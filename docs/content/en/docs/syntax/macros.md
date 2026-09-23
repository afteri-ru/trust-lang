---
title: Macros and DSL
tags: [syntax, macros]
weight: 30
---

Macros in **TrustLang** are elements of metaprogramming.

A macro is one or several consecutive terms that are replaced by another term
or by a whole syntactic construct (a sequence of lexemes) and
act as specialized regular expressions for searching and replacing sequences of terms.

Macros are processed by the **lexer** before sending tokens to the parser for syntax analysis,
which makes it possible to change even the syntax of the language, for example, to turn the *TrustLang* syntax based on grammar rules
into the more familiar keyword-based syntax,
since such text is easier to perceive when subsequently reading the program source code.

### Defining macros

Defining macros is similar to the [definition](/en/docs/operators/create/) of other objects and consists of three parts
**<macro name> <creation/assignment operator> <macro body>** and ends with the semicolon "**;**",
i.e. the usual operators **:=** or **=** are used to create a new or redefine an existing object,
and the macro name is specified between two characters "**@@**" and may contain one or several lexemes (terms).

All macros belong to the global namespace, therefore the first term in the macro name must be unique,
otherwise it will shadow local and global variables during [name lookup](/en/docs/syntax/naming/),
if they are written in the program text without [qualifiers (sigils)](/en/docs/syntax/naming/#sigil).

The body of a macro can be a valid language expression, a sequence of lexemes (which is enclosed in double at-signs "**@@**",
i.e. **`@@ lexeme1 lexeme2 @@`**) or an ordinary text string (which must be specified between triple at-signs "**@@@**", i.e. **`@@@ text string @@@@`**).

In the macro name, after the first term, there may be one or several patterns.
A *pattern* is a term that, when matching a sequence of lexemes with the macro identifier,
can be replaced by any other single term (i.e. in fact this is matching by a given template pattern).

To create a pattern term, you must put a dollar sign at the beginning of its identifier (which corresponds to the qualifier of a local variable),
i.e. the macro name `@@ FUNC $name @@` will match both the sequence of lexemes `FUNC my_func_name` and `FUNC other_name_func`.

To delete a macro, a special syntax is used: `@@ name @@@@;` or `@@ two terms @@@@;`,
i.e. the macro name is specified after the opening `@@`, and the deletion is finished by the universal terminator `@@@@`.

```bash
    # Macro body from a text string (as in the C/C++ preprocessor)
    @@ macro_str @@@ string - macro body @@@@; # A string for the lexer

    # Deleting the macro @macro_str
    @@ macro_str @@@@;
```

### Macro arguments and their expansion {#args}

Macros can be defined both with arguments (parameters in parentheses) and without them.
If a macro was defined with arguments, their checking will be performed by the macro processor when defining and expanding the macro.
If a macro was defined without arguments, their presence is ignored by the macro processor.

**The first term of the macro name is the key of a *group* of macros**: one group may contain **many
macros with the same first name but different arity** (a different number/composition of additional
terms), for example `break`, `break $label`, `break $a $b`. Such macros coexist and do not conflict.

When expanding, the **longest (most specific)** macro is selected from the group — the one that
consumes the most terms of the input buffer. The "duplication" diagnostic is issued **only when the signature (all terms) fully
coincides**, and not when only the first name coincides. Different arities of
one group are not duplicates.

Macros with arguments in parentheses and without them are also different signatures: the call-form and the bracketless form
with the same first name can coexist (the bracketless form is also matched to a call, consuming
only the first term; the call-form — the whole call).

```bash
    @@ macro @@ := term; # Macro without arguments
    @@ macro $value @@ := term(@$value); # Macro with an additional pattern term (different arity)

    macro;        # OK -> term;          (the arity-1 form)
    macro 42;     # OK -> term(42);      (the arity-2 form, longest-match)

    # But 
    @@ call() @@ := term(); 

    call(); # OK -> term();
    call;   # OK -> term;  (the bracketless form `call`, if it is defined)
```

If arguments are specified when defining a macro, the place for inserting them in the macro body
is written as the name of a local variable preceded by the character "**@**", i.e. **@$arg**.

The place for inserting the number of actually passed arguments is marked by the lexeme "**@$#**".
If the passed arguments need to be inserted as a dictionary, the place for insertion is marked by the lexeme "**@$\***".

If a macro accepts an arbitrary number of arguments (the macro arguments end with an ellipsis),
then the place for inserting them into the macro body is marked by the lexeme "**@$...**".

By analogy with the C/C++ preprocessor, to join two lexemes into one in the macro body, the operator "**@##**" is used,
and to convert a lexeme into a text string, the operators **@#**, **@#"** or **@#'** are used, for example
`@@macro($arg)@@ := @@ func_ @## @$arg( @#" arg ) @;`, then the call `macro(name);` will be transformed into `func_name ("name");`

Examples of using macros:
```python
    # Ordinary macros (the macro body is a valid expression)
    @@ macro @@        := replace();
    @@ macro2(arg) @@  := { call(@$arg); call()};

    # Macro bodies from a sequence of lexemes
    @@ if(...) @@    := @@ [ @$... ]--> @@; # The expression may be incomplete
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

For example, the same logic "loop up to 5" — in two forms:

<table>
<tr><th>DSL macros</th><th>Base syntax</th></tr>
<tr><td><pre><code>count := 1;
@while( true ) {
    @if( count &gt; 5 ) {
        @return 42;
    };
    count += 1;
};</code></pre></td><td><pre><code>count := 1;
[ 1 ] &lt;-&gt; {
    [ count &gt; 5 ] --&gt; {
        -- 42 --;
    };
    count += 1;
};
</code></pre></td></tr>
</table>

### Compiler debug output (`@__DEBUG__`, `@__DEBUG_SCOPE__`)

Built-in system macros for debugging **the compiler itself** (they work only in a debug build
of the compiler; in a release build the call produces no output and is accompanied by a warning). Both macros
do not generate code — the analyzer applies the effect and removes the marker.

- `@__DEBUG__(<masks>)` — controls the **filter** of messages that the compiler creates through
  `TRUST_DEBUG`; `@__DEBUG__()` — disable. The application of the filter is **duplicated in the output itself** as an echo
  (`@__DEBUG__: debug messages ENABLED, filter='…'` / `DISABLED (empty filter)`) — so it is visible that the filtering
  parameters were applied/changed/disabled. The echo and the filter do not affect the state dump.
- `@__DEBUG_SCOPE__([<name masks>...] [, key=value...])` — prints the **current state of the
  analyzer** at this point (regardless of the `@__DEBUG__` filter). Positional arguments — only
  a **name filter** (masks comma-separated); an **empty call `@__DEBUG_SCOPE__()` prints the full dump**.
  Named options (values without quotes): `level=current|all`, `types=on|off`, `max=<N>`, `count=only`.

**Prefixes of output lines** (each line): for `TRUST_DEBUG` messages — `.../file:line` of the **call
site in the compiler**; for the output of `@__DEBUG__`/`@__DEBUG_SCOPE__` — `.../file:line` of **the
macro itself** in the `.src`. Directories in the path are replaced with an ellipsis (`.../file.src:12: `).

Named options are checked **when the macro is parsed**: for an unknown name/value, a
diagnostic is issued with the full list of supported options and allowed values.

Filtering of messages is mandatory: without a specified `@__DEBUG__` filter, `TRUST_DEBUG` messages are not
printed. You can filter by a keyword, by a component, by a file and by a compiler path.

```trust
@__DEBUG__("scope,resolve");        # enable TRUST_DEBUG messages by these keywords
x := 1;
@__DEBUG_SCOPE__();                                        # full dump of the analyzer state
@__DEBUG_SCOPE__("x*", level=all, types=on, max=10);       # with a name filter and options
@__DEBUG__();                       # disable messages (does not affect the dump)
```
Output (a fragment): `prog.src:1: @__DEBUG__: debug messages ENABLED, filter='scope,resolve'`,
`src/semantic/name_resolution.cpp:129: enter FuncDecl depth=3`,
`prog.src:3: scope: depth=2 names=1`, `prog.src:3:   [1] ModuleDecl \`prog.src\`: x`.

### Keyword syntax (DSL): the familiar keyword-based syntax

### Peculiarities of associative memory
The *TrustLang* syntax is based on strict rules without using keywords,
and however logical it may look, association by keywords is recalled much more easily, for example **if**,
than the combination *minus minus right angle bracket* **-->**.
Because of this, it makes sense to use not the "pure" base syntax, but a more familiar dialect using keywords.

*TrustLang* already contains a set of macros that extend the rule-based base syntax of *TrustLang*
with a set of predefined keywords, as in classic programming languages,
which can be adapted or supplemented for your own domain.


## Mnemonic commands (`@func`)

To avoid having to remember grammar rules and special characters, some DSL macros are designed as
**mnemonic commands** — macro commands with a convenient name instead of combinations of special characters.

The `@func` command — defining a function without needing to remember the operator `:=`:

```trust
@func myfn ( a:Int32 ) { @print('{}', a); };
@func add ( a:Int32, b:Int32 ): Int32 { @return a + b; };
# → void myfn(int32_t a);  int32_t add(int32_t a, int32_t b);
```

- `@func <name> ( <arguments> ) { <body> }` — a function definition (Void). It expands into
  `<name>( <arguments> ) := { <body> }`.
- `@func <name> ( <arguments> ): <type> { <body> }` — a function definition with a return type.
  It expands into `<name>( <arguments> ): <type> := { <body> }`. `<type>` can be composite
  (for example `Tuple(Int32, Int32)`).

> **Note:** the name `func` is reserved by DSL (like `main`, `module`, `return`, `if`, etc.),
> therefore do not use it as the name of a function/variable.

### Constants
- *@true* — 1:Bool
- *@yes* — 1:Bool
- *@false* — 0:Bool
- *@no* — 0:Bool

### Operators
- *@if(...)* — The first conditional operator
- *@elif(...)* — The second and all subsequent conditional operators
- *@else* — The *otherwise* operator

- *@while(...)* — The precondition loop operator
- *@dowhile(...)* — The postcondition loop operator
- *@loop* — The infinite loop operator

- *@break [label]* — Exit from the nearest loop or from the named block `label` (one identifier, without `::`). Without a label: `@break;`
- *@continue [label]* — Jump to the beginning of the nearest loop or the named block `label` (one identifier, without `::`). Without a label: `@continue;`
- *@return [value]* — Exit from the current function (an analog of break by the function name `@__FUNCTION__`). Void form: `@return;`. With a value: `@return <value>;` (one rvalue: a name, literal, string, call or dictionary `(a, b,)` in parentheses)

- *@match( ... )* — The expression evaluation operator
- *@case( ... )* — The pattern comparison operator
- *@default* — The default selection operator

## Built-in functions and checks

- *@assert( cond )* — Evaluate the expression and check it for truth at runtime
- *@assert( cond, 'fmt', args... )* — A check with a message: on failure, instead of the condition text, `std::format(fmt, args...)` is output (the format string is a narrow literal `'...'`)
- *@verify( cond )* — Evaluate the expression and check it for truth at runtime; unlike `@assert`, the condition is evaluated even when checks are disabled
- *@verify( cond, 'fmt', args... )* — The same, but with a format message on failure

*If you run the compiler with the flag `-Wno-assert`, then the runtime checks of `@assert` are removed from the program text,
while the computations inside `@verify` are performed, but their result is ignored.*

<a id="check-area"></a>

## Predefined macros

During the operation of the *TrustLang* parser, several reserved macros are automatically formed,
some of which correspond to the C/C++ preprocessor macros.
These predefined macros can be used as ordinary constants.

