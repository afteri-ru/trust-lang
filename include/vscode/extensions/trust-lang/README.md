# Trust Lang — VS Code Extension

Language support and debugger for [Trust language](https://github.com/afteri-ru/trust-lang).

## Features

- **Syntax highlighting** for `.src` files
- **Language Server Protocol (LSP)** — diagnostics, completions, hover info
- **Debug Adapter Protocol (DAP)** — full debugging via LLDB
- **Tasks** — build and run `.src` files directly from VS Code
- **Open C++ file** — jump to the generated C++ source during debugging

## Requirements

- **Trust compiler** (`trust`) — transpiles `.src` to C++
- **C++ compiler** (e.g. `clang++-22`) — compiles transpiled code
- **LLDB** (`lldb-server`) — debugger backend
- **Trust LSP** (`trust-lsp`) — language server
- **Trust DAP** (`trust-dap`) — debug adapter

All tools are installed as part of the Trust language toolchain.

## License

LGPL