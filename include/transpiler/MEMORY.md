# MEMORY.md

> scope: include/transpiler
> role: persistent-memory
> last_reviewed: 2026-08-26
> review_period: 30
> max_size: 11542

## Architecture

`CppTranspiler` — фасад-драйвер (наследник `KindVisitor`): публичный API, обход/диспетчеризация,
рекурсивные точки (`emitExpr`/`emitSequenceBody`/`emitBodyNode`). Всё mutable-состояние — в
`CppEmitContext` (`emit_ctx.hpp`); тяжёлая кодогенерация — по связным эмиттерам (`TypeEmitter`,
`DeclEmitter`, `StmtEmitter`, `ExprEmitter`, `ContractEmitter`), разделяющим `CppEmitContext` и
обращающимся к драйверу через ссылку. Транспилятор — ТОЛЬКО кодогенерация по «низкоуровневому» AST:
всё, что требует анализа структуры (метки goto, точка с запятой, break/continue), зашито в AST
проходом lowering (см. semantic/MEMORY.md).

## Facts and invariants

- **Манглинг trust-имён — единый `utils::name_to_cpp`:** простые ASCII-имена получают префикс `c_`
  (`x → c_x`), юникод транслитерируется/HEX-кодируется, `::` → `cpp_std$$vector`. **Нативные имена с
  ведущим `%`** — уже C++-символы рантайма: `name_to_cpp` срезает `%` без изменений, иначе ломается
  линковка. Обратный `cpp_to_name` восстанавливает `%`, обеспечивая round-trip.
- **Две формы нативных функций:** forward-decl (`%name(...):T := ...;` — имя это C-style идентификатор,
  реализацию линковщик ищет по имени) vs импорт (`name(...):T := %native...;` — алиас: C++-функция `name`
  НЕ эмитится, вызовы переписываются в `native(...)`). `@[link]` ставится на импорт, не на forward-decl.
  Линковка forward-декларации: без `::` → `extern "C"`, с `::` → C++-линковка.
- **⚠ trap (addNameMapping, cpp-оффсет имени):** нельзя считать оффсет от 1 — `output_prepend` (инклуды)
  сдвигает `mapStackTop().outputBegin`. Оффсет = `outputBegin.offset() + длина префикса` сгенерированной
  строки. `mapDeclaredName` — единый расчёт (hover).
- **⚠ trap (Return с пост-условиями обязан оборачивать код в `MapperScope(n.range())`):** в
  `visit_ReturnStmt` при непустых `post` эмитится `auto __trust_res_N = <expr>;` + проверка + `return
  __trust_res_N;`. БЕЗ MapperScope этот код наследует маппинг тела функции — у строки `return` пустой
  `trustToCpp`. Как в `visit_BreakStmt`/`visit_SemicolonStmt`.
- **Позиция пост-условия:** перед каждым `return <value>` (имя функции связывается через hoist в temp
  `__trust_res_N` + RAII `ResultGuard`). Функция известна ИЗ УЗЛА (`JumpStmt::m_funcDecl` ставит семантика)
  — транспилятору не нужен контекст. Для void — в конце тела.
- **Trust-условия ТИПА:** живут на узле ДЕКЛАРАЦИИ (`Binary::m_trust`); использование несёт НЕВЛАДЕЮЩУЮ
  ссылку `VarDecl::m_typeDecl`/`Binary::m_typeDecl` (ставит семантика, переживает таблицу символов),
  условия НЕ копируются. Проверка генерируется при создании значения и ПОСЛЕ присваивания
  (`AssignOp::lhsType` уже на узле); на чтение повторно не проверяем.
- **Enum:** эмитится самодостаточный `struct c_Color` с `using Value`, полями `value`/`ordinal`,
  `constexpr`-конструкторами и операторами по ординалу. Члены — `static const` + out-of-class
  `const c_Color c_Color::c_MEMBER{...};` (`static constexpr` собственного типа невозможен — неполный тип
  в точке объявления). Значения членов — ТОЛЬКО скалярные литералы; составные НЕ реализованы
  (диагностика в самой генерации, по факту невозможности сформировать C++-литерал — иначе тихий сброс
  `int8_t c_a{}` или невалидный C++).
- **Точка входа (`@main` → `<модуль>__main__`):** entry-функция эмитится с СЫРЫМ именем (без манглинга
  `c_`), типом `int` (если явный не задан) и `return 0;` в конце, если нет явного return — иначе не
  сливается с `extern int <модуль>__main__()` в pipeline `_main.cppt`.
- **Рациональный литерал** `num\den` — отдельная лексема `RationalLiteral` → `trust::Rational("num\\den")`
  (backslash экранируется в C++-строковом литерале); заголовок `trust/rational.hpp` — по типу (механизм №1).
- **StrChar-экранирование:** текст литерала хранится как есть (escape не декодируются). Экранирование
  нужно ТОЛЬКО для StrChar (`'…'` → `"…"`): голый `"` в нём допустим и обязан стать `\"` в C++.
  StrWide (`"…"` → `L"…"`) экранирования не требует (голый `"` в нём невозможен).
- **Два механизма подключения инклудов** (без сканирования выходного буфера): №1 — по типу
  (`m_usedTypes`, собирается из `recordUsedType`/`resolveCppTypeId`, директивы ПОСЛЕ обхода);
  №2 — по рантайм-символу (EMBED-узлы, substring-скан текста, только `recordRuntimeSymbolHeaders(id)`).
  `std::any_cast` (`<any>`) — по типу операнда Any. См. types/MEMORY.md.
- **Source map для forward-decl подавляется** (`SourceMapWriter::suppressMapping`): синтетическое
  объявление на сайте импорта не должно заявлять исходный диапазон модуля (этот диапазон маппится в
  собственном `.cppt` — иначе коллизия trustKey «уже замаплено»). Сопоставление экспортов — по указателям
  термов.
- **Term-изоляция:** транспилятор не обращается к `m_term` напрямую — диапазоны/имена только через методы
  узлов (`range()`, `nameRange()`, `blockRange()`, `text()`). Изменения раскладки Term/term_to_ast не
  должны влиять на codegen.
- **Именованные блоки-метки** для break/continue эмулируются РУЧНЫМИ goto-метками в lowering, а НЕ через
  нативный C++26 `break label;`/`continue label;` (P2552) — clang++-22 не поддерживает.
