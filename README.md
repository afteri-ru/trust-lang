# TrustLang - A Trusted Programming Language

Rework of the project's code base with a name change (**NewLang -> TrustLang**).

TrustLang is a high-level general-purpose programming language for safe (trusted) development. It is implemented as a transpiler to C++, providing safe memory management without a garbage collector and detecting memory management errors and data race conditions at compile time. 

The TrustLang supports tensor computations (LibTorch), rational numbers with unlimited precision, static and dynamic typing, and named and optional function parameters. It also provides one-dimensional arrays via literals `[1,2,3,]` / `[...]:Type` (→ `std::vector<Elem>`), index access `a[i]`, the `:Array(...)` constructor, and array methods (`count`, `at`, `first`, `push_back`, ...); multi-dimensional arrays (tensors) are reserved for the LibTorch backend.

It supports deep integration with the C/C++ ecosystem, including direct function calls, C++ code embedding, and use of the C++ standard library.

**The project is under active development and is developed with the help of AI agents.**


## Testing

Tests are registered with **CTest** and run via:

```sh
cmake -B _build && cmake --build _build
ctest --test-dir _build --output-on-failure   # all tests
```

Test layout (CTest runs suites in this order):

- **unit/** - C++ unit tests (`test/unit/`), a single `unit_tests` executable
  registered as one CTest test; `ctest --output-on-failure` prints the full
  GTest report (with failing test names) when the suite fails.
- **lit/** - LIT integration tests (`test/lit/`), registered as the `lit_tests`
  CTest test (run through the `trust` CLI + FileCheck).
- **vscode/** - VSCode extension tests (`test/vscode/`), registered as
  `vscode_unit`, `vscode_dap` and `vscode_lsp` CTest tests.
- **examples/** - standalone programs (`examples/*.src`), run one-by-one via
  `trust --run` as the `examples_tests` stage (after unit/lit/vscode).
- **integration/** - playground end-to-end chain (`integration_tests`, after examples).
- **package/** - Distribution archive and Debian package tests (`test/package/`):
  `package_target_test`, `deb_package_test` - the LAST suites. They run only after
  the `unit/lit/vscode` suites pass (fixture `package_deps`).

`make run_tests` (alias `cmake --build _build --target run_tests`) builds the
test binaries (unit_tests, trust, trust-dap, trust-lsp) and then runs the same
CTest suite.

## Formatting (`--format`)

Pretty-printing of Trust source is implemented as a core pipeline feature
(`formatter` component) and exposed through:

- **CLI** - `trust --format <file>` prints the formatted source to stdout;
  `trust --format-check <file>` exits `0` if the file is already formatted and
  `1` otherwise (useful for CI).
- **LSP** - `textDocument/formatting` returns the formatted document as a
  `TextEdit`; the VSCode extension registers `documentFormattingProvider` and
  sets `[trust].editor.defaultFormatter` to `rsashka.trust-lang`, so "Format
  Document" works out of the box.

The formatter operates on the raw token stream produced by the lexer and the
gaps between tokens: it normalizes indentation and spacing, preserves regular
comments (`#`, `/* */`) and doc comments (`##`, `///`, `/** */`), keeps
embedded C++ (`{% ... %}`), raw strings and macros verbatim, and collapses
multiple blank lines into one. Output is idempotent (formatting an already
formatted file is a no-op).

### Configuration (`.trust-format`)

Formatting settings are read from a `.trust-format` file in the source
directory or any parent (searched upward, like `.clang-format`). The format is
`Key: Value` with `#` comments and an optional `---` header:

```
IndentWidth: 4            # indent width in spaces (default 4)
UseTabs: false            # true / false / Always / Never (default false)
ColumnLimit: 120          # max line width for inline lists (default 120)
InsertFinalNewline: true  # ensure a trailing newline (default true)
Keywords: print,each      # macro names allowed without the '@' sigil
```

Values such as `IndentWidth`/`UseTabs`/`ColumnLimit` are configured **only** in
`.trust-format` (there are no CLI flags for them). The CLI only selects the
config source: `--format-config=<file>` (explicit file), `--format-style=none`
(ignore `.trust-format`, defaults only). `trust --format-dump-config` prints all
formatting settings with their default values and comments (like
`clang-format -dump-config`). `trust --help` groups these under a dedicated
**Formatting** section.

Bash tab-completion for `trust` options is available by sourcing
`include/pipeline/trust-completion.bash`. It is data-driven from the driver option table
(`trust --complete-options` / `trust --complete-files`), so the completed option
names — including which options take a file/path value — never drift from the CLI.
The completion finds the binary by probing `$TRUST_BIN`, then a `./trust` or
`./_build/trust` in the current directory, then a `_build/trust` relative to the
script (so it works from a build directory or straight from the repository without
PATH setup), then a PATH entry. A candidate is accepted only if it answers
`--complete-options` and reports itself as `TrustLang <version>` via `--version` —
the unique project brand (the binary name `trust` itself collides with p11-kit's) —
so a foreign `trust` on PATH, or any other binary that merely supports
`--complete-options`, is never mistaken for the compiler. If no valid binary is
found, the cache is cleared so the next completion retries.
The script is also shipped in the distribution tarball at
`share/bash-completion/completions/trust` and in the `.deb` package at
`/usr/share/bash-completion/completions/trust` (the standard bash-completion
location), so it can be copied or symlinked into `/usr/share/bash-completion/completions/`.

### Macro names without `@` (`--keywords` / `Keywords:`)

A macro defined as `@@ test @@ ... @@` can already be called without the `@`
sigil (`test()` expands the same as `@test()`); the `-Wsigil` diagnostic warns
about the missing `@`. Listing such a macro under `Keywords` (in `.trust-format`
or via `--keywords=test,each`) suppresses that warning (the macro is treated
like a keyword) and makes the formatter format it like a control keyword
(`each (...)`). Only list entries without a leading `@` suppress the warning.

The **default** keywords list comes from the built-in `trust/dsl.src`
(`@__OPTION__("keywords", "func,extern,forward,if,elif,else,while,dowhile,loop,module,return,break,continue,match,case,default,namespace")`)
and can be overridden by `.trust-format` `Keywords:` and then by CLI `--keywords=`
(priority: **CLI > `.trust-format` > dsl default**).

Trust has **no built-in keywords** — `if`, `while`, `return`, `func`, `namespace`,
etc. are DSL macros defined (and redefinable) in `dsl.src`/`--dsl`. The formatter
therefore has no hardcoded keyword set: it formats names from the *effective*
keywords list as control keywords (space before `(` and `{`, like clang-format).
`--format` loads the DSL to obtain this list (respecting `--dsl`/`--no-dsl`); value
literals such as `true`/`false` are not in the list and keep the `-Wsigil` warning
when used bare.

## Runtime linking (`--link-runtime`)

Runtime-backed types (e.g. `Rational`) need the trust runtime library, which is
built both as a dynamic library (`trust-runtime.so`) and a static one
(`trust-runtime.a`). When compiling an executable, the pipeline links the runtime
according to `--link-runtime` (default **`static`**):

- `static` - the runtime code is linked into the executable from `trust-runtime.a`;
  the resulting binary is self-contained and runs without `trust-runtime.so`.
- `shared` - the executable links `trust-runtime.so` dynamically and resolves it
  at run time via `LD_LIBRARY_PATH` / the launch directory.

## Diagnostics options (`-W<name>[=status]`)

Severity-options control compile-time diagnostics (status: `ignore`/`remark`/`note`/`warning`/`error`/`fatal`):

- `-Whelp` - print the central diagnostics help (single source, grouped by `DiagGroup` and
  `WarnGroup` aggregates). Driver help (`--help`) and diagnostics help (`-Whelp`) are two separate
  help commands (the "two-help model"); both are generated centrally from the option/diagnostics
  tables. Diagnostics are registered per component through the unified API (`TRUST_DIAG_SET`/
  `TRUST_FLAG_SET` + `registerDiagnostics()`), see `include/diag/OPTIONS.md` and `include/pipeline/cli.hpp`.
- `-Wembed=ignore` - silence the warning about the very fact that a `{% ... %}` C++ embed block
  is used (independent of the names inside; default `warning`).
- `-Wsigil=ignore` - silence the warning about normalizing a simple local variable declared
  without the `$` sigil into a local `$x` (default `warning`).
- `-Wformat=ignore|warning|error` - controls compile-time type checking of arguments against a
  printf format string for native functions marked with `@[format("printf", ...)]` (default `error`).
- `-Wunused-variable` / `-Wunused-parameter` - separate diagnostics for unused variables and
  unused function parameters (enabled via `-Wlint`, groups `Wall`/`Wextra`/`Wunused`).
- `-Werror` / `-Wno-error` - a global switch (clang/gcc style): `-Werror` promotes all warnings
  to errors, `-Wno-error` restores them.

## Trust conditions, `-Wsolver` and `--solver-mode`

Trust conditions (pre-/post-conditions and assertions) are declared with the DSL mnemonics
`trust_pre( <expr> )` (`@( <expr> @)`), `trust_post( <expr> )` (`@< <expr> @>`) and
`trust_assert( <expr> )` (`@{ <expr> @}`). They attach after the name of a function (pre/post),
a variable/type (assert), or stand alone as `@{ <expr> @};` statements in a body. The return value
of a function is referred to by the function's own name in a postcondition; using it in a
precondition is an error. In a type assertion the type name acts as a value of that type
(`MyInt trust_assert( MyInt > 0 ) ::= :Int32`), checked where a value of the type is created.
A data type carrying a trust assertion cannot be auto-deduced (`x := ...`) - it requires an
explicit type annotation (`x :T := ...`).

Trust conditions are controlled by two orthogonal options (GCC/Clang convention: a severity
diagnostic `-Wsolver` and a behavioral flag `--solver-mode`, never mixed):

- `-Wsolver=ignore|warning|error` - severity of the *presence* diagnostic «trust condition(s)
  present», i.e. a warning/error that such conditions exist (default `warning`). Silence with
  `-Wsolver=ignore`. It is NOT emitted when `--solver-mode` is active (explicit verification needs no hint).
- `--solver-mode=assert|export|calculate` - behavioral flag (not a diagnostic):
  - `assert` - insert runtime checks (precondition at the start of the function body, postcondition
    before exit, assertion at its definition site, a variable assertion right after its declaration).
    Both `@assert`/`@verify` and trust conditions lower to the single intrinsic
    `trust::intrinsic_assert` (see `include/types/intrinsics.hpp`), which is expanded at code
    generation into `if (!(cond)) trust::trust__abort__(file, line, "<cond>", backtrace)` - one source
    of truth for runtime assertion checks.
  - `export` / `calculate` - generate (and, for `calculate`, run through Z3) an SMT-LIB 2 file for the z3
    solver: `export` writes `<input>.smt2` (+`.smt2.map` source map), `calculate` checks satisfiability of the
    verification conditions and, on a violation (SAT), reports a counterexample (parameter values from the
    model). Works without Z3 for generation; `calculate` needs `WITH_SOLVER=ON` (Z3), otherwise reports
    "недоступно".
- Loops are verified by an explicit invariant (`@{ invariant: I @};` before the loop → induction). A loop
  without an invariant emits a `-Wsolver-loop` diagnostic (`ignore|warning|error`, default `warning`: the
  loop is not verified). Bounded unrolling can be enabled globally with the behavioral flag
  `-fsolver-loop-unroll` / `-fno-solver-loop-unroll` (like `-funroll-loops`, not a `-W` diagnostic) or
  per-loop with the `z3_unroll(N)` term inside the invariant contract (`@{ invariant: z3_unroll(3) @};`).
- Quantifier bound variables (`z3_forall(i, ...)` / `z3_exists(i, ...)`) must be variables **declared
  earlier**; the quantifier ranges over the type of that variable (its value is not used). An undeclared or
  auto-inferred bound variable is an error.
- `@__OPTION__("<flag>", "on"|"off"|<value>)` can set feature flags from source (e.g.
  `@__OPTION__("solver-mode", "assert")`); flag values are validated, invalid ones are errors.

## Analysis options in the Language Server (shebang)

For `trust`, a `#!...` first line is just a lexer comment; the compiler receives its
options through `argv` (the OS runs the script via the shebang). The Language Server,
however, opens the file as text, so it would otherwise ignore those options and report
e.g. `trust condition(s) present` (`-Wsolver`) or `macro 'X' is missing '@' sigil`
(`-Wsigil`) even though running the file via `trust` would not.

The LSP therefore reads the shebang of the opened document and applies its *analysis*
options (diagnostics `-W...` and behavioral flags `--solver-mode`, `--keywords`,
`-fsolver-loop-unroll`) to the analysis context, alongside the environment options
(trust-lsp CLI args / VSCode settings, e.g. the `trust.lspArgs` array). Execution/link
options (`--run`, `-o`, ...) are ignored.

Common analysis options are defined **once** (`commonAnalysisOptions`,
`include/pipeline/cli.hpp`) and applied centrally (`applyAnalysisArgs`,
`include/pipeline/analysis_options.hpp`) by both `trust` and `trust-lsp`. `trust-lsp` does
not list them in its own option table: any unknown `--name=value`/`-fname` CLI option is
collected (`analysis_passthrough` in `parseDriverArgs`) and parsed by the same common
`applyAnalysisArgs`, so the set of common options can grow arbitrarily without changes to
the LSP. The LSP applies the environment and shebang option sets separately (in the
precedence order below) and reports option errors **by source**: an invalid option in the
shebang (e.g. `--solver-mode=bogus`) is published as a regular `Error` diagnostic on the
shebang line, while an invalid environment option is logged.

The `trust.shebangMode` setting (CLI `--shebang-mode=`) controls the precedence between
environment and shebang options:

| value | effect |
|---|---|
| `ignore` | never read the shebang (environment only) |
| `shebang-only` | apply only the shebang options |
| `env-after-shebang` (default) | shebang first, then environment (environment overrides) |
| `env-before-shebang` | environment first, then shebang (shebang overrides) |

For a file without a shebang, only the environment options apply in every mode.

## Generated C++ files

Every generated `.cppt` (and the `_main.cppt` entry file) starts with an autogenerated header
(first line: project name, full compiler version, and generation date/time). The `LICENSE` file
is copied into the build directory next to the generated `Makefile`/`build.conf` (the license
text itself is not embedded into the generated C++ files).

## Building a distribution archive

The `package` target produces a gzipped tar archive for installation/distribution:

```sh
cmake --build _build --target package
```

The archive is also built automatically as part of the default build
(`cmake --build _build`) - independently of running the tests (test binaries and
`ctest` are not required). The archive (and the `.vsix` package, when built) are
placed in a dedicated `_build/dist/` directory. The archive name encodes the build attributes:

```
trust-lang-<version>-<git-hash>-<os>-<arch>.tar.gz
```

where `<os>` derives from `CMAKE_SYSTEM_NAME` (e.g. `linux`) and `<arch>` from
`CMAKE_SYSTEM_PROCESSOR` (e.g. `x86_64`). The archive contains the compiler
(`trust`), the language servers (`trust-lsp`, `trust-dap`), the runtime libraries
(`trust-runtime.so` and `trust-runtime.a`), `VERSION`/`LICENSE`, and a
`manifest.txt` with the build metadata (version, git hash, OS, arch, date).
The public runtime headers and the stdlib sources are not copied separately: their
contents are embedded into the runtime libraries / the compiler and stay
version-synced with them.

Because the toolchain (clang-22, LLVM, GMP, bison/flex, lit) and the pipeline are
POSIX/ELF-based, the recommended way to build on a Windows host is **WSL2**, where
the environment is a native Linux and the archive is produced as a `linux-<arch>`
package. A fully native Windows build requires separating the embedded-header
storage from ELF sections and replacing the make-based pipeline - a separate task.

## Debian package (.deb)

On Debian/Ubuntu hosts (when `dpkg-deb` is available) a `.deb` package for
automatic installation is built alongside the archive, both as part of the
default build and on demand:

```sh
cmake --build _build --target deb          # or just: cmake --build _build
sudo apt install ./_build/dist/trust-lang_<version>_<arch>.deb
```

The package installs `trust`, `trust-lsp`, `trust-dap`, `trust-playground` into
`/usr/bin`, the runtime libraries (`trust-runtime.so`/`.a`) into `/usr/lib`, and
the bash completion into `/usr/share/bash-completion/completions/trust`. The
`Depends` field is computed from the binaries via `dpkg-shlibdeps`, and the
package version is strictly the release number (`0.6.0`) - a distribution package
must not embed a build-specific git hash, or `apt` would treat every rebuild as a
new version.
