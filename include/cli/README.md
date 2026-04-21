# cli — Command-Line Interface

CLI-утилита `trust` — точка входа для компиляции файлов из терминала.

## Ответственность

- Парсинг CLI-аргументов (входной файл, выходной файл, -h, --version)
- Запуск pipeline: read → lex → parse → analyze → optimize → codegen
- Запись результата в выходной файл или stdout
- Обработка ошибок через `diag`

## Зависимости

- `diag` — диагностика и опции
- `parser` — токенизация и парсинг
- `gencpp` — AST, семантический анализ, генерация C++

## Использование

```bash
trust <input.trust> [-o output.cpp]
trust --version
trust -h