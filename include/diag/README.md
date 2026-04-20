# trust — C++20 Diagnostic & Options Library

Библиотека для управления диагностическими сообщениями и опциями компилятора/анализатора.

## Архитектура

Context — фасад, объединяющий три компонента:

- **DiagnosticEngine** — вывод диагностики с форматированием, подсчётом ошибок/предупреждений и визуальным подчёркиванием диапазонов.
- **Options** — система именованных опций, определяемых через X-макросы. Поддерживает парсинг CLI аргументов и стековый push/pop для временных изменений.
- **Source Manager** — хранение исходных файлов, конвертация offset ↔ line:column с LRU-кешем.

Типы локаций (`SourceIdx`, `SourceLoc`, `SourceRange`) находятся в `location.hpp` и используются всеми компонентами.

Context владеет DiagnosticEngine через unique_ptr, Options через optional, и Source Manager через вектор FileEntry.

## Компоненты

### Location (`location.hpp`)

Типобезопасные примитивы для отслеживания позиций в исходном коде:

| Тип | Размер | Описание |
|-----|--------|----------|
| `SourceIdx` | `int` | Сильно типизированный индекс файла. Нельзя случайно передать обычный int вместо индекса источника. |
| `SourceLoc` | 4 байта | Упакованная позиция: `(source_idx + 1) << 22 | offset`. Биты: младшие 22 бита — offset (макс. ~4 МБ), старшие — source_idx + 1. Константы: `OFFSET_BITS = 22`, `SOURCE_SHIFT = 22`, `MAX_OFFSET = (1 << 22) - 1`. |
| `SourceRange` | 8 байт | Диапазон `{begin, end}`. Если `begin == end` — точечная позиция. |
| `ParserError` | — | Исключение с прикреплённым SourceLoc. |

### DiagnosticEngine (`diag.hpp`, `diag.cpp`)

Движок диагностики с пятью уровнями серьёзности:

```
Remark < Note < Warning < Error < Fatal
```

Возможности:
- Форматирование через `std::format`
- Привязка к исходному коду: `file:line:column: severity: message`
- Визуальное подчёркивание диапазона `^~~~`
- Подсчёт ошибок и предупреждений
- Перенаправляемый вывод (`std::ostream*`)

### Options (`options.hpp`, `options.cpp`)

Система именованных опций, определяемых на этапе компиляции через X-макросы:

```cpp
#define OPTIONS_LIST(M) \
    M(UnusedVar,   "unused-var",  Severity::Warning) \
    M(Deprecated,  "deprecated",  Severity::Warning) \
    M(All,         "all",         Severity::Warning)
```

Ключевые особенности:
- Опечатки в `OptKind` — ошибка компиляции (enum, а не строка)
- Парсинг CLI: `-Wname` / `-Wname=value`
- Значения: `fatal`, `error`, `warning`, `ignore`, `remark`, `note`
- Стековый `push/pop` для временных изменений (скоупированные настройки)

Реализация push/pop:
```
base:  {UnusedVar=Warning, Deprecated=Warning}
push → delta1
  set(UnusedVar, Error) → delta1 stores {UnusedVar→Warning}
push → delta2
  set(UnusedVar, Fatal) → delta2 stores {UnusedVar→Error}
pop  → restore delta2 → UnusedVar = Error
pop  → restore delta1 → UnusedVar = Warning
```

### Context (`context.hpp`, `context.cpp`)

Фасад, объединяющий все компоненты:
- Управление источниками (`add_source`, `load_file`)
- Доступ к диагностике и опциям
- Конвертация позиций (`loc_from_line`, `line_column`)
- LRU-кеш для `line_column()` — кэшируется последний запрошенный `SourceLoc`

## Использование

```cpp
#include "context.hpp"

using namespace trust;

int main() {
    Context ctx;

    // Добавить исходный код
    SourceIdx src = ctx.add_source("demo.cpp", "int x = foo();\n");

    // Настроить вывод
    ctx.diag().setOutput(&std::cout);
    ctx.diag().setMinSeverity(Severity::Remark);

    // Зарегистрировать опции
    ctx.opts().add_option(OptKind::UnusedVar);

    // Сообщить об ошибке
    auto loc = ctx.loc_from_line(src, 1);
    ctx.diag().report(loc, Severity::Error, "unknown identifier '{}'", "foo");

    // Парсинг аргументов командной строки
    std::span<char*> args(argv, argc);
    auto remaining = ctx.opts().parse_argv(args.subspan(1));

    return 0;
}
```

## Сборка

```bash
mkdir build && cd build
cmake ..
cmake --build .
./unittest   # Запуск тестов
./demo       # Демонстрация
```
