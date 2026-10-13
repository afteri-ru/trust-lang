# MEMORY.md

> scope: include/debug/dap (+ src/debug)
> role: persistent-memory
> last_reviewed: 2026-11-05
> review_period: 30
> max_size: 3000

## Architecture

DAP-слой отладчика: `DapHandler` (обработка DAP-команд, трансляция позиций trust↔C++ через
`trust::SourceMapReader`), `DapTransport`/`StdioTransport` (интерактивный stdin/stdout) и
`TcpTransport` (`server[=<port>]`, default 4711).

## Facts and invariants

- **⚠ Source map берётся из embedded ELF-секции `.debug_trust_map`** (`fromElf()`); внешние
  `.map`/`.trust` файлы НЕ используются.
- **⚠ Source-aware обработка (setBreakpoints/stackTrace):** тип файла — по расширению; `.src`-
  брейкпоинты транслируются через `SourceMapReader`, `.cppt` — напрямую в GDB; `sourceKind: "src"`
  (default) показывает trust-файлы с трансляцией, `"cpp"` — C++ `.cppt` с оригинальными номерами.
  Имена переменных от GDB (C++) транслируются в trust через `getTrustName`.
- **DAP-совместимая трансляция stop reason:** `breakpoint-hit`→`"breakpoint"`,
  `end-stepping-range`→`"step"`, `signal-received`→`"exception"`.

## Decisions

## Relations

- `debug/gdb`, `debug/mi`, `sourcemap` (SourceMapReader).
