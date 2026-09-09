---
title: Dictionaries and sets (enumerations)
weight: 4
tags: [types, collections, oop]
---

Working examples with the actual behavior of the current version can be run in the playground:
[dictionary](/en/playground/?file=dict), [dictionary with mixed types](/en/playground/?file=dict_mixed),
[enumeration and variant](/en/playground/?file=enum_variant).

## Dictionary (:Dictionary)

A dictionary is a set of data of an arbitrary type with access to elements by an integer index or by the
name of the element (if present) (it resembles both a tuple and a structure). A dictionary is always
one-dimensional, but each element can contain an arbitrary number of elements of any type, including
other dictionaries.

A dictionary literal is written in parentheses with a mandatory trailing comma:
`(,)` — an empty dictionary, `(1, two='2', name=3,)`. Access to an element — by name through a dot after the name of
the variable (`d.name`) or by an integer index (`d[0]`), which starts at 0 and can be
negative (counting from the end). Modification — by the operator `[]=`; the number of elements — by the method `size()`.

A positional literal without specifying a type (`(1, 2, 3,)`) always creates a dictionary (`:Dictionary`). A dictionary is
the only universal type: it can be cast to any other type, for which it is enough
to pass it as an argument to the cast/constructor of the target type (see
[Type conversion](type_system/)).

## Enumeration (:Enum) {#enum}

*Enum* is a type with a limited list of members, each of which has a name and a value. All members have a
**single** value type, which is inferred by the general rules — from the explicit values of the members or from an explicit
type annotation. The declaration literal is written in the postfix form `(members,):Enum` or the prefix
`:Enum(members)` (as for a typed tuple); the trailing comma is mandatory.

```python
Status ::= (OK=0, ERROR=1, BUSY=2,):Enum;   # members with explicit values; the value type is Int64
Color ::= (RED, GREEN, BLUE,):Enum;          # bare members — autoincrement from 0
Flag  ::= (LOW:Int8, HIGH,):Enum;            # the explicit type of a member sets the value type Int8
```

If there are no explicit values, the value type is the minimal signed integer by the number of members; the explicit annotation
`name:Type` forces the value type (it must be the same for all members). Comparison of
members is performed **by value**, not by position.

Access to a member — through the type name: `Status.OK`. Classic methods (through the type name):
`count()` (the number of members), `fromName(name)` (a member by name), `fromValue(value)` (a member by value).

Access to a member (`Status.OK`, `Data.i`) and the methods `count()`/`fromName()`/`fromValue()` are **implemented**
(example — [enumeration and variant](/en/playground/?file=enum_variant)).

## Variant (:Variant) {#variant}

*Variant* is a heterogeneous type: each member has its **own** data type (an analog of `std::variant`). The type of a member
is set by an explicit annotation (`name:Type`) or inferred from its value; for a bare member without a type,
the minimal signed integer by position is taken.

```python
Data ::= (i=42, s='text', r=1\2,):Variant;  # member types: Int8, StrChar, Rational
V    ::= (X:Int64=7, Y:Rational,):Variant;  # explicit member types
```

Access to a member — through the type name: `Data.i` (the type is the type of the concrete member). A classic method:
`count()` (the number of members).
