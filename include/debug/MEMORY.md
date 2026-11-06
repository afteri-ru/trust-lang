# MEMORY.md

> scope: include/debug
> role: persistent-memory
> last_reviewed: 2026-11-13
> review_period: 30

## Architecture

Система отладки trust-lang через DAP-протокол (VSCode): `trust-dap` — DAP-сервер, драйвер `GdbDebug`
взаимодействует с GDB через **GDB/MI** (subprocess fork/exec; НЕ LLDB — нет зависимости).
Публичного API у компонента НЕТ (tool-компонент): все заголовки — детали реализации.

| каталог | MEMORY | ответственность |
|---|---|---|
| `debug/dap/` | `dap/MEMORY.md` | DAP-команды, трансляция позиций trust↔C++, транспорт |
| `debug/gdb/` | — (инварианты в коде) | `GdbDebug` (сессия) и `StackFrameInfo` |
| `debug/mi/` | — (инварианты в коде) | запуск GDB/MI и чистый разбор строк MI |

## Facts and invariants

- **CLI-аргументы `trust-dap`: только `--project-dir`** (и `server[=<port>]`/`--gdb`). Все пути
  (`sourceFile`, `cppFile`, `targetFile`, `gdbPath`) передаются через DAP-запрос `launch`, НЕ через
  CLI — иначе сломалось бы делегирование путей из VSCode конфигурации.
- **Source-aware обработка (setBreakpoints/stackTrace):** тип файла — по расширению. `.src`-брейкпоинты
  транслируются через `SourceMapReader`, `.cppt` — напрямую в GDB. `sourceKind: "src"` (default) — стек
  показывает trust-файлы с трансляцией; `"cpp"` — C++ `.cppt` с оригинальными номерами. Имена переменных
  от GDB (C++) транслируются в trust через `trustNameOf`.
- **DAP-совместимая трансляция stop reason:** `breakpoint-hit`→`"breakpoint"`,
  `end-stepping-range`→`"step"`, `signal-received`→`"exception"`.
- **VSCode extension (Build pipeline):** сборка при F5 — в `resolveDebugConfiguration`
  (`withProgress`: transpile → compile → trust-dap). Временные файлы (`.cppt`, ELF) — в каталоге
  `trust.tempDir` (default `.trust`).
- **Зависимости:** `msgpack-c` (бинарная сериализация mapping), `nlohmann_json` (DAP JSON-RPC),
  `zlib` (сжатие source map).

## Decisions

## Relations
