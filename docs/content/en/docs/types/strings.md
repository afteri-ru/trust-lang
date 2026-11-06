---
title: Character strings
weight: 3
tags: [types]
---

*TrustLang* supports two string types:

- `:StrChar` — universal byte UTF-8 strings; literal in double quotes `"…"`;
- `:StrWide` — system (wide-character) strings; literal in single quotes `'…'`.

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

The width of a string literal is set by the quotes: `"…"` — `:StrChar`, `'…'` — `:StrWide`.
Raw strings follow the same rule by delimiter: `R"(…)"` — `:StrChar`, `R'(…)'` — `:StrWide`.

## String methods {#methods}

Strings provide the methods of the underlying C++ class (`std::string`/`std::wstring`): element access
(`at`, `front`, `back`), size and capacity (`size`, `length`, `empty`, `capacity`, `reserve`,
`shrink_to_fit`, `resize`), modification (`clear`, `append`, `push_back`, `pop_back`, `insert`, `erase`,
`replace`, `assign`) and operations (`find`, `rfind`, `substr`, `compare`, `starts_with`, `ends_with`,
`contains`). `StrChar` additionally provides `c_str()` and `data()` (result — `const char*`).

## String iteration and conversion {#iterate}

A cursor over a string is the single iterator type `Iterator<T>` of the standard library, marked
`@[borrowed]` (see [Iterating collections](containers/)). The `chars()` method returns a cursor over the
elements (`:Int8` for `StrChar`, `:Int32` for `StrWide`), built from the string's `begin/end`; while the
cursor is alive the string must not be changed. Conversion between string types uses
`toWide()`/`toNarrow()` (UTF-8 ↔ wide characters).

```python
s : StrChar := "hello";
it : Iterator<Int8> := s.chars();
@while( it.hasNext() ) { ch := *it; ++it; };
w : StrWide := s.toWide();
back : StrChar := w.toNarrow();
```

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
