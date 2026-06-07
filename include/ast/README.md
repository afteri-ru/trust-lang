# AST — Abstract Syntax Tree

## Назначение

Универсальные типы для представления последовательности токенов (Sequence) и иерархии узлов AST. Обеспечивает единый формат данных для передачи между этапами конвейера: Lexer → MMProcessor → ParserAST.

## Особенности реализации

- **Token/Sequence** — `SyntaxToken` как `std::variant<Lexeme, AstNodePtr>`, объединяющий лексемы лексера и узлы AST в единый тип последовательности (`SyntaxSeq = std::vector<SyntaxToken>`).
- **Иерархия AST** — `AstNodeBase` (kind, text, range) → `AstNodeAttr` (kind, text, range, attrs, docs) → специализированные узлы: IdentName, Literal, Binary, CallExpr, Scope.
- **Система атрибутов** — `AttrId` (`uint32_t`) содержит битовую маску: bits 0–29 = индекс в `AttrPool`, bit 30 = встроенный/пользовательский, bit 31 = установлен вручную/автоматически. Регистрация и хранение — в `AttrPool` (без `AttrPoolView`), разбор `@[...]` — в `attr_parser.hpp`. `parse_attr(ctx, range, name, params)` только ищет атрибут в пуле; регистрация выполняется отдельно (`register_attr`/`register_builtin_attr`).
- **Поток данных** — последовательность проходит через конвейер: Flex/Lexer → Sequence → MMProcessor (трансформация) → Sequence → ParserAST.
- **God-class устранён** — старый универсальный `AstNode` заменён на иерархию с распределением полей по специализированным классам.