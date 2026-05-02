# Trust-lang Debug System Architecture

## Overview

Система отладки для trust-lang — языка, транслируемого в C++.
Позволяет отлаживать trust-lang код напрямую через DAP-протокол (VSCode),
в том числе навигировать по исходному коду и устанавливать значения переменных
как в C++ файле, так и в оригинальном trust-исходнике.

## Namespace

Весь код модуля отладки находится в пространстве имён `trust`.

## Принципы

Единственный класс `TrustSource` отвечает за всё, что связано с source-маппингом:
хранение пар `(trust_file, cpp_file)`, маппинг строк и переменных, сериализацию
(msgpack), загрузку из ELF-секции `.debug_trust_map` или внешнего `.map` файла (fallback),
а также генерацию embed-кода. TrustSource — единая точка входа для transpiler'а (добавление данных)
и debugger'а (чтение/трансляция).

---

## Компоненты

### 1. TrustSource (`trust_source.h/.cpp`)

Единый класс хранения, загрузки, сериализации и трансляции source-маппинга.

**Структура:**
- `FilePairEntry` — пара файлов `(trust_file, cpp_file)` + маппинги
- `trustToCppIndex` — `std::map<LineNumber, LineNumber>` (trust_line → cpp_line) для O(log n) nearest-neighbour поиска trust→cpp
- `trustVarMapping` — `std::map<std::string, VarMapValue>` (trust_var → {cpp_var, {trust_line, cpp_line}})
- `cppToTrustVar` — `std::unordered_multimap<std::string, std::string>` (cpp_var → [trust_var...])
- `cpp_line_inserted` — количество строк, добавляемое к cpp_line при вычислениях номера строки в C++ файле
- `base_directory_` — базовый каталог для нормализации trust-путей
- `cpp_directory_` — базовый каталог для нормализации cpp-путей

**Нормализация путей:**
Единый приватный метод `normalizePath(path, baseDir)`:
- Если путь относительный или `baseDir` пуст — возвращает `lexically_normal()` как есть
- Если путь абсолютный — удаляет префикс `baseDir + "/"`, оставляя относительную часть
- Если путь не начинается с `baseDir` — ошибка `FAULT`
- Для trust-путей `baseDir` = `base_directory_`
- Для cpp-путей `baseDir` = `cpp_directory_`

Конструктор `TrustSource(basePath, cppPath)`:
- При пустом `cppPath`: `cpp_directory_ = base_directory_ + "/.trust/"` (по умолчанию)
- При непустом `cppPath`: нормализуется через `absolute().lexically_normal()` по общим правилам

**API — добавление данных (для transpiler'а):**
```cpp
const FilePairEntry *setFilePair(std::string_view trustFile, std::string_view cppFile);
bool addLineMapping(LineNumber trustLine, LineNumber cppLine);
bool addVarMapping(LineNumber trustLine, LineNumber cppLine,
                   std::string_view trustVar, std::string_view cppVar);
void setCppLineInserted(size_t n);
```

**API — утилиты для строк:**
```cpp
static size_t new_line_count(std::string_view s);            // подсчёт '\n' в строке
void include_append(const std::vector<std::string> &files);  // сумма '\n' в массиве → cpp_line_inserted
```

**API — трансляция строк и переменных:**
```cpp
std::optional<LineMapValue> nearestTrustToCpp(std::string_view trustFile, LineNumber trustLine) const;
std::optional<LineMapValue> nearestCppToTrust(std::string_view cppFile, LineNumber cppLine) const;
std::optional<VarMapInfo> getCppVar(std::string_view trustFile, LineNumber trustLine, std::string_view trustVar) const;
std::optional<VarMapInfo> getTrustVar(std::string_view cppFile, LineNumber cppLine, std::string_view cppVar) const;
```

**API — сериализация (msgpack):**
```cpp
static std::vector<unsigned char> pack(const TrustSource &ts);
static bool unpack(TrustSource &ts, const unsigned char *data, size_t size,
                   std::string *error = nullptr);
```

**API — загрузка (фабричный метод):**
```cpp
static std::unique_ptr<const TrustSource> LoadFromBinary(
    const std::string &binaryPath,
    const std::string &mapPath = "");
```
- Пытается прочитать секцию `.debug_trust_map` из ELF-бинарника
- Если не найдена — читает внешний `.map` файл (fallback)
- Если ничего не найдено — возвращает nullptr

**API — утилиты:**
```cpp
static std::string generateEmbeddedMapCode(const std::vector<unsigned char> &mapData);
static bool writeMapFile(const std::vector<unsigned char> &mapData, const std::string &path);
void clear();
```

**API — доступ к данным (для тестов и отладчика):**
```cpp
const FilePairEntry *currentFilePair() const;
const std::vector<FilePairEntry> &entries() const;
```

**Формат msgpack (компактный, без имён полей, с версией):**
```
array 3:
  [0] uint32 – version_major (TRUST_VERSION_MAJOR)
  [1] uint32 – version_minor (TRUST_VERSION_MINOR)
  [2] array  – file_entries:
    file_entry = array 5:
      [0] str  – trust_file (нормализованный)
      [1] str  – cpp_file (нормализованный)
      [2] uint – cpp_line_inserted
      [3] array – line_pairs: [[trust_line, cpp_line], ...]
      [4] array – var_renames: [[trust_line, cpp_line, trust_var, cpp_var], ...] или nil
```

Версия проекта (`major, minor` из VERSION) хранится в начале для контроля формата.
При unpack версия читается, но не проверяется.

**Генерация embed-кода:**
Эмбед-код генерируется с названием секции `.debug_trust_map` и переменной `debug_trust_map_data`.

**Трансляция строк:** O(log n) для nearest-neighbour через `std::map::upper_bound`/`lower_bound`.

**Трансляция переменных:** `getCppVar` — прямой поиск по ключу trust_var в `trustVarMapping`,
`getTrustVar` — обратный поиск через `cppToTrustVar` (exact match по cpp_line, fallback nearest).

### 2. TrustDebug (`trust_debug.h/.cpp`)

Центральный класс взаимодействия с LLDB API.

**Возможности:**
- Создание таргета, установка брейкпоинтов
- Запуск процесса, StepOver, Continue
- Чтение stdout процесса
- Доступ к текущему фрейму, треду, процессу

**Source-маппинг:**
- `BreakpointBySource(file, line)` — транслирует trust → C++ через source, если он задан
- `GetVariablesBySource()` — возвращает имена переменных с трансляцией
- `GetValueBySource(name)` / `SetValueBySource(name, value)` — доступ к переменным с трансляцией
- Если source не задан — все операции выполняются в режиме 1:1 (имена передаются как есть)

**Настройка source'а:**
```cpp
TrustDebug dbg;
auto source = TrustSource::LoadFromBinary("binary.elf");
dbg.SetSource(std::move(source));
```

**Config:**
```cpp
struct Config {
    std::string lldbServerPath;   // путь к lldb-server (пусто = системный)
    bool asyncMode;               // асинхронный режим LLDB
    lldb::LaunchFlags launchFlags;
};
```

### 3. Транспортный слой (`dap_transport.h/.cpp`)

Отвечает за DAP-транспорт: чтение/запись пакетов в формате Content-Length, TCP-сервер,
вспомогательные функции для формирования DAP-сообщений.

**Классы:**

```cpp
class DapTransport {                // абстрактный базовый класс
    virtual std::string readPacket() = 0;
    virtual void send(const std::string &payload) = 0;
};

class StdioTransport : public DapTransport { // stdin/stdout (interactive-режим)
    std::string readPacket() override;
    void send(const std::string &payload) override;
};

class TcpTransport : public DapTransport {   // TCP-сокет (server-режим)
    TcpTransport(int fd);                    // принимает сокет клиента
    std::string readPacket() override;
    void send(const std::string &payload) override;
};
```

**DAP Protocol helpers (free functions):**

```cpp
int nextDapSeq();                              // инкрементальный seq
json readDapPacket(DapTransport &transport);    // readPacket + JSON parse
void sendDapResponse(...);                      // формирование response
void sendDapEvent(...);                         // формирование event
void sendDapOutput(...);                        // output-событие
void sendBreakpointEvent(...);                  // breakpoint-событие
```

**TCP server helpers:**

```cpp
int createTcpServer(int port);     // socket → bind → listen (INADDR_ANY)
int acceptConnection(int serverFd); // accept одного клиента
```

**CLI parsing:**

```cpp
struct DapOptions { int port; bool help; std::string projectDir; std::string lldbServerPath; };
DapOptions parseDapOptions(int argc, const char *argv[]);
void printUsage(const char *prog);
```

**Константы:**

```cpp
constexpr int DAP_DEFAULT_PORT = 4711;  // стандартный порт DAP
```

### 4. trust-dap (`trust_dap.cpp`)

DAP-сервер для отладки trust-lang. Загружает TrustSource, создаёт TrustDebug,
готовит Target и запускает процесс. Поддерживает interactive (stdin/stdout)
и TCP-server режимы.

**CLI-аргументы:**
```
trust-dap [--help]
          [server[=<port>]]
          [--project-dir <path>]
          [--lldb-server <path>]
```

Все параметры опциональные.

По умолчанию работает в interactive-режиме (stdin/stdout). Если указан `server`,
работает как TCP-сервер на стандартном порту DAP (4711) или на указанном порту.

**Пути к файлам (source, cpp, target, map)** не передаются через CLI — они приходят
стандартным способом через аргументы DAP-запроса `launch`:
```json
{
  "command": "launch",
  "arguments": {
    "sourceFile": "/path/to/file.src",
    "cppFile": "/path/to/file.cpp",
    "targetFile": "/path/to/binary.elf",
    "mapFile": "/path/to/file.map"
  }
}
```

**DAP-последовательность (стандартное поведение):**
1. `initialize` — ответить с capabilities
2. `launch` — создать target + запустить процесс (из аргументов launch)
3. `setBreakpoints` — применить немедленно (target уже существует)
4. `configurationDone` — только ответить, без создания target
5. `continue`/`next`/`stepIn`/`stepOut` — управление выполнением

**Обрабатываемые DAP-команды:**
- `initialize`, `launch`, `configurationDone` — конфигурация
- `setBreakpoints`, `breakpointLocations` — точки останова
- `continue`, `next`, `stepIn`, `stepOut` — управление выполнением
- `stackTrace`, `scopes`, `variables` — состояние отладки
- `getCppFile` — custom request: возвращает cppFile/cppLine последнего фрейма
- `disassembly` — возвращает строки C++ файла как псевдо-инструкции
- `disconnect` — завершение

### 4. Build pipeline (VSCode task)

VSCode task `trust: build and debug` выполняет:

1. **Транспиляция** — вызов trust-lang компилятора, указанного в настройках плагина, превращает `.src` в `.cpp`
2. **Компиляция C++** — вызов компилятора (например `clang++-22`), указанного в настройках плагина, собирает `.cpp` в ELF
3. **Запуск trust-dap** — запускает DAP сервер без аргументов пути (пути передаются через DAP-запрос `launch`)

Временные файлы (`.cpp`, ELF) создаются в каталоге, указанном в настройках
(по умолчанию `.trust` в корне проекта).

### 5. VSCode extension (`include/vscode/extensions/trust-lang/`)

Расширение VSCode, регистрирующее:

- **Тип файла** `.src` — trust-lang исходный код, syntax highlighting
- **Debug adapter** — `trust-dap` с аргументами из настроек
- **Build task** — `trust: build and debug`, вызываемый по F5
- **Breakpoints** — поддержка SetBreakpoints по F9
- **Контекстное меню** — команда `Trust: Open Generated C++ File` открывает C++ файл, соответствующий текущему .src
  - Если debug-сессия активна — custom request `getCppFile`
  - Если последний билд доступен — открытие C++ файла с соответствующей строкой через `lastBuildResult`
  - Fallback: открытие C++ файла с первой строки
  - Кнопка "Show Disassembly" в панели отладки отображает C++ код вместо ассемблера

**Поля настроек (`trust-lang.*`):**
- `compilerPath` — путь к trust-lang компилятору
- `compilerArgs` — аргументы trust-lang компилятора
- `cppCompiler` — имя C++ компилятора (без пути, берётся из PATH, например `clang++-22`)
- `tempDir` — каталог для временных файлов (по умолчанию `.trust`)
- `dapServerPath` — путь к `trust-dap` серверу
- `lldbServerPath` — путь к `lldb-server`

## Схема взаимодействия

```
VSCode  ←→  [F5] → build task (transpile → compile)
                    ↓
         ←→  trust-dap  ←→  TrustDebug  ←→  LLDB API  ←→  process
                             │
                    TrustSource (загрузка/сериализация/трансляция)
```

## Типы данных

```cpp
using LineNumber = int;
using LinePair = std::pair<LineNumber, LineNumber>;
using LineMapValue = std::pair<std::string, LineNumber>;  // <file, line>

struct VarMapValue {
    std::string cppVar;
    LinePair lines;  // {trust_line, cpp_line}
};

struct VarMapInfo {
    std::pair<std::string, std::string> files; // {trust_file, cpp_file}
    std::pair<std::string, std::string> vars;  // {trust_var, cpp_var}
    LinePair lines;                            // {trust_line, cpp_line}
};

struct FilePairEntry {
    std::pair<std::string, std::string> files;           // {trust_file, cpp_file}
    std::map<LineNumber, LineNumber> trustToCppIndex;    // trust_line → cpp_line
    std::map<std::string, VarMapValue> trustVarMapping;  // trust_var → {cpp_var, lines}
    std::unordered_multimap<std::string, std::string> cppToTrustVar; // cpp_var → trust_var
    size_t cpp_line_inserted = 0;
};
```

## Portability

- `TrustSource` не зависит от LLDB — может использоваться отдельно
- Msgpack формат — единственный формат сериализации
- Чтение ELF-секции `.debug_trust_map` — приватный статический метод, при необходимости расширяется на PE/Mach-O

## Тестовые утилиты и их запуск

### Исполняемые файлы (`test/unit/debug/CMakeLists.txt`)

| Цель | Описание | Зависит от LLDB? |
|------|----------|------------------|
| `simple_transpiler` | Транспилирует `.src` → `.cpp` + embedded source map (msgpack). Принимает путь к `.src` файлу и опционально путь к временному каталогу для .cpp файлов. | Нет |
| `lldb_main` | Интеграционный тест TrustDebug без DAP: создаёт таргет, брейкпоинты, StepOver, читает переменные. | Да |
| `simple_integration` | Полный цикл: transpile → compile → debug через LLDB. | Да |
| `lldb_debuggee` | Вспомогательный ELF-файл, используемый как debuggee (не тест сам по себе). | Да |

### Запуск

Каждый исполняемый файл имеет custom target `run_<name>` в `test/lit/CMakeLists.txt`.
Запуск через `cmake --build . --target run_<name>`. Все цели собраны в `run_debug_tests`
(LLDB-зависимые — только при `LLDB_FOUND=TRUE`).

Агрегирующая цель верхнего уровня — `run_tests` в корневом `CMakeLists.txt`:
```
cmake --build . --target run_tests              # unit + lit + debug
```

Тестовый `.src` файл: `test/unit/debug/simple_example.src`.

## Зависимости

- **libmsgpack-c** — бинарная сериализация mapping data
- **LLDB API** — только TrustDebug и тесты (TrustSource не зависит от LLDB)