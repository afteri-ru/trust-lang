# optimizer — AST-оптимизации

Constant folding, dead code elimination, inline expansion на уровне AST перед генерацией кода.

## Ответственность

- Constant folding — вычисление постоянных выражений на этапе компиляции
- Dead code elimination — удаление недостижимого кода
- Inline expansion — встраивание вызовов небольших функций

## Зависимости

- `types` — система типов
- `gencpp` — AST-структуры