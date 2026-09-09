---
title: Conditions and loops
tags: [operators, control-flow]
weight: 30
---

In *TrustLang* there is no `if` keyword: a condition is checked by a construct that in meaning corresponds to
the mathematical "follows" — two dashes and an angle bracket `-->`. The notation is easily combined into chains for
checking several conditions of the "else if" kind. Below the construct is shown in both forms — the DSL macro
and the base syntax (about the two notation forms see [Macros and DSL](../syntax/macros/)):

<table>
<tr><th>DSL macros</th><th>Base syntax</th></tr>
<tr><td><pre><code>@if( condition1 ) {
    action1
} @elif( condition2 ) action2
@else {
    action_otherwise
};</code></pre></td><td><pre><code>[ condition1 ] --&gt; { action1 },
[ condition2 ] --&gt; action2,
[ ... ] --&gt; { action_otherwise };
</code></pre></td></tr>
</table>

To write loops, the operator `<->` is used (in the base notation) or the macros `@while`/`@dowhile`
(in DSL). The condition is written in square brackets; depending on the relative position of the condition and the
body, a loop can be a precondition loop (`while`) or a postcondition loop (`do while`). Equivalent forms:

<table>
<tr><th>DSL macros</th><th>Base syntax</th></tr>
<tr><td><pre><code>@while( cond ) {
    loop body
};</code></pre></td><td><pre><code>[ cond ] &lt;-&gt; {
    loop body
};</code></pre></td></tr>
<tr><td><pre><code>{
    loop body
} @dowhile( cond );   # postcondition</code></pre>
</td><td><pre><code>{
    loop body
} &lt;-&gt; [ cond ];   # postcondition</code></pre></td></tr>
<tr><td><pre><code>@while( cond ) {
    ...
} @else {
    ...    # if cond == false at entry
};</code></pre></td><td><pre><code>[ cond ] &lt;-&gt; {
    ...
}, [ ... ] --&gt; {
    ...    # else branch
};
</code></pre></td></tr>
</table>

The precondition loop (`while`) supports an `else` branch, which is executed if the loop entry condition
was never fulfilled.

> **Note:** this differs from the analogous construct in Python, where the `else` section is always executed,
> except when the loop is interrupted by `break`.

## Iterating over collections (foreach)

The `foreach` loop has no separate DSL keyword — it is implemented by a loop with element destructuring and
mutation of the source (the list expansion operator). An example of summing the elements of a dictionary:

```python
summa := 0;
dict := (1,2,3,4,5,);
[ dict ] <-> {          # loop condition — while there is data in dict
    # unpacking: the first element goes to item, the suffix dict... — the rest
    item, dict... := ... dict;
    summa += item;      # accumulating the sum
};
```

## Pattern matching (match)

The expression evaluation operator (`match`) is an approximate analog of the *switch*/*match* operators in other
languages. It is specified by the construct `[ expression ] ==> { ... }` (base syntax) or the macro `@match`
(DSL), whose body lists condition branches. Equivalent forms:

<table>
<tr><th>DSL macros</th><th>Base syntax</th></tr>
<tr><td><pre><code>@match( $var ) ==&gt; {
    @case( 1 ) { code };
    @case( 1, 2 ) { code };
    @default { code default };
};</code></pre></td><td><pre><code>[ $var ] ==&gt; {
    [1] --&gt; { code };             # $var == 1
    [1, 2] --&gt; { code };          # ($var == 1 || $var == 2)
    [...] --&gt; { code default };   # otherwise
};</code></pre></td></tr>
</table>

Although the operator looks externally like pattern matching, in essence it is a short notation
of a multiple comparison operator: any comparison operator can be used as the evaluation operator:

- `==>` — equality with type casting;
- `===>` — exact equality;
- `~>` — type check (class name);
- `~~>` — duck typing;
- `~~~>` — strict duck typing.

If duck typing is used as the comparison operator, the evaluation turns into classic
pattern matching by fields (the example is given in the `@match` form):

```python
$value := (f1=1, f2='2',);
@match( $value ) ~~> {
    @case((f1=_,), (f1=_, f2=0,)) { ... }; # the field f2 is absent or is a number
    @case((f1=_, f2="",), (f1=_, f2='',)) { ... }; # the field f2 is a string
    @default { ... };
};
```

## Overriding the comparison function (attribute `@[matcher("fn")]`)

If value comparison does not reduce to `==` (for example, matching a string against a regular expression),
the comparison function of `match` can be overridden with the attribute `@[matcher("fn")]`, where `fn` is a
previously declared predicate function with the signature `bool fn(T_value, T_pattern)`. In each branch, instead of the
comparison `(value == pattern)`, the predicate `fn(value, pattern)` is called; the checked expression (scrutinee)
is evaluated once into a temporary variable. With a given matcher, the branches are always generated as an
`if/else` chain (the `switch` operator is not applicable — the pattern is not required to be a constant).

The attribute is placed between the value and the operator (in the "raw" form, since the `@match` macro requires the operator
immediately after itself):

```python
@func matches_regex(value:StrChar, pattern:StrChar):Bool { ... };
$s := 'foobar';
[ $s ] @[matcher("matches_regex")@] ==> {
    @case('^foo') { ... };
    @case('bar$', 'baz') { ... };
    @default { ... };
};
```

The semantics verifies that `fn` is a declared predicate function with exactly two parameters and a boolean
(Bool) return. The `@[matcher]` attribute is incompatible with matching by type (`~>`/`~~>`/`~~~>`).

