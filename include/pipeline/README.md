# pipeline — Transpiler Pipeline

Библиотека `pipeline_lib` — точка входа для компиляции файлов из терминала.

## Ответственность

- Драйвер полного pipeline компиляции: read → lex → parse → analyze → optimize → codegen
- Парсинг аргументов (входной файл, выходной файл, -h, --version)
- Запись результата в выходной файл или stdout
- Обработка ошибок через `diag`

## Зависимости

- `diag` — диагностика и опции
- `parser` — токенизация и парсинг

## Использование

```bash
trust <input.src> [-o output.cppt]
trust --version
trust -h
