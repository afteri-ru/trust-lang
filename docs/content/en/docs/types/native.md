---
title: Native types
weight: 9
tags: [types, collections]
---

## Native data types
Native (machine) data can be both variables and functions, and the name of native objects starts with the percent sign "**%**".

In the case of variables, this is binary data that is located in one continuous region of machine memory
at a certain address and has a strictly defined format.

To use existing libraries, you must either import a native object,
thereby creating a TrustLang object but with an implementation in another language, for example C/C++,
or make a forward declaration of the native object.

## Importing native objects {#import}

**Import** is the binding of a **trust name** of a function/variable to the **name of an existing native**
function/variable. Unlike a [forward declaration](#native), an imported function
is registered under **its own** trust name, and its **name and arguments may differ** from the name
and arguments of the imported native function.

The syntax is the same as for the [creation](/en/docs/operators/create/) of ordinary objects, only the right operand
specifies the name of the native object. The types of the arguments and of the return value are always specified explicitly.

- If the types of the registered and the native function coincide (no conversion is required), the native name
  can be specified with an **ellipsis** — it means that **the arguments of the call repeat the arguments of the left
  signature** (they need not be duplicated):

  ```trust
  sqr(x:Float64):Float64 := %std::sqrt...;   # arguments like those of sqr
  ```

- In the general case, the arguments and the type of the imported function are specified **explicitly**; during generation, a
  call of the right function is inserted, and, if necessary, the arguments are cast via `static_cast`:

  ```trust
  sqr(x:Float64):Float64 := %std::sqrt(x:Float128, true):Float128;
  ```

On import, the arguments are automatically converted to the native types of the imported function. The import can
be performed at program compile time or at runtime (dynamic library loading).

```python
fopen(filename:StrChar, modes:StrChar):Int64 := %fopen...;
fclose(f:Int64):Int32 := %fclose...;
fflush(f:Int64):Int32 := %fflush...;
fprintf(f:Int64, format:StrChar, ...):Int32 := %fprintf...;
```

If a native function/variable requires linking an external library, the name of the library
is specified by the attribute `@[link("name")@]` before the declaration. The attribute adds the flag `-l<name>`
to the linking step (build.conf `LIBS`). The existence of the symbol/library is **not checked** —
it is the linker's responsibility. Only static linking is supported.

```trust
@[link("m")@]
fsqrt(x:Float64):Float64 := %sqrt...;

@[link("z")@]
fcrc(crc:UInt32):UInt32 := %crc32...;
```

If a native declaration/type requires including a C++ header, specify it with the attribute
`@[include("name")@]` before the declaration (or with the mnemonic `@include("name")` without `;`). A bare name →
an angle include `#include <name>`; an argument with a leading `"`/`<` — the directive as is. The header
is emitted into the generated C++ only together with this declaration:

```trust
@include("cstdlib")
%abort(x:Int32):Void := ...;   # → #include <cstdlib>
```

## Forward declaration of native objects {#native}

A **forward declaration** is the registration of a native function/variable
**whose name is** the native C-style identifier (e.g. `%sqrt` is the function `sqrt`). The name and arguments
of the declared function **coincide** with the native signature — no adaptation is performed.
The implementation may be located below in the current file or in an external library — the linker will find
the symbol by name at the build stage.

The rule of linking a native name: if it is **without** `::` (e.g. `%sqrt`, `%open`, `%abs`) — the declaration
is linked as a **C symbol** (`extern "C"`), which allows correctly calling libc/libm functions;
if the name **contains** `::` (e.g. `%std::sqrt`) — as a **C++ symbol** (`extern "C"` is not added).

The syntax is the same as for creation, but the native name is on the left, and on the right is the ellipsis character (no body).
The types of the arguments and of the return value are specified explicitly.

```python
    %open(path:StrChar, flags:Int32):Int32 := ...;
    %close(fd:Int32):Int32 := ...;
    %read(fd:Int32, buf:StrChar, cnt:SizeT):SizeT := ...;

    %fd^:Int32 := 0;
    fd = %open('foo.txt', 0);   # calling a native function by name
```

### Other native types (status)

The user-defined types `Enum`/`Variant`/`Tuple` are described in [Dictionaries and sets](../types/dicts/).

## Native template types

Any native C++ template type can be declared in TrustLang code (registration during AST analysis);
the C++ header is included on-use when the type is used. The declaration is a prefix of type parameters
+ the native C++ name:

```trust
@include("vector")
<T> %std::vector() := ...;    # the type vector<T>, the C++ name std::vector

v: vector<Int32> := [1, 2, 3,];
# → #include <vector>  +  std::vector<int32_t> c_v = std::vector<int32_t>{1, 2, 3};
```

A type argument is a bare name `Int32` (a soft warning about the sigil `:`) or an explicit type
`:Int32`/`:MyClass`/`:MyClass<:Int8>`. If the C++ name coincides with a built-in container
(`std::vector`/`std::array` from `:Array`), a soft diagnostic is issued, and the instantiation is resolved
through the built-in type. More — [Generics](/en/docs/syntax/generics/).

## Forward declaration of native classes {#native-class}

A native C++ class (not a template) can be used in TrustLang code through a **forward
declaration** by analogy with the forward declaration of functions: the trust name of the class is bound to the native
C++ name, and the interface members (methods, fields, constructors, static members) are described in the body
`{ ... }`. The class body does **not** contain an implementation — each member is a forward declaration (`:= ...`):
the class is defined in the C++ header, which is included on-use through the attribute `@[include]`.

```trust
@include("string")
String ::= %std::string {          # trust class String ↔ C++ std::string
    %size(): UInt64 := ...;        # object method → s.size()
    %empty(): Bool := ...;         # object method → s.empty()
    %c_str(): CString := ...;      # object method → s.c_str()
    %data: CString := ...;         # object field → s.data
};
```

Syntax:

- **`String ::= %std::string { ... };`** — the RHS is ANY name; native — with a leading `%` (the C++ name without
  `%`). The trust name of the class is to the left of `::=`.
- **Members** (all — forward, `:= ...`):
  - `%Name(...):Ret := ...;` — an object method → `obj.name(...)`;
  - `@::st(...):Ret := ...;` — a **static method** of the class (the name with `::`; `@::` → `ns::Class::`) → `Cls::st(...)`;
  - `%field:Type := ...;` — an object field → `obj.field`;
  - `@::static_field:Type := ...;` — a **static field** → `Cls::static_field`;
  - `%String(...):String := ...;` — a constructor (the name coincides with the class) → `std::string(...)`.
- **Classification:** a static member — the name contains `::` (specified as `@::name`); an instance member — without `::`.
  The correct registration of an instance member is a leading dot (`.name`); without it — the diagnostic
  `-Wclass-member-dot` (default `ignore`; `-Wclass-member-dot=warning|error`).
- **Access to a static member:** `Cls::name` (namespace style, without a warning) or `Cls.name`
  (as to a field) → `cppName::name`; the access `Cls.name` issues `-Wstatic-member-as-field` (by default
  `warning`; `-Wstatic-member-as-field=ignore|error`).
- The attribute `@[include("header")@]` (or the mnemonic `@include("header")`) before the declaration
  includes the C++ header **only when** this type is used (on-use).

A C++ `struct` for a native class is **not generated** — the existing C++ name is used.

```trust
@include("string")
MyStr ::= %std::string {
    %size(): UInt64 := ...;
    %data: CString := ...;   # object field → s.data
};

s: MyStr := MyStr('hello');              # constructor → std::string("hello")
n: UInt64 := s.%size();                  # method → (c_s).size()
d: CString := s.%data;                   # field → (c_s).data
# → #include <string>
```

## Forward declaration of native template classes {#native-class-template}

A native C++ template class is declared in the same way, but with type parameters on the left and an **explicit
implementation** through the instantiation `%std::pair<T1,T2>` on the right (the main form):

```trust
@include("utility")
<T1,T2> Pair ::= %std::pair<T1,T2> {    # trust template Pair ↔ std::pair
    %first(): T1 := ...;                # Pair<A,B>.first
    %second(): T2 := ...;               # Pair<A,B>.second
};

p: Pair<Int32, StrChar> := ...;
# → std::pair<int32_t, std::string> + #include <utility>
```

The generalized form `<T1,T2> Pair ::= <T1,T2> %std::pair { ... }` (the template is bound generically,
without an explicit list of arguments in the RHS) — sugar, the **implementation is deferred**: during parsing a
"not implemented" diagnostic is issued; use the explicit form `%std::pair<T1,T2>`. The notation format is the same
for a native and a non-native name and differs only in the presence/absence of `%`.


## Integration with C/C++: embed inserts and overview (interaction)

*TrustLang* is deeply integrated with the C/C++ ecosystem: direct function calls,
embedding of C++ code, use of the standard library and calls of *TrustLang* functions
from C++.

Ready, runnable examples — in the playground: [native functions](/en/playground/?file=native_functions),
[native linking](/en/playground/?file=native_link).

## Importing native functions: `@extern` / `@forward`

The mnemonic commands bind a trust name to a native C/C++ symbol:

```trust
@extern abs(x:Int32):Int32   # abs(x:Int32):Int32 := %abs ...
@forward open(path:StrChar, flags:Int32):Int32
```

- `@extern` — import by the native name (the trust name = the native name without the prefix `%`).
- `@forward` — a forward declaration (the name is specified first, the body is an ellipsis).

The raw notation: `%name(...):T := ...;` (a forward declaration) and
`name(...):T := %native...;` (an import alias).

## Embedding C++ code: the blocks `{% ... %}`

```trust
{% for (int i = 0; i < 10; ++i) { printf("%d\n", i); } %}
```

Native syntax is inserted into the output C++ code directly.

## Attributes of native declarations

- `@[link("name")]` — add `-l<name>` to linking.
- `@[include("header")]` — include the C++ header for a native declaration/type. A bare name
  (`@[include("vector")@]`) → an angle include `#include <vector>`; an argument with a leading `"`/`<` →
  the directive as is (`#include "my/header.h"` / `#include <cstdlib>`). The header is emitted into the
  generated C++ only together with the declaration (not globally). The mnemonic `@include("name")`
  (without `;`, before a declaration) expands into this attribute.
- `@[libstd]` — link with the standard library.
- `@[format("printf", ...)]` — check the arguments against the printf format string.

An example of including the header of a native function:

```trust
@include("cstdlib")
%abort(x:Int32):Void := ...;   # → #include <cstdlib> + extern "C" void abort(int32_t);
```

## Linking

The linking of a native declaration depends on the presence of `::` in the native name: without `::`
(`%sqrt`) — `extern "C"` (a C symbol of libc/libm); with `::` (`%std::sqrt`) — C++ linking
without `extern "C"`. More about C++ generation — in [Architecture](../architecture/).

For **modules in TrustLang itself**, a different mechanism is used — the *semantic ABI*: the type
of an object is restored by the language compiler from the attached semantic definition, and not from the
decorated name of the symbol. The principle is described in [Semantic ABI of modules](../sabi/).

## Links

- [Generic programming](generics/)
- [Code blocks](../operators/#block)

## Native template types and template classes

C++ template types and native classes/template classes are declared through type parameters and a forward interface of methods. Details — in [Generic programming](generics/).

