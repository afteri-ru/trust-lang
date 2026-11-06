# MEMORY.md

> scope: include/ast (public API)
> role: persistent-memory
> last_reviewed: 2026-11-13
> review_period: 30

## Architecture

Поток: Lexer → Term → Macro/Parser → `TermToAstConverter` → AST. `AstNodeBase` — kind + `MapperRange`; Term
не хранится, текст — mixin `WithText`. **Две таблицы AST (ключи не совпадают):** `PARSER_TOKEN_KINDS(T)` —
ключ `kind` (kind→класс+категория кодогена); `AST_SLOT_CLASSES` (`slots/node_slots_x.hpp`) — ключ `class`
(состав/владение/обход). Публичный путь определения классов — `ast_nodes.hpp` (части напрямую извне нельзя).
Граница «AST → производная форма» — `ast/clone.hpp` (`cloneTree`/`cloneTrees`), основа `ir::buildHir`.

| internal-подкаталог | MEMORY | ответственность |
|---|---|---|
| `ast/nodes/` | `nodes/MEMORY.md` | классы узлов и их семантика |
| `ast/slots/` | `slots/MEMORY.md` | класс-таблица слотов, `forEachSlot`, клон/ремап невладеющих ссылок |
| `ast/convert/` | `convert/MEMORY.md` | `TermToAstConverter`: правила `visit_*`, контекст-маркеры, контракты |

## Facts and invariants

- **⚠ Конверсия Term→AST не зависит от `session` (R34):** конструкторы узлов и `TermToAstConverter` принимают
  порт `ConvertServices{diag, attrs, opts, strings}` (`ast/convert_services.hpp`), собираемый владельцем
  (`makeConvertServices`). Ребро `ast→session` запрещено гейтом.
- **⚠ C/C++-имя нативной привязки — ЦЕЛЕВОЙ факт, НЕ в языковой модели (R1/R4):** `Decl` несёт только
  предикат `m_bindsNativeCpp`; имя на фазе конвертации Term→AST живёт во временном носителе
  `Decl::m_pendingNativeCppName`, а `assignDeclarationIds` переносит его в `DeclCppFacts` (компонент `target`,
  порт `Context::declCppFacts()`) под `DeclId` и очищает узел. Чтение — через порт цели (`DeclCppFacts`).
  `Decl` НЕ несёт C/C++-имени: `dump()` печатает `(native)` без имени. C++-суффикс сигнатуры хранится там же
  по `declId()` (`DeclCppFacts::cppSuffixOf`, пишет семантика); в AST-узле суффикса нет.
  Временный носитель НЕ ключуется по адресу узла (переиспользование адреса даёт ложную привязку).
- **Сайт импорта в AST — `ModulePart{index, body, projection}`:** всё, что видно с сайта в части модуля, —
  ОДИН объект `SiteProjection` (отфильтрованный экспорт, namespace-алиасы, несовпавшие маски, protected-флаги).
  Владелец проекции — кэш загрузчика (`shared_ptr`); правило видимости — `types/names.hpp`.
- Узлы в экспорт-наборе (`ExportEntry::node`) мутируются ТОЛЬКО по слотам; declaration-node не заменяется
  (контракт владения — `module_loader/MEMORY.md`).
- **⚠ Инвариант kind→класс ОБЯЗАТЕЛЕН:** узел с `Kind` обязан быть классом из `PARSER_TOKEN_KINDS` (иначе
  `is<T>()`/`as<T>()` — UB: класс выводится из kind, RTTI нет). Клон копирует по kind→классу.
- **⚠ kind↔класс НЕ 1:1:** разные kinds — одинаковый класс при той же структуре данных; отношение задаёт
  ВТОРЫМ полем `PARSER_TOKEN_KINDS`. Новый класс под новый kind ЗАПРЕЩЁН; перечень классов ПРОИЗВОДИТСЯ из
  `AST_SLOT_CLASSES` + `static_assert`.
- **⚠ default-конструктор любого узла даёт `kind = END`:** узел-замена обязан задавать kind явно, иначе
  неотличим от «не-узла» (кодоген падает в `dispatchEmit`).
- **⚠ Типы в AST: state — `QualType`, identity — `TypeId`.** Где state (const/inferred) обязан доехать до
  кодогена, поле хранит `QualType`; `TypeId` — для identity/ключей. Потеря state — дефект.
- **⚠ Deleter владеющего ресурса — материализованный факт, не текст:** `@[deleter(D)]` резолвит
  семантика (`applyDeclarationQualifiers`). `unique<T,D>` несёт D в identity типа (`RefTypeData`), для
  `shared` тип D хранится в `VarDecl::deleterType` (выход `resolveTypeRef`/`applyRefAttrs`). Кодоген имя
  по тексту НЕ резолвит (R39/R53).
- **⚠ Alias-`TypeDecl` несёт собственный `DeclId`:** `TypeDecl` без объявления-ребёнка (`MyInt ::= :Int32`,
  `::-`) — это само объявление, id пишется на обёртку (`Binary::declId()`); у типа с телом
  (`:Class{...}`) id остаётся у `ClassDecl`/`RecordDecl` — ровно одна идентичность на объявление.
  Единый аксессор `declarationIdOf(node)` (R23); адрес узла идентичностью не является.
- **⚠ `text()` и `range()` независимы:** при макро-раскрытии spelling остаётся из определения, range
  переприсваивается на call-site. Текст из range не выводить; правило имени применяет ТОЛЬКО
  `assignNameFromRaw`.
- **⚠ Term НЕ хранится в AST:** конструктор извлекает range/текст/флаги. У manual/test-узла `range()`
  невалиден; `text()` есть только у `WithText`.
- **⚠ `ModuleNode` импорта не хранит своего тела:** `parts[].body` — разделяемый `shared_ptr` на тело реестра
  (конвертируется один раз); маски разбираются ОДИН раз; экспорт/алиасы — разделяемый кэш.
- **⚠ `AstNodeBase::visibility` — target-neutral видимость** (`Public/Protected/Private`; два писателя).
  Объявление УРОВНЯ МОДУЛЯ ставит загрузчик; ЧЛЕН тела класса — конвертер Term→AST из
  standalone-маркера (`convertClassMembers`: `::`/`_::`/`__::` → C++-доступ public/protected/private).
  Класс-скоуп регистрируется под local-key, поэтому видимость члена не влияет на ключи/экспорт;
  C++-маппинг — дело бэкенда, доступ проверяет семантика.
- **⚠ `AstNodeBase::module`** ставит ТОЛЬКО загрузчик и только объявлениям уровня модуля; локальные/члены
  модуля не несут.
- **⚠ `IdentName::moduleRef` — внутренний факт «имя разрешилось в объявление другого модуля»:**
  `ModuleIndex` владельца (ставит разрешение для кодогена/`#include`), НЕ синтаксис. Синтаксическая
  форма `\path::name` удалена из грамматики: модуль не является префиксом разрешения.
- **⚠ Слои представления:** `Term` — разбор, `AstNode` — AST, `HirModule` — производная (клон+lowering). AST
  после `run()` неизменяема; кодоген/flow — HIR.
- **⚠ ref-вид: единая точка разбора — `ast/ref_syntax.hpp`** (`refKindFromAttrArgs`, `refKindOfAttr`,
  `refKindOfTypeNode`, `refKindOfTypeSpec`). НЕ дублировать разбор по месту.
- **Типизированный доступ без RTTI:** `is<T>()`/`as<T>()` через kind→класс; `as<T>()` при неверном kind —
  FAULT. `dynamic_cast`/`static_pointer_cast` по AST ЗАПРЕЩЕНЫ. Контракты объявления — `trustContracts()`.
- **`resolvedDecl`** (у `IdentName`/`ArgNode`) — невладеющие ссылки на объявление (заполняет семантика);
  переживают pop скоупов. Для типов/полей/неразрешённых имён — nullptr (не признак ошибки).
- **`visit_<Kind>`** у потребителей — осознанная compile-time проверка полноты, НЕ дублирование; no-op в
  default-базу выносить нельзя.
- **⚠ `origin`:** у HIR-клона — исходный узел AST (nullptr у созданных lowering'ом). Идентичность ОБЪЯВЛЕНИЯ
  (`Decl`-узла, R23) — `Decl::declId()` (`DeclId`, `ast/decl_id.hpp`): назначается `assignDeclarationIds`
  (pre-order, идемпотентно) в порядке объявлений модуля-владельца, переживает клонирование; синтетика
  (созданная анализатором/lowering после назначения) id не несёт. `NodeId` (id на КАЖДЫЙ узел) не вводится.
- **⚠ `MatchStmt::m_matcherDecl` — невладеющая ссылка на разрешённый предикат `@[matcher("fn")]`:**
  заполняет семантика (`analyzeMatchMatcher`), кодоген читает её вместо повторного резолва по тексту (R53);
  слот `NonOwnPtr` ремапится клоном (как `IdentName::resolvedDecl`).
- **⚠ ErrorExpr — «diagnose-then-replace»:** первый нашедший ошибку пасс репортит и заменяет под-узел;
  `m_original` вне `children()` (без каскада).
- **⚠ Глифы операторов — единый лист `utils/operator_spelling.hpp`:** `TRUST_BINARY_OP_SPELLINGS` — проекция
  enum↔глиф над `trust::spell` (НЕ второй источник строк); пересечение с реестром перегружаемых
  операторов проверяется тестом `GlyphsMatchAstSpellings`.
- **⚠ AST владеет синтаксисом, не семантикой формы:** имена/порядок/`visibility`/инициализатор/
  `MapperRange` полей — факты AST; тип поля record существует в трёх местах (`Symbol::type`,
  `VarDecl::inferredType`, форма `types`), владелец — форма (`ARCHITECTURE.md` R1/R6), остальные —
  не источник истины для типа поля. `VarDecl::inferredType` поля — проекция AST-фактов для бэкенда (E2/R6),
  читается через `ExprFacts`, а не как тип поля.

## Relations

- `attrs` (раскладка `AttrId`, правила unhandled/unknown), `ir` (клон+lowering, HIR), `types` (identity/state),
  `transpiler`/`semantic` (потребители).
