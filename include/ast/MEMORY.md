# MEMORY.md

> scope: include/ast
> role: persistent-memory
> last_reviewed: 2026-09-07
> review_period: 30
> max_size: 10300

## Architecture

`SyntaxToken` — элемент последовательности (`std::variant<Lexeme, AstNodePtr>`); `SyntaxSeq =
std::vector<SyntaxToken>`. Поток: Flex/Lexer → Sequence → MMProcessor → Sequence → ParserAST.

`AstNodeBase` хранит ТОЛЬКО `m_kind` и `TermPtr m_term`. `text()` читается из `m_term`; `range()` для
простых узлов — из `m_term`, для составных — вычисляется НА ЛЕТУ по детям. Для узла БЕЗ `m_term`
(manual/test-only): `range()` = невалидный `{}` (без EXPECT), `text()` — EXPECT (родители считают
охват по `child->range()`).

Две группы конструкторов (НЕ смешиваются): терм-`X(Kind, TermPtr, Context*)` (при ctx!=null сам
рекурсивно строит детей) и manual-без-TermPtr (тесты). `TermToAstConverter` (внутр. API, вход
`termToAst`) НЕ мутирует исходный Term.

## Facts and invariants

- **⚠ trap (kind↔класс НЕ 1:1):** РАЗНЫЕ kinds — ОДИНАКОВЫЙ класс при той же структуре данных;
  отношение задаётся ВТОРЫМ полем `PARSER_TOKEN_KINDS`. ЗАПРЕЩЕНО создавать новый класс под новый kind.
- **visit:** полный набор `visit_<Kind>` у потребителей — осознанная compile-time проверка полноты,
  НЕ дублирование; no-op в default-базу выносить нельзя.
- **ArgNode — ЕДИНЫЙ узел позиции списка аргументов** (параметр/элемент коллекции/аргумент вызова);
  коллекции приводятся к одной форме (три формы raw/AssignOp/ArgNode устранены).
- **`CheckAreaStmt` из маркера `@__CHECK_AREA__`:** term_to_ast перехватывает терм `MACRO_CONTEXT` с
  текстом `@__CHECK_AREA__` и строит лист `CheckAreaStmt` (не через generic dispatch; НЕ ContextMacro).
  Аргументы (дочерние NAME-термы) → `area`/`behavior`; имена атрибутов-требований резолвятся через
  `ctx.attrs().lookup` и добавляются `add_attr(id, /*manual=*/false)` — обрабатываются семантикой
  (`analyzeCheckAreaStmt`), а не «unhandled» (иначе `reportUnhandledAttributes` дал бы `-Wunhandled-attr`).
- **MODULE-конвертация — только рекурсивная конвертация данных** (`m_body`); реальную загрузку файла
  делает `ctx.loader()` ЦЕЛИКОМ в pipeline. SEQUENCE-термы рекурсивно разворачиваются в top-level,
  BLOCK сохраняется как `ScopeBlock` (границы блоков не теряются).
- **Trust-контракты:** `@{ kind --> <logical> @}` (kind всегда явный); маркеры с ВЕДУЩИМ `@`
  (`}@` конфликтует с `@@`). `collectChildren()` пуст — обходят `m_expr` явно.
- **⚠ trap:** trust-контракт-термы после имени кладутся в `m_sequence` терма-имени → `hasConvertibleChildren`
  обязан ПРОПУСКАТЬ их, иначе имя с контрактами (`x @{...@} := ...`) станет `CallExpr`.
- **`AstNodeBase::m_trust`** — контракты объявления (после имени/цикл); НЕ входят в children; из
  `term->m_left->m_sequence` (фильтр TRUST_*).
- **Термины решателя (`TrustElem`)** — операторы Z3 (`@( term, args... @)`), первый аргумент — строго
  маркер из `SMT_Z3_TERM_LIST`; квантор: первый `m_args` — связка. НЕ элементы языка.
- **Нереализованные TermID** (`Kind=Unimplemented`) → диагностика `Severity::Error` + nullptr; `END` — FAULT.
- **Защита стека:** `attr::StackGuard` → `attr::StackCheck` (необязат. целый N); функции контроля —
  `%`-нативные вызовы (`%trust_stack_check*`), арность/целочисленность атрибута валидирует семантика.
- **Система модулей:** идентификация ТОЛЬКО по индексу; `moduleId` — только при загрузке (детекция цикла).
- **⚠ Признак обработки атрибута (attr.hpp):** у `AttrId` верхний байт (bits 24–31) — флаги (builtin=24, analyzer=25, codegen=26, manual=31; биты 27–30 reserved; index = bits 0–23). Атрибут с НИ одним из analyzer/codegen считается «необработанным» (`detail::is_handled==false`): встроенные `pure/send/sync/thread` (и не зарегистрир. `optional`) зарегистрированы, но НИ одна стадия их не читает → любое использование даёт `-Wunhandled-attr` (pipeline: reportUnhandledAttributes после term_to_ast и после generateToFile). `-Wunknown-attributes` — незарегистрированное имя в `@[...]` (parse_attr).
- **⚠ ErrorExpr — УНИВЕРСАЛЬНАЯ error/recovery-заглушка (диагностика «diagnose-then-replace»):** пасс,
  первым обнаруживший ошибку и владеющий нужным контекстом, сообщает её СРАЗУ в момент выявления и
  заменяет ошибочный под-узел на `ErrorExpr`-владельца. `m_original` НЕ входит в `children()`/
  `collectChildren` → следующие пассы (семантика/транспилятор) содержимое игнорируют → нет повторной/
  каскадной диагностики, и узел НЕ удаляется (хранится для дампов/диагностик). Переиспользовать в
  других анализаторах вместо передачи причины в следующий пасс (введён capture-проходом `$^`).

