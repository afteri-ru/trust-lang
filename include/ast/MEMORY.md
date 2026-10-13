# MEMORY.md

> scope: include/ast (public API)
> role: persistent-memory
> last_reviewed: 2026-11-04
> review_period: 30
> max_size: 8000

## Architecture

Поток: Lexer → Term → Macro/Parser → `TermToAstConverter` → AST. `AstNodeBase` — kind + `MapperRange`;
Term не хранится. Текст — mixin `HasText` через `WithText<Base>`. **Две таблицы AST (ключи не совпадают):**
`PARSER_TOKEN_KINDS(T)` — ключ `kind` (kind→класс+категория кодогена); `AST_SLOT_CLASSES`
(`slots/node_slots_x.hpp`) — ключ `class` (состав/владение/обход). Публичный путь определения классов —
`ast_nodes.hpp` (части напрямую включать нельзя извне компонента). Граница «AST → производная форма» —
`ast/clone.hpp` (`cloneTree`/`cloneTrees`, `JumpTargetsT`), основа `ir::buildHir`.

Детали реализации — в internal-подкаталогах:

| internal-подкаталог | MEMORY | ответственность |
|---|---|---|
| `ast/nodes/` | `nodes/MEMORY.md` | определения классов узлов и их семантика (RecordDecl, CallExpr, Operatоры, ref-узлы) |
| `ast/slots/` | `slots/MEMORY.md` | класс-таблица слотов, `forEachSlot`, клон/ремап невладеющих ссылок |
| `ast/convert/` | `convert/MEMORY.md` | `TermToAstConverter`: правила `visit_*`, контекст-маркеры, trust-контракты |

## Facts and invariants

- **Сайт импорта в AST — `ModulePart{index, body, projection}`:** всё, что видно с сайта в части
  модуля, — ОДИН объект `SiteProjection` (отфильтрованный экспорт, namespace-алиасы, несовпавшие
  маски, protected-флаги). Владелец проекции — кэш загрузчика (`shared_ptr`), правило видимости —
  `types/names.hpp`.
- Узлы, попавшие в экспорт-набор модуля (`ExportEntry::node`), мутируются ТОЛЬКО по слотам;
  сам declaration-node не заменяется — контракт владения в `module_loader/MEMORY.md`.
- **⚠ Инвариант kind→класс ОБЯЗАТЕЛЕН:** узел с данным `Kind` обязан быть классом из `PARSER_TOKEN_KINDS`
  (иначе `is<T>()`/`as<T>()` — UB: класс выводится из kind, RTTI нет). Клон копирует по kind→классу.
- **⚠ trap (kind↔класс НЕ 1:1):** РАЗНЫЕ kinds — ОДИНАКОВЫЙ класс при той же структуре данных; отношение
  задаётся ВТОРЫМ полем `PARSER_TOKEN_KINDS`. Новый класс под новый kind ЗАПРЕЩЁН; перечень классов
  ПРОИЗВОДИТСЯ из `AST_SLOT_CLASSES` + `static_assert`.
- **⚠ default-конструктор любого узла даёт `kind = END`:** узел-замена («удалить директиву») обязан
  задавать kind явно, иначе неотличим от «не-узла» (кодоген падает в `dispatchEmit`).
- **⚠ Типы в AST: state — `QualType`, identity — `TypeId`.** Где state (const/inferred) обязан доехать
  до кодогена, поле узла хранит `QualType`; `TypeId` — для identity/ключей. Потеря state — дефект.
- **⚠ `text()` и `range()` независимы:** при макро-раскрытии spelling остаётся из определения, range
  переприсваивается на call-site/`{}`. Текст из range не выводить; правило имени (`:` TypeName, `^` кроме
  `"$^"`, `ReadOnly`) применяет ТОЛЬКО `assignNameFromRaw`.
- **⚠ Term НЕ хранится в AST:** конструктор извлекает range/текст/флаги. У manual/test-узла `range()`
  невалиден; `text()` есть только у `WithText`.
- **⚠ `ModuleNode` импорта не хранит своего тела:** `parts[].body` — разделяемый `shared_ptr` на тело
  реестра (конвертируется один раз); маски разбираются ОДИН раз; наборы экспорта/алиасов — разделяемый
  кэш.
- **⚠ `AstNodeBase::visibility` — target-neutral видимость** (`Public`/`Protected`/`Private`), ставит
  загрузчик; C++-маппинг — дело бэкенда, доступ проверяет семантика.
- **⚠ `AstNodeBase::module`** ставит ТОЛЬКО загрузчик и только объявлениям уровня модуля; читают
  семантика (видимость) и кодоген (C++-изоляция). Локальные/члены модуля не несут.
- **⚠ `IdentName::moduleRef` — маркер ссылки `\path::name`:** `ModuleIndex` уже загруженного модуля;
  `text()` — trust-имя ВНУТРИ модуля без модульного пути.
- **⚠ Слои представления:** `Term` — дерево разбора, `AstNode` — AST, `HirModule` — производная
  (клон+lowering). AST после `run()` неизменяема; кодоген/flow — HIR.
- **⚠ ref-вид: единая точка разбора — `ast/ref_syntax.hpp`:** `refKindFromAttrArgs`, `refKindOfAttr`,
  `refKindOfTypeNode`, `refKindOfTypeSpec`. НЕ дублировать разбор по месту.
- **Типизированный доступ без RTTI:** `is<T>()`/`as<T>()` через kind→класс и `std::is_base_of_v`;
  `as<T>()` при неверном kind — `FAULT`. `dynamic_cast`/`static_pointer_cast` по AST ЗАПРЕЩЕНЫ.
  Контракты объявления — `trustContracts()`/`hasTrustProperty()` (EXPECT/FAULT при инородном элементе).
- **`IdentName::resolvedDecl`/`ArgNode::resolvedDecl`** — невладеющие ссылки на объявление (заполняет
  семантика); переживают pop скоупов и используются пост-проходными анализами. Для типов/полей/
  неразрешённых имён — nullptr (не считать признаком ошибки).
- **`visit_<Kind>` у потребителей** — осознанная compile-time проверка полноты, НЕ дублирование; no-op
  в default-базу выносить нельзя.
- **⚠ `origin`:** у HIR-клона — исходный узел AST (nullptr у созданных lowering'ом); `NodeId` не вводится.
- **⚠ ErrorExpr — error/recovery-заглушка («diagnose-then-replace»):** первый нашедший ошибку пасс
  сообщает её и заменяет под-узел; `m_original` вне `children()` (без каскада).

## Relations

- `attrs` (раскладка `AttrId`, правила unhandled/unknown), `ir` (клон+lowering, HIR), `types` (identity/state),
  `transpiler`/`semantic` (потребители).
