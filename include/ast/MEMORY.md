# MEMORY.md

> scope: include/ast
> role: persistent-memory
> last_reviewed: 2026-08-19
> review_period: 30
> max_size: 10233

## Architecture

`SyntaxToken` — универсальный элемент последовательности, наследник `std::variant<Lexeme, AstNodePtr>`;
`SyntaxSeq = std::vector<SyntaxToken>` — единый тип последовательности (внутренние поля AST
`m_dims/m_args/m_body`). Поток: Flex/Lexer → Sequence → MMProcessor → Sequence → ParserAST.

**`AstNodeBase`** хранит ТОЛЬКО `m_kind` и `TermPtr m_term`. `text()` читается из `m_term`; `range()`
для простых узлов — из `m_term`, для составных (Binary/control-flow/VarDecl/FuncDecl/MatchStmt)
вычисляется НА ЛЕТУ по узлам-детям. Для узла БЕЗ `m_term` (manual/test-only, синтетический):
`range()` возвращает НЕВАЛИДНЫЙ range `{}` (без EXPECT), а `text()` — EXPECT. Это необходимо: родители
считают свой охват по `child->range()` (`spanOfNodes`), поэтому ручные дети обязаны «мягко» сообщать
об отсутствии source-range.

**Две группы конструкторов (правило, не смешиваются):** терм-конструктор `X(Kind, TermPtr, Context*)`
(при `ctx != nullptr` сам рекурсивно строит детей через `TermToAstConverter`) и manual-конструктор без
TermPtr (тесты). Гибриды вида `Binary(k, term, left, right)` запрещены. `TermToAstConverter`
(term_to_ast.hpp) — внутренний API ast_lib; публичный вход — `termToAst`. Конвертер **НЕ мутирует**
исходный Term (проверяется тестом `ConverterDoesNotMutateTerm`).

## Facts and invariants

- **⚠ trap (kind↔класс НЕ 1:1):** РАЗНЫЕ kinds ОБЯЗАНЫ иметь ОДИНАКОВЫЙ класс при одинаковой структуре
  данных. Отношение kind→класс задаётся ВТОРЫМ полем `PARSER_TOKEN_KINDS` (node_type). ЗАПРЕЩЕНО
  создавать новый класс под новый kind при той же структуре данных.
- **Инвариант (visit):** полный набор `visit_<Kind>` у каждого потребителя (SemanticAnalyzer,
  CppTranspiler) — осознанная compile-time проверка полноты, это НЕ дублирование кода. ЗАПРЕЩЕНО
  выносить no-op в default-базу — только явное переопределение.
- **ArgNode — ЕДИНЫЙ узел позиции списка аргументов** (параметр, элемент словаря/enum/variant, аргумент
  вызова): `text()` = имя ("" — безымянный), `m_type` (явный тип), `m_value`, resultType (ставит
  семантика). Элементы коллекций приводятся к ОДНОЙ форме ArgNode
  (`term_to_ast::visit_DICT`/`appendDictElementsFromArgs`), устраняя три формы (raw/AssignOp/ArgNode).
- **MODULE-конвертация в AstNode — только рекурсивная конвертация данных** (TermPtr m_body); реальная
  загрузка файла `ctx.loader()` ЦЕЛИКОМ в pipeline. В ast_lib — только общее рекурсивное построение.
  SEQUENCE-термы рекурсивно разворачиваются в top-level операторы, BLOCK-термы (пользовательские скоупы)
  сохраняются как `ScopeBlock` — границы блоков не теряются.
- **Trust-контракты (единая форма):** `@{ [kind:] <logical> @}` — один маркер с ВЕДУЩИМ `@` (`@{`,`@}`),
  НЕ с завершающим `X@` — `}@` конфликтует с `@@` (макрос) и `@name`. Один класс `TrustContract`
  (kind в поле, expr в `m_expr`). `collectChildren()` пуст — анализатор/транспилятор обходят `m_expr`
  ЯВНО. Мнемоники `trust_pre/post/assert/invariant(...)` — макросы-обёртки над `trust_contract(kind, expr)`.
- **⚠ trap (trust-контракт-термы после имени кладутся в `m_sequence` терма-имени, НЕ в отдельное поле):**
  `hasConvertibleChildren` обязан ПРОПУСКАТЬ `TRUST_CONTRACT`-термы в m_sequence — иначе имя с контрактами
  (`x @{...@} := ...`) стало бы `CallExpr` (контракт продублировался бы как аргумент вызова). `convertSeq`
  m_sequence терма-имени не обходит (обходит только оператора), поэтому правка нужна только в
  `hasConvertibleChildren`.
- **`AstNodeBase::m_trust`** — trust-контракты, привязанные к объявлению (после имени в `:=`/`::=`, на
  цикле `WhileStmt::m_trust`). НЕ входит в `children()`/`collectChildren`; заполняется из
  `term->m_left->m_sequence` (фильтр TRUST_*). Контракт в позиции выражения — автономный `TrustContract`
  в последовательности.
- **Термины решателя (`TrustElem`)** — операторы конкретного решателя (SMT/Z3): `@( term, args... @)`,
  первый аргумент СТРОГО маркер (имя из `SMT_Z3_TERM_LIST`, `include/ast/z3_term.hpp`), остальные — в
  `TrustElem::m_args`. Для кванторов первый `m_args` — переменная-связка. НЕ элементы языка.
- **Нереализованные TermID** (`Kind=Unimplemented`: AWAIT/YIELD/WHEN_ALL/WHEN_ANY/FILLING/PARENT) при
  конвертации сообщают диагностику `Severity::Error` с позицией и возвращают nullptr; `TermID::END` —
  FAULT. Пайплайн при `errorCount>0` после ParseAST не запускает semantic/transpile.
- **Система модулей:** идентификация ТОЛЬКО по индексу (`ModuleRegistry`); `moduleId` — только при
  загрузке для детекции цикла. `ModuleNode::m_exports` — отфильтрованный интерфейс сайта импорта;
  `m_isImport` — true для `\module(...)`, false для корневого.
