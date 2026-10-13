# MEMORY.md

> scope: include/ast/slots
> role: persistent-memory
> last_reviewed: 2026-11-04
> review_period: 30
> max_size: 5000

## Architecture

`node_slots_x.hpp` — единственная класс-таблица состава узлов (`AST_SLOT_CLASSES` + строки
`X(shape, field, walk, filter)`); `slot_engine.hpp` — `AstSlotVisitor`, эмиссия строк и
`AST_SLOT_METHOD`. `AstNodeBase::forEachSlot` (генерируется `AST_SLOT_METHOD`) — единственный источник
обхода/клона/ремапа.

## Facts and invariants

- **⚠ Состав узла — ТОЛЬКО строка класс-таблицы** (`ast/slots/node_slots_x.hpp`,
  `X(shape, field, walk, filter)`): `forEachSlot` генерируется `AST_SLOT_METHOD(BASE, ROWS)` (слоты базы
  первыми, `m_trust` — в конце через `AstNodeBase`). Политика, зависящая от KIND (`TypeSet` — лист,
  `TypeDecl/NameDecl` — без LHS, дедуп `m_indices` с `m_right`), — ДАННЫЕ строки (`SkipKinds`/
  `ListDeduped`), а не `if` в обходе; пропущенный слот ловит CTest `ast_slots_guard` (негативная фикстура
  `test/guards/fixture_ast_slots`).
- **⚠ Невладеющие ссылки ремапятся только visitor'ом** (`NonOwningRemapper`, `ast/clone.cpp`): состав —
  строки `NonOwn*` класс-таблицы.
- **Перечень классов ПРОИЗВОДИТСЯ из `AST_SLOT_CLASSES`;** `static_assert` в `token.hpp` проверяет
  отсутствие мёртвых классов и полноту kind→класс.

## Relations

- `ast` (kind-таблица `PARSER_TOKEN_KINDS`), `ast/nodes`, `ast/clone.cpp`, `cmake/check_ast_slots.cmake`.
