# Trust Debug System Architecture

## Overview

Trust Debug System — это система отладки для Trust-языка, который транслируется в C++. Система позволяет отлаживать Trust-код напрямую, используя VSCode через протокол DAP (Debug Adapter Protocol).

## Компоненты

### 1. Trust Compiler (trust-compiler)

Транслирует Trust-код в C++ и генерирует mapping data.

**Вход:** `.src` файл
**Выход:**
- `.cpp` файл с транслированным кодом
- Встроенная ELF секция `.trust_map`
- Внешний `map.json` файл

**Формат Trust:**
```trust
create x = 10          // Объявление переменной
x = y + 5              // Присваивание
call printf("%d", x)   // Вызов функции
```

### 2. Source Mapper (trust_mapper)

Загружает mapping data из:
- ELF секции `.trust_map` (предпочтительно)
- Внешнего JSON файла `map.json` (fallback)

Предоставляет:
- `trustToCpp()` — маппинг Trust строк → C++ строки
- `cppToTrust()` — маппинг C++ строк → Trust строки
- `getTrustVars()` — маппинг C++ переменных → Trust переменные

### 3. GDB Bridge (gdb_bridge)

Управление GDB через MI2 интерфейс:
- Запуск/остановка GDB
- Установка/удаление breakpoint'ов
- Выполнение (run, step, next, continue)
- Получение stack frames и переменных

### 4. DAP Server (trust-dap)

Реализация Debug Adapter Protocol:
- stdin/stdout JSON-RPC коммуникация
- Обработка DAP запросов от VSCode
- Трансляция DAP команд в GDB MI2
- Маппинг Trust ↔ C++ через TrustMapper

**Поддерживаемые команды:**
- initialize, launch, configurationDone
- setBreakpoints, threads, stackTrace
- scopes, variables, next, stepIn, stepOut
- continue, evaluate, disconnect

### 5. VSCode Extension

Локальное расширение для VSCode:
- Тип отладки: "trust"
- Запускает trust-dap сервер
- Конфигурация: program, sourceMap

## Поток данных

```
VSCode                    trust-dap                    GDB
  |                         |                          |
  |-- initialize ---------> |                          |
  |<-- capabilities ------- |                          |
  |                         |                          |
  |-- launch -------------> |                          |
  |                         |-- start GDB ------------>|
  |<-- success ------------ |                          |
  |                         |                          |
  |-- setBreakpoints ------> |                          |
  |                         |-- -break-insert -------->|
  |<-- breakpoints --------- |<-- response -------------|
  |                         |                          |
  |-- configurationDone ---> |                          |
  |                         |-- -exec-run ------------>|
  |<-- initialized --------- |                          |
  |<-- stopped ------------- |<-- *stopped event ------|
  |                         |                          |
  |-- stackTrace ----------> |                          |
  |                         |-- -stack-list-frames --->|
  |<-- stackFrames --------- |<-- response -------------|
  |  (mapped to Trust)      |                          |
  |                         |                          |
  |-- continue ------------> |                          |
  |                         |-- -exec-continue ------->|
  |<-- allThreadsContinued - |                          |
```

## Структура проекта

```
include/dap/              # Заголовки + cmake + vscode-extension
src/dap/                  # Исходники DAP сервера
tests/dap/                # Тесты + compiler + example.src
```

## Формат Source Map

```json
{
  "version": 1,
  "sources": [{
     "trust_file": "path/to/file.src",
    "cpp_file": "path/to/file.cpp",
    "mappings": [{
      "trust_line": 1,
      "cpp_line": 5,
      "trust_vars": ["x", "y"],
      "cpp_vars": ["x", "y"]
    }]
  }]
}