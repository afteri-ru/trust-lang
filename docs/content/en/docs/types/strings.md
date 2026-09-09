---
title: Character strings
weight: 3
tags: [types]
---

*TrustLang* supports two string types:

- `:StrChar` — universal byte UTF-8 strings;
- `:StrWide` — system (wide-character) strings.

## Byte strings `:StrChar` {#StrChar}

A single element of a byte string is one byte (more precisely `:Int8`, an eight-bit signed integer). The size
of the string is returned in bytes, and index access refers to the byte of the sequence. Since the data
is interpreted as a sequence of UTF-8 characters, when modifying individual bytes care must be
taken.

## System strings `:StrWide` {#StrWide}

A single element of a system string is the wide character `wchar_t`, and the size is returned in wide characters.
The size of `wchar_t` depends on the operating system: in Windows one `:StrWide` character occupies 2 bytes,
in Linux — 4. The main purpose of system strings is to simplify work in a text terminal: one character
always corresponds to one character cell without converting UTF-8 code points.

Despite the different single element, the internal representation of both types is the same: the data of the strings
is stored as a byte UTF-8 sequence.

## String formatting {#format}

Any variable can be addressed as a function (by specifying parentheses after the name), thereby creating
a copy/clone of the object; for strings, such a call is used as a **data formatting template**.

Formatting is possible in two ways:

- **A `printf`-style format string** (the type `:FmtChar`/`:FmtWide`) is **not implemented** in the current version
  — the type `:FmtChar`/`:FmtWide` does not exist (see [Status](../status/)). For native functions with a `printf` string,
  the attribute `@[format("printf", pos, ...)]` is used (parameter checking at compile time,
  example — [hello](/en/playground/?file=hello)).
- **In all other cases** (any string by default), the format corresponds to the
  [{fmt}](https://fmt.dev/latest/syntax.html)/`std::format` library: the ordinal number of the argument
  (`{0}`, `{1}`) or empty `{}`. Argument checking is performed when the string is cloned
  (at compile time and at runtime). Named arguments (`name = …`) are **not implemented**.

```python
$template := "{0}: {1}";    # format as in {fmt}
$result := $template('value', 123);
```
