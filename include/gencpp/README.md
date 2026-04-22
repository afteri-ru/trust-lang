# gencpp — Транспайлер

Компонент выполняет семантический анализ, оптимизацию AST и генерацию C++ кода.

## Назначение

- Семантический анализ AST (проверка типов, symbol table)
- AST-оптимизации: constant folding, dead code elimination, inline expansion
- Генерация C++ кода (visitor-паттерн)

## Состав

- **AST-ноды** — Expr, Stmt, Decl и их производные
- **Semantic Analyzer** — проверка типов, разрешение имён
- **Symbol Table** — хранение и поиск объявлений
- **Optimizer** — constant folding, DCE, inline expansion
- **C++ Generator** — visitor → C++ код
- **C++ Module** — `trust.cppm` для C++20 module usage

## Тестирование

Для тестирования используется упрощённый текстовый формат представления AST. Описание формата, типов узлов и примеры — в `tests/gencpp/README.md`.