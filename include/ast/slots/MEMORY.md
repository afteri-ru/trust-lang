# MEMORY.md

> scope: include/ast/slots
> role: persistent-memory
> last_reviewed: 2026-11-13
> review_period: 30

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
  `ListDeduped`), а не `if` в обходе; пропущенный слот проверяет тест `test/unit/ast/slots_test.cpp`.
- **⚠ Невладеющие ссылки ремапятся только visitor'ом** (`NonOwningRemapper`, `ast/clone.cpp`): состав —
  строки `NonOwn*` класс-таблицы.
- **Агрегаты-структуры в строке — по образцу `CaseList`/`TargetList`:** владеемые узлы внутри
  `vector<Struct>` обходятся `AST_ROW_<shape>` (шаблон `CaseList` — `MatchStmt::MatchCase`,
  `TargetList` — `DestructureDecl::Target`, `OptRecordList` — `TemplateArg`); поля агрегата — часть строки-таблицы (они не слоты сами по себе).
- **Перечень классов ПРОИЗВОДИТСЯ из `AST_SLOT_CLASSES`;** `static_assert` в `token.hpp` проверяет
  отсутствие мёртвых классов и полноту kind→класс.

## Relations

- `ast` (kind-таблица `PARSER_TOKEN_KINDS`), `ast/nodes`, `ast/clone.cpp`.
