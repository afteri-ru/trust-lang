# cli — Command-Line Interface

CLI-утилита `trust` — точка входа для компиляции файлов из терминала.

## Ответственность

- Драйвер полного pipeline компиляции: read → lex → parse → analyze → optimize → codegen
- Парсинг CLI-аргументов (входной файл, выходной файл, -h, --version)
- Запись результата в выходной файл или stdout
- Обработка ошибок через `diag`

## Зависимости

- `diag` — диагностика и опции
- `parser` — токенизация и парсинг
- `gencpp` — AST, семантический анализ, оптимизации, генерация C++

## Использование

```bash
trust <input.trust> [-o output.cpp]
trust --version
trust -h