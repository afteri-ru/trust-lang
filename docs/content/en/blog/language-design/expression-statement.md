---
title: "If you think you know how \"Expression\" differs from \"Statement\", then most likely you are mistaken"
slug: expression-statement
date: 2026-08-02
tags: [semantics, syntax, programming-languages, parser]
---


I study the [syntax of programming languages](/en/blog/comparison/cpp-rust-vs-python/) and try to separate fundamental regularities from historical accidents. One of such accidents is the almost ubiquitous use of the terms **expression** and **statement** when describing and classifying the syntax of programming languages.

Intuitively it seems that `2 + 2` and `if (x) { foo(); }` are completely different entities, but on a deeper analysis it turns out that such a separation is artificial and arose from the architectural features of computing machines almost half a century ago and since then simply "carries over" from language to language.

## Part 1. Historical

To understand where the division of syntax into **expressions** and **statements** came from, one needs to trace the evolution of syntactic rules from the first high-level languages.

### FORTRAN -> ALGOL 60 -> C: three acts of one play

- **FORTRAN (1957) — separation "by hardware".**
  In the language created for the IBM 704, expressions (`X + Y*Z`) were strictly separated from control statements (`IF`, `DO`, `GOTO`). The former computed values, the latter controlled the flow of execution, and mixing them was impossible. This concept of syntax directly reflected the architecture of the computing machines of that time: a program was understood as a sequence of machine instructions, each of which either computed an operand or changed the instruction counter, but not both at once.

- **ALGOL 60 (1960)**.
  In ALGOL 60 the compound statement `begin ... end` (the prototype of a block) was introduced, but something else is more important: the language **allowed** a conditional expression. The construct `if B then E1 else E2` could appear in the position of any expression, and therefore allowed writing

  ```
  x := if a > b then a else b
  ```

  This was the direct predecessor of the ternary operator, showing that a choice between two values fits perfectly into the role of an expression.

- **C (1972) — fixing the Expression vs Statement distinction**.
  Dennis Ritchie (with the participation of Ken Thompson, the author of the predecessor language B), designing C as a "portable assembler" for the PDP-11, fixed in the grammar a clear distinction between these notions. Such a decision was dictated by the striving for the most direct mapping of the language's constructs onto machine instructions: the conditional jump ~~jz~~ `BEQ` of the processor is an action, not a way to compute a value.

### Lisp and the parallel universe without statements

Almost synchronously with FORTRAN, in 1958, John McCarthy created Lisp, fundamentally based on the lambda calculus. Here there is **no notion of statement** — the whole program consists of S-expressions: whether it is a function call, a conditional construct or a block.

In Lisp the construct `(if (> a b) a b)` is simply an expression whose value can be assigned, passed to a function or used in a more complex composition. And the block `(let ((x 5)) (+ x 1))` returns 6, remaining an ordinary expression.

This is not an "extension" of capabilities, but a fundamentally different principle: the whole program is computable values, and not a sequence of control instructions. From this position the division into `expression` and `statement` looks unnecessary.

## Part 2. Modern languages

It is interesting to see how deeply in modern languages the boundary between these terms has taken root, and how it is overcome.

### The classical model: C, C++, Java (and Python)

In these languages `if`, `for`, `while` categorically cannot be part of an expression. The code

```cpp
int y = if (x > 0) { 1; } else { 2; }   // compilation error
```

is inadmissible. Developers are forced to either use the ternary operator:

```cpp
int y = (x > 0) ? 1 : 2;
```

or (in C/C++) resort to non-standard extensions like GCC statement expressions:

```cpp
int y = ({ if (x > 0) 1; else 2; });    // only with GCC extensions
```

In Python the situation is analogous: `if` is a statement, but starting from version 2.5 a ternary expression was introduced:

```python
y = 1 if x > 0 else 2
```

This construct is an exact analog of the ternary operator `?:`, a "patch" for a grammar that did not dare to make `if`/`else` a full-fledged expression.

### JavaScript: emulating an expression through a function

JavaScript formally preserves the C-like separation, however workaround tools are built into the language. Using an IIFE (immediately invoked function expression), a developer can "wrap" statement-logic in a context where a value is required:

```javascript
let y = (function() {
    if (x > 0) return 1;
    else return 2;
})();
```

Before the advent of modern JIT optimizations, such a trick carried real overhead for a function call, but it clearly demonstrates the manual emulation of expression grammar on top of statement. It is telling that, if the community goes to such tricks, then the need for such an approach is great.

### Ruby: everything is an expression

In Ruby any construct is an expression, without exceptions. One can write:

```ruby
y = if x > 0
  1
else
  2
end

z = case value
    when 1 then "one"
    when 2 then "two"
    else "other"
    end

w = while false
  # the body is not executed
end  # w equals nil, but while itself is a valid expression
```

Even the definition of a class or method returns the value of the last expression. The Ruby grammar does not introduce a separate nonterminal "statement" — the whole program consists only of expressions.

### Rust: a hybrid — neither ours nor yours

Rust offers a peculiar compromise: formally the grammar has a distinction between `statement` and `expression` (`let` declarations, item declarations are not expressions), but the control constructs (`if`, `match`, `loop`) are entirely assigned to the Expression category — unlike C/C++, where this is fundamentally impossible.

At the same time Rust has quite a few confusing rules around constructs that seemingly are `expression`, but behave strangely depending on the context. For example:

```rust
let x = while true { break 5; };  // ERROR! while cannot do that
```

whereas the very similar `loop` can:

```rust
let x = loop { break 5; };  // OK! x == 5
```

The difference between `loop` and `while`/`for` is that the compiler cannot guarantee that the body of `while`/`for` will be executed at least once (the condition may be false from the very beginning), whereas `loop` is either infinite or exits through `break` with a concrete value. This asymmetry of behavior is purely technical, but it fundamentally affects the syntax of these constructs.

Or here is: `if` with parentheses and without them behaves unexpectedly:

```rust
fn f() -> i32 {
    if x > 0 { 1 } else { 2 } - 1
}
```

It seems that this should compute `(if...) - 1`. **But no!** The compiler treats `if {...} else {...}` as a separate `statement` (because it is not in the "tail" position of the block), and `- 1` as a separate expression — unary minus. The function will return a type error, because the last line turns out to be `-1`, and the value of the if-expression will simply be discarded.

To get the expected behavior, one needs to explicitly wrap the `if` in parentheses:

```rust
let y = (if x > 0 { 1 } else { 2 }) - 1;  // this works as needed
```

But the strangest thing is the behavior of the semicolon, which can change the type of a function:

```rust
fn f() -> i32 {
    5 + 3;   // with a semicolon!
    10
}
```

But if you put `;` after the last line:

```rust
fn f() -> i32 {
    5 + 3;
    10;      // now there is ; here
}
// ERROR: the function must return i32, but returned ()
```

`;` (the semicolon) in Rust is not simply an "end of line", but an *operator* that literally changes the type of an expression to `()` (the empty type). A forgotten or extra semicolon is the most frequent cause of non-obvious errors among beginners.

Rust really did move further than C in the direction of "everything is an expression", but this is not a single principle, but a set of point, sometimes mutually contradictory rules, each of which solves a concrete engineering task (type safety, termination guarantees, parsing ambiguity resolution), and not a consequence of one beautiful idea consistently applied everywhere.


## Part 3. What actually distinguishes a Statement from an Expression?

If we return to the original question, so how does a `statement` differ from an `expression`?

### Rejected criteria

1. ~~"Statement is execution, Expression is computation"~~ — both terms are connected with computational actions; the difference is not in the nature of the operation.
2. ~~"Statement does not return a value"~~ — refuted by Ruby, where everything returns a value.
3. ~~"Statement is a separate category of the grammar"~~ — this is simply a description of the symptom (of what is fixed at the BNF level in a specific language), and not an explanation of the fundamental differences between the terms.

A more productive thing turns out to be the view of the statement/expression pair as a structural composition in which:

- **Expression** is an indivisible lexical unit: a literal, an identifier, a function call, a mathematical operation.
- The **semicolon** `;` (or its analogs — a newline in Ruby) plays the role of a *separator operator*, which tells the compiler: the expression on the left must be evaluated, its value discarded, and one must move on to the right part. A chain of several expressions sequentially joined by `;` forms a statement.
- A **compound block** bounded by curly braces `{...}` is a way to turn a *chain* of several expressions into a single indivisible expression. The value of the block becomes the value of the last expression in it.

Thus, **statement** is not an alternative category of syntax, but a *composition of one or several expressions joined by the `;` operator*, where the value of the whole chain equals the value of the last element. The notation:

```
expression1;
expression2;
...
expressionN
```

can be read as `(expression1 ; expression2 ; ... ; expressionN)`, and its value is the value of `expressionN`. And if such a chain needs to be formatted as a single expression, then it is placed in curly braces:

```
{ expression1; expression2; ...; expressionN }
```

In languages that support such an interpretation, blocks naturally take the place of expressions in any context — on the right side of an assignment, as function arguments, as operands.

### And what about the comma operator in C/C++?

The comma operator in C/C++ is very similar to the implementation of the described formal model — a composition of expressions:

```cpp
int y = (foo(), bar(), 42);   // foo() and bar() are evaluated for the side effect, y == 42
```

But there is a fundamental difference: the comma operator is always an Expression and cannot go beyond one expression context.

```cpp
int y = foo(), bar();   // This is NOT a comma-expression! These are two declarations (of a variable and a function)!
int z = (foo(), bar());  // and this is a real comma operator, parentheses are needed
```

Without explicit parentheses, the `,` (comma) operator in the context of a variable declaration or a function argument list is interpreted by the grammar **differently** — as a separator in a list (`declarator-list`, `argument-list`). And this ambiguity of the C/C++ grammar is resolved only by context.

Moreover, the `,` (comma) operator is limited to the `expression` level and cannot contain `statement` constructs:

```cpp
int y = (if (x > 0) 1; else 2, 42);   // ERROR - if is not an Expression, it cannot be inserted inside ","
```

And finally, the comma operator has the *lowest priority* among C operators, and its area of application is rigidly limited to contexts where the parser can be precisely sure that this is not a list separator:

```cpp
f(a, b, c);   // this is a call with three arguments, not a comma-expression!
f((a, b), c); // and this is already a comma-expression as the first argument
```


## Part 4. Formal grammar of universal syntax

If one tries to describe expression and statement in a general form, a beautiful mutual recursion between them is obtained:

```
Program     := Statement

Expression  := atomic-expr
             | Expression binary-op Expression
             | unary-op Expression
             | Expression '(' arg-list ')'          // function call
             | '{' Statement '}'                     // a grouped Statement is also an Expression!

Statement   := Expression
             | Expression ';' Statement              // recursive composition through ;

arg-list    := Expression (',' Expression)*
```

Where:
- **Statement** is either one expression, or an expression followed by `;` and a new `statement` (recursively).
- **Expression** is an atomic expression (an identifier, a literal, an operation, a call) or `{ statement }`, i.e. a block inside which there is a `statement`, but which itself is considered a single expression.

It is precisely the mutual recursion, in which blocks can contain `statement`, and `statement` is built from expressions, that gives such a model logicality and harmony, whereas different languages limit it in different ways:

- **C-like languages** allow the transition `{ statement } -> expression` **only** for simple expressions, but forbid it for control constructs. Therefore `if (x) { ... }` cannot be used as an expression, although the block in curly braces could itself be an expression if the grammar allowed this. Moreover, the GCC extension `({ ... })` literally implements such a rule: `{ statement }` is interpreted as `expression`, whereas standard C cannot do that.
- **Lisp-like** languages and **Ruby** do not need a separate `statement` nonterminal at all — in these languages everything is an expression.
- **Rust** turned out to be somewhere in the middle. It has a category of declarations (`let`, `fn`, `mod`), which are statements and cannot be part of an expression. Nevertheless, all control constructs (`if`, `match`, `loop`, etc.) are expressions.

## Conclusion

The division into `expression` and `statement`, familiar to many generations of developers of C-like languages, is not a logical necessity, but a *historical accident* fixed by the architecture of von Neumann machines, which was carried over from FORTRAN to ALGOL, and then to C. It became fixed in grammars as a "convenient" solution at the dawn of compiler construction, but it has no fundamental reasons at the level of syntax.

An analysis of the development of the syntax of different programming languages shows that **a statement can be considered as a structural composition of expressions**, and blocks in braces as a tool for the reverse "packing" of such a sequence into a single whole.

