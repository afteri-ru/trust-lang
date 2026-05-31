# Trust-lang Debug System Architecture

## Overview

Система отладки для trust-lang — языка, транслируемого в C++.
Позволяет отлаживать trust-lang код напрямую через DAP-протокол (VSCode),
в том числе навигировать по исходному коду и устанавливать значения переменных
как в C++ файле, так и в оригинальном trust-исходнике.

## Namespace

- `GdbDebug`, `DapHandler`, транспортные классы — глобальное пространство имён
- `trust::Context` — пространство имён `trust` (из `diag/`)
- `trust::SourceMapReader` — пространство имён `trust` (из `diag/mapper.hpp`)

## Принципы

`trust::Context` (см. `include/diag/context.hpp` и `include/diag/ARCH.md`) — единая точка входа
для source-маппинга: хранение пар `(trust_file, cpp_file)`, маппинг диапазонов (byte offset-based),
имен переменных, сериализация (msgpack), нормализация путей. Context используется transpiler'ом
(добавление данных) и debugger'ом (чтение/трансляция).

Типы маппинга: `Location`, `SourceRange`, `FileIdx` (из `include/diag/location.hpp`),
`RangeMap`, `NameMap`, `SourceMapping` (из `include/diag/mapping.hpp`).

---

## Компоненты

### 1. trust::Context (`diag/context.hpp`, `src/diag/context.cpp`)

Единый класс хранения, загрузки, сериализации и трансляции source-маппинга.
Используется компонентами debug, lsp и diag. Детальное описание API — в `include/diag/ARCH.md`.

Основные методы, используемые в debug: `mapTrustToCpp(Location)` / `mapCppToTrust(Location)`, `getCppName()` / `getTrustName()`, `getTrustFileMappings(FileIdx)`, `loc_from_line()`, `line()` / `column()` / `line_column()`, `filename()` / `source()`, `packMapping()` / `unpackMapping()`, `file_count()` / `output_count()`.

---

### 2. GdbDebug (`include/debug/gdb_debug.h`, `src/debug/gdb_debug.cpp`)

Центральный класс взаимодействия с GDB через GDB/MI протокол (subprocess fork/exec).

Возможности:

- Создание таргета, установка брейкпоинтов (по имени и по файлу+строке)
- Запуск процесса, StepOver, StepInto, StepOut, Continue
- Чтение stdout процесса (console output)
- Доступ к переменным через `-stack-list-variables`
- Получение stack frames через `-stack-list-frames`
- Асинхронный режим через GDB/MI async-уведомления
- DAP-совместимая трансляция stop reason: `breakpoint-hit` → `"breakpoint"`, `end-stepping-range` → `"step"`, `signal-received` → `"exception"`

Параметр конфигурации: `gdbPath` (путь к gdb, по умолчанию `"gdb"`).

---

### 3. DapHandler (`include/debug/dap_handler.hpp`, `src/debug/dap_handler.cpp`)

Содержит класс `DapHandler`, обрабатывающий все DAP-команды. Использует `GdbDebug` для управления отладкой и `trust::SourceMapReader` для трансляции позиций между trust- и C++-файлами.

Ключевые методы: `isTrustFile(path)`, `translateCppToTrust(...)`.

---

### 4. DAP Transport (`include/debug/dap_transport.h`, `src/debug/dap_transport.cpp`)

Отвечает за DAP-транспорт: чтение/запись пакетов в формате Content-Length,
TCP-сервер, вспомогательные функции для формирования DAP-сообщений.

Классы: `DapTransport` (абстрактный базовый), `StdioTransport` (stdin/stdout, interactive-режим), `TcpTransport` (TCP-сокет, server-режим).

DAP Protocol helpers: `nextDapSeq()`, `readDapPacket()`, `sendDapResponse()`, `sendDapEvent()`, `sendDapOutput()`, `sendBreakpointEvent()`.

TCP server helpers: `createTcpServer(port)`, `acceptConnection(serverFd)`.

CLI parsing: структура `DapOptions` (port, help, projectDir, gdbPath), `parseDapOptions()`, `printUsage()`.

---

### 5. trust-dap (`src/debug/trust_dap.cpp`)

DAP-сервер для отладки trust-lang. Содержит класс `DapHandler`,
который обрабатывает все DAP-команды через `GdbDebug` (GDB/MI) и `trust::SourceMapReader`
для трансляции позиций между trust- и C++-файлами.

Особенности реализации:

- Использует `GdbDebug` вместо `TrustDebug` (LLDB) — нет зависимости от LLDB
- Source map загружается из embedded ELF-секции `.debug_trust_map` через `SourceMapReader::fromElf()`
- Thread `pollEvents()` обрабатывает `*stopped`/`*exit` асинхронные уведомления GDB
- Поля класса следуют CODESTYLE (префикс `m_`)
- Source-aware обработка: определяет тип файла (`.src` vs `.cppt`) по расширению

CLI-аргументы:

```
trust-dap [options]

By default runs in interactive mode (stdin/stdout).

Options:
  --help              Show this help
  server[=<port>]     TCP server mode on given port (default: 4711)
  --project-dir <path>  Project working directory (default: cwd)
  --gdb <path>        Path to gdb binary (default: gdb)
```

Обработка DAP-команд:

1. `initialize` — ответить с capabilities (configurationDone, breakpointLocations, functionBreakpoints, stepIn, stepOut, disassembly)
2. `launch` — загрузить embedded source map из ELF через `SourceMapReader::fromElf()`, сохранить пути sourceFile/cppFile/targetFile, создать target, запустить процесс, запустить поток pollEvents()
3. `configurationDone` — только ответить
4. `setBreakpoints` — source-aware: `.src` брейкпоинты транслируются через SourceMapReader, `.cppt` — напрямую GDB
5. `breakpointLocations` — вернуть строки файла с учётом маппинга
6. `stackTrace` — двухоконная отладка (см. ниже)
7. `scopes` — вернуть scope "Local"
8. `variables` — вернуть список переменных через `GdbDebug::GetVariables()` с трансляцией имён C++ → trust
9. `continue`, `next`, `stepIn`, `stepOut` — управление выполнением
10. `threads` — список тредов
11. `setExceptionBreakpoints` — заглушка
12. `disconnect` — остановить поток pollEvents, завершить

**Embedded source map:**
Source map data встраивается в ELF-секцию `.debug_trust_map` на этапе компиляции C++.
trust-dap читает эту секцию через `SourceMapReader::fromElf()`, парся ELF-заголовок
и таблицу секций. Внешние `.map` файлы не используются.

**Поток pollEvents:**

- Запускается в отдельном потоке после launch
- Каждые 500ms вызывает `GdbDebug::WaitForEvent()`
- При `Stop` — отправляет DAP-событие `stopped` с причиной (breakpoint/step/exception)
- При `Exit` — отправляет `exited` + `terminated`

#### Двухоконная отладка (sourceKind)

trust-dap поддерживает отладку одновременно в двух окнах: `.src` (trust-lang исходный код) и `.cppt` (сгенерированный C++ код).
Переключение между окнами происходит через команду `Trust: Open Generated C++ File` (или кнопку на панели отладки).

Механизм работы `handleStackTrace`:
Запрос `stackTrace` принимает опциональный параметр `sourceKind`:

- `sourceKind: "src"` (по умолчанию) — стек-трейнс показывает trust-lang файлы (`.src`) с транслированными номерами строк.
- `sourceKind: "cpp"` — стек-трейнс показывает C++ файлы (`.cppt`) с оригинальными номерами строк от GDB без трансляции.

Механизм работы `setBreakpoints`:

- В `.src` файлах — точка останова транслируется через `ReaderFile::getMapTrustToCpp()` в соответствующую позицию C++ файла.
- В `.cppt` файлах — точка останова передаётся напрямую в GDB без трансляции.

Механизм работы `handleVariables`:
Имена переменных, полученные от GDB (C++ names), транслируются в trust-lang имена через `ReaderFile::getTrustName()`.

#### Обработка launch-запроса

DAP-запрос `launch` принимает следующие поля (передаются из VSCode конфигурации):

- `sourceFile` — путь к исходному `.src` файлу (текущий файл в VSCode)
- `cppFile` — путь к сгенерированному `.cppt` файлу
- `targetFile` — путь к скомпилированному ELF бинарю
- `gdbPath` — путь к GDB (опционально, переопределяет настройку расширения)

Все пути должны быть переданы через DAP-запрос, а не через CLI-аргументы.
CLI-аргументы `trust-dap` принимает только `--project-dir`

---

### 6. Build pipeline (VSCode extension)

VSCode extension выполняет сборку автоматически при запуске отладки:

1. **Транспиляция** — вызов trust-lang компилятора
2. **Компиляция C++** — вызов C++ компилятора
3. **Запуск trust-dap** — запускает DAP сервер; пути к файлам передаются через DAP-запрос `launch`

Сборка выполняется в `resolveDebugConfiguration` с отображением прогресса через `withProgress`.
Временные файлы (`.cppt`, ELF) создаются в каталоге из настроек vscode плагина (по умолчанию `.trust` в корне проекта).

---

### 7. VSCode extension (`include/vscode/extensions/trust-lang/`)

Расширение VSCode, регистрирующее:

- **Тип файла** `.src` — trust-lang исходный код, syntax highlighting
- **Debug adapter** — `trust-dap` с аргументами из настроек
- **Build task** — `trust: build and debug`, вызываемый по F5
- **Breakpoints** — поддержка SetBreakpoints по F9
- **Контекстное меню** — команда `Trust: Open Generated C++ File`
- **Pre-launch build** — автоматическая транспиляция и компиляция при запуске отладки

Поля настроек (`trust.*`): `compilerPath`, `cppCompilerPath`, `cppCompilerOptions`, `tempDir`, `dapPath`, `gdbPath`, `traceDAP`.

---

### Схема взаимодействия

```
VSCode → resolveDebugConfiguration → withProgress(transpile → compile) → trust-dap → GdbDebug → GDB/MI → process (ELF with embedded .debug_trust_map)
```

---

### Тестовые утилиты

| Цель | Описание | Зависит от GDB? |
|------|----------|-----------------|
| `simple_transpiler` | Транспилирует `.src` → `.cppt` + embedded source map (msgpack) | Нет |
| `gdb_main` | Интеграционный тест GdbDebug (таргет, брейкпоинты, StepOver, переменные) | Да |
| `gdb_debuggee` | Вспомогательный ELF-файл (debuggee) | Нет |
| `transpile_test` | Модульный тест transpiler'а `trust::SourceMapReader` | Нет |
| `dap_handler_test` | Unit-тесты DapHandler | Нет |
| `dap_handler_integration` | Интеграционные тесты с real ELF | Нет (требуется prepare_test_data) |

Запуск: `cmake --build . --target run_<name>`. Агрегирующая цель — `run_tests`.
Тестовый `.src` файл: `test/unit/debug/simple_example.src`.

---

### Сборка

Библиотека `debug_lib` (static): исходники `gdb_debug.cpp`, `dap_transport.cpp`, `dap_handler.cpp`. Зависимости: `diag_lib` (PUBLIC), `msgpack-c-static` (PRIVATE), `trust_include_dirs` (PUBLIC).

Исполняемый файл `trust-dap`: исходник `trust_dap.cpp`. Зависимости: `debug_lib` (PRIVATE), `nlohmann_json::nlohmann_json` (PRIVATE).

---

### Зависимости

- **libmsgpack-c** — бинарная сериализация mapping data
- **nlohmann_json** — только DAP-сервер (JSON-RPC)
- **trust::SourceMapReader** (`diag_lib`) — source-маппинг (не зависит от отладчика)
- **zlib** — сжатие/распаковка source map данных
