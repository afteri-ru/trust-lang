# MEMORY.md

> scope: include/debug
> role: persistent-memory
> last_reviewed: 2026-08-19
> review_period: 30
> max_size: 4093

## Architecture

Система отладки trust-lang через DAP-протокол (VSCode). `trust-dap` — DAP-сервер, драйвер
`GdbDebug` взаимодействует с GDB через **GDB/MI** (subprocess fork/exec; НЕ LLDB — нет зависимости).
`DapHandler` обрабатывает DAP-команды и через `trust::SourceMapReader` транслирует позиции
trust↔C++. Source map загружается из **embedded ELF-секции `.debug_trust_map`**
(`SourceMapReader::fromElf()`); внешние `.map`/`.trust` файлы НЕ используются.

Транспорт: `DapTransport` (абстрактный), `StdioTransport` (интерактивный stdin/stdout),
`TcpTransport` (`server[=<port>]`, default 4711).

## Facts and invariants

- **CLI-аргументы `trust-dap`: только `--project-dir`** (и `server[=<port>]`/`--gdb`). Все пути
  (`sourceFile`, `cppFile`, `targetFile`, `gdbPath`) передаются через DAP-запрос `launch`, НЕ через
  CLI — иначе сломалось бы делегирование путей из VSCode конфигурации.
- **Source-aware обработка (setBreakpoints/stackTrace):** тип файла определяется по расширению.
  `.src`-брейкпоинты транслируются через `SourceMapReader` (getMapTrustToCpp), `.cppt` — напрямую в
  GDB без трансляции. `sourceKind: "src"` (default) — стек показывает trust-файлы с трансляцией;
  `sourceKind: "cpp"` — C++ `.cppt` с оригинальными номерами от GDB без трансляции. Имена
  переменных от GDB (C++) транслируются в trust через `getTrustName`.
- **DAP-совместимая трансляция stop reason:** `breakpoint-hit`→`"breakpoint"`,
  `end-stepping-range`→`"step"`, `signal-received`→`"exception"`.
- **VSCode extension (Build pipeline):** сборка при F5 выполняется в `resolveDebugConfiguration`
  (`withProgress`: transpile → compile → trust-dap). Временные файлы (`.cppt`, ELF) — в каталоге из
  настроек `trust.tempDir` (default `.trust` в корне проекта).
- Зависимости: `msgpack-c` (бинарная сериализация mapping), `nlohmann_json` (только DAP JSON-RPC),
  `zlib` (сжатие source map). `diag_lib` используется через `trust::SourceMapReader`.
