# MEMORY.md

> scope: include/syntax
> role: persistent-memory
> last_reviewed: 2026-09-04
> review_period: 30
> max_size: 10783

## Architecture

Scanner (Flex, lexer.l) → Macro → Parser (Bison) → Term-дерево. Раскрытие макросов — НЕ отдельная
стадия: лексер подаёт термины в буфер `m_macro_analisys_buff`, `Macro` раскрывает их до грамматики Bison.
Прагмы/предопределённые макросы вынесены в `PredefMacroResolver`/`PragmaEvaluator` (Parser держит их
членами, вызывает в GetNextToken). Алгоритмика макропроцессора — свободные функции (`syntax/macro_split.hpp`):
`macro_arg_parser`/`macro_matcher`/`macro_expander`/`macro_validator`; `Macro` — реестр скоупов.

**ЕДИНСТВЕННЫЕ источники списков макросов/прагм — x-macro** `predef_macro_x.hpp` (`TRUST_VALUE_MACROS` +
`TRUST_CONTEXT_MACROS`) и `pragma_macro_x.hpp`. Из них генерируются enum, таблицы имён/описаний и авто-
стрингификация; реестры и доки строятся ОДИН раз. Неизвестный макрос (`@__...__`) — «not implemented»;
прагма в `expandPredefMacro` — без ошибки (её ведёт pragmaEval).

**⚠ `@__CHECK_AREA__(...)` — контекст-макрос С аргументами (НЕ штампуется, НЕ прагма):** в `GetNextToken`
ДО `expandPredefMacro`/прагм `PragmaEvaluator::evalCheckArea` разбирает `( <area> [, <behavior>] [, <attr>...] )`
(area через `areaKindFromString`; behavior СТРОГО `default|ignore|warning|error`), стирает вызов и вставляет
ОДИН терм `TermID::MACRO_CONTEXT` (текст `@__CHECK_AREA__`, аргументы — дочерние NAME-термы). term_to_ast
по тексту строит узел `CheckAreaStmt` (НЕ ContextMacro → ContextMacroExpander его не раскрывает). Голый
(без скобок) `@__CHECK_AREA__` не штампуется — completeness-тест `AllContextMacrosStamped` пропускает его.

## Facts and invariants

- **Ловушка (каждое `<<EOF>>`-правило ОБЯЗАНО заканчиваться `yyterminate()`):** иначе Flex, не получив
  return в `<<EOF>>`, вызывает yylex снова на том же EOF → бесконечный цикл на незакрытых `/* */`,
  `{% %}`, `@@@`, raw-string, `` ` ``. Строковые состояния (`state_STRCHAR`/`STRWIDE`) своего `<<EOF>>`
  не имеют (fallback на глобальный), но незакрытая строка без `\n` не репортится.
- **Семантические ошибки разбора → `Severity::Error`, НЕ Fatal:** не бросают исключение; пайплайн (в LSP
  allow_semantic_on_errors) может продолжить на частичном AST. Fatal/throw — только ошибки безопасности.
- **Ловушка (позиция синтаксической ошибки — НЕ flex-курсор):** `Parser::error()` не должен брать позицию
  из `tokenStartOffset()`/`m_current_pos`: `GetNextToken` упреждающе буферизует термины до `;`, курсор
  «убежал» вперёд. Корректная позиция — из реально потреблённых bison-токенов `Parser::m_recent`.
- **Атрибуты `@[...@]`: макро-имена внутри НЕ раскрываются** (`m_in_attr`) — иначе бесконечная рекурсия
  для keyword-макросов (напр. `func_const`). Явный `@`-макрос внутри атрибута — ошибка. `@]` лексится как
  ATTR_COMPLETE. Предохранители: `kMacroExpansionLimit`/`kMacroNestingLimit` (самовоспроизведение идёт
  через границы чтений парсера, `m_macro_depth` его не ловит).
- **Лексема NONE «_»:** голый `_` — отдельный токен NONE (НЕ NAME). В name-позициях (`ns_part`) — `NAME
  "_"` (none-значение `x := _`, скрытая область, rest-пропуск), в `catch_arg` — catch-any (`TermID::NONE`
  → `IntAny`). `none` — DSL-макрос → `_`. Идентификаторы с ведущим `_` (`_var`) остаются NAME (длиннейшее
  совпадение `{name}` перекрывает `"_"`).
- **Унифицированная последовательность (`Term::m_sequence`):** верхнеуровневая sequence и вложенные
  блоки строятся из единого нетерминала `expression`. `AppendBlock` сохраняет границу области видимости
  (иначе объявления «протекают» в охватывающий скоуп). Пустые `;` разрешены; пустая Sequence → пустой
  SEQUENCE-терм (не попадает в AST как SEMICOLON).
- **`Term::Create`:** копирующая (безопасна для локальных строк) и non-copy (string_view, текст живёт в
  SourceMapper и должен пережить Term). Для намеренного обрезания в parser.y — явная копия.
- **Границы унификации «аргументы vs шаблон» (это инварианты против `<`-неоднозначности, НЕ дубликаты):**
  голое имя в `<>` (`template_args`, напр. `vector<Int32>`) — ТИП-аргумент (мягкий warning «нужен сигил ':'`);
  то же голое имя в `()` (`args`, `f(x)`) — ЗНАЧЕНИЕ/переменная. Поэтому `args` (разрешает голый `logical`)
  и `template_args` (только `digits_literal`/`LPAREN logical RPAREN`) НЕ сводимы в один нетерминал без запрета
  голых выражений-аргументов вызова (`f(d.count)`, `:Int32(x)` — обязательные формы). Голый `logical`
  как «значение по умолчанию»/value в `<>` запрещён и пишется `(expr)` (уже так в `template_arg`); в `()/[]`
  после callee голое выражение-аргумент сохраняется. `type_item`(без `<>`) vs `type_item_tmpl`(с `<>`) —
  намеренный «инвертированный» шаблон: в операнд-позиции `<` обязан оставаться сравнением (`1:Bool < var`);
  слияние вернуло бы S/R `type_class • LT`. `%expect 2` не меняется.
- **Форматтер не должен терять исходник после ошибки лексера:** незакрытая строка/комментарий «съедают»
  хвост файла; в `Emitter::run()` хвостовой зазор от последнего токена до EOF при `parseGap == verbatim`
  сохраняется вербатим.
- **Forward-объявление классов/шаблон-классов через `::=` (`native_class_fwd`):** `String ::= %std::string {...};`,
  `<T1,T2> Pair ::= %std::pair<T1,T2> {...};` (явная реализация) и `<T1,T2> Pair ::= <T1,T2> %std::pair {...};`
  (обобщённая). RHS — ЛЮБОЕ имя (native — с ведущим `%`); терм CLASS, чей текст = имя, `m_id=CLASS`,
  члены — в `m_sequence`. Явная форма заполняет m_template RHS (аргументы реализации); обобщённая — НЕ
  заполняет (шаблонность — из `::=`->m_template), ClassDecl выводит generic по m_template. Обе формы дают
  `Pair<A,B>`→`std::pair<A,B>`: `m_templateArgs` (optional) заполняется ТОЛЬКО для явной реализации
  (`имя<T1,T2>`); обобщённая форма оставляет его nullopt (generic = `m_templateParams.has_value()` &&
  `!m_templateArgs.has_value()`), маппинг 1:1 происходит при инстанциации по типовым параметрам.
  Dot-члены `.method()/.field/.%method()/.%field` — через dedicated
  `class_member` (`member_name`/`dot_member_name`, ведущая '.' в тексте имени — признак экземплярного члена),
  а не общий `assign_lval` (чтобы не конфликтовать с dot-доступом `field`); форма `EQ` члена сохранена. `%expect 2` не
  меняется. Родитель `<T> X ::= ...` — `template_prefix type_def_seq` (параметры в m_template терма `::=`).
