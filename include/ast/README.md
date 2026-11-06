# AST - Abstract Syntax Tree

## Назначение

Типы для представления последовательности токенов (`Sequence`) и иерархии узлов AST — единый формат
данных между этапами конвейера: Lexer → Term → Macro/Parser → `TermToAstConverter` → AST.

## Архитектура

- **AST строится из дерева `Term`** (компонент `syntax`); Term в узлах не хранится. Текст узла —
  mixin `WithText`/`HasText` (невладеющий view в пул строк `StringInterner`); нормализацию имени
  выполняет ТОЛЬКО создание name-node через `assignNameFromRaw`.
- **Иерархия:** `AstNodeBase` (kind, `MapperRange`) → `AstNodeAttr` (+attrs) → специализированные узлы.
  Система атрибутов вынесена в компонент `attrs`.
- **Единая диспетчеризация (kind-dispatch):** `include/ast/kind_dispatch.hpp` выводит перечень из
  `PARSER_TOKEN_KINDS` и задаёт `kindCategory`, `hasAllKindHandlers<C>()`, `dispatchEmit`, `faultUnexpectedKind`,
  `skipNode`. Новый kind обязан получить категорию.
- **Типизированный доступ без RTTI:** `is<T>()`/`as<T>()` по kind→класс из `PARSER_TOKEN_KINDS`;
  `dynamic_cast`/`static_pointer_cast` по AST запрещены.
- **Единый источник состава узла** — класс-таблица `ast/slots/node_slots_x.hpp` (генерация `forEachSlot`,
  обход/клон); поле-слот вне таблицы — ошибка (`test/unit/ast/slots_test.cpp`). Клон дерева — `clone.hpp` (каждый клон
  получает `origin`).

- **AST владеет синтаксисом, не семантикой формы:** порядок/`visibility`/инициализатор/`MapperRange` полей — AST; типы полей принадлежат `types` (R6).

## Документация

- `MEMORY.md` — публичные правила; `nodes/MEMORY.md`, `slots/MEMORY.md`, `convert/MEMORY.md` — детали.
