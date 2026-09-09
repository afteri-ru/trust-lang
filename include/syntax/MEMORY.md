# MEMORY.md

> scope: include/syntax
> role: persistent-memory
> last_reviewed: 2026-09-17
> review_period: 30
> max_size: 15600

## Architecture

Scanner (Flex) → Macro → Parser (Bison) → Term-дерево. Раскрытие макросов — НЕ отдельная стадия:
лексер подаёт термины в буфер, `Macro` раскрывает их до грамматики Bison. Прагмы/предопределённые
макросы — `PredefMacroResolver`/`PragmaEvaluator` (члены Parser, вызов в GetNextToken). Алгоритмика
макропроцессора — свободные функции; `Macro` — реестр скоупов.

**ЕДИНСТВЕННЫЕ источники списков макросов/прагм — x-macro** (`predef_macro_x.hpp`,
`pragma_macro_x.hpp`): из них генерируются enum, таблицы имён/описаний и авто-стрингификация;
реестры и доки строятся ОДИН раз. Неизвестный макрос (`@__...__`) — «not implemented»; прагма в
`expandPredefMacro` — без ошибки (её ведёт pragmaEval).

## Facts and invariants

- **⚠ `@__CHECK_AREA__(...)` — контекст-макрос С аргументами (НЕ штампуется, НЕ прагма):** в
  `GetNextToken` ДО `expandPredefMacro`/прагм `evalCheckArea` разбирает `( <area> [, <behavior>]
  [, <attr>...] )` (behavior СТРОГО `default|ignore|warning|error`), стирает вызов и вставляет ОДИН
  терм `TermID::MACRO_CONTEXT` (аргументы — дочерние NAME-термы). term_to_ast строит по тексту
  `CheckAreaStmt` (НЕ ContextMacro → ContextMacroExpander его не раскрывает). Голый (без скобок)
  `@__CHECK_AREA__` не штампуется — completeness-тест `AllContextMacrosStamped` его пропускает.
- **⚠ `@__DEBUG__`/`@__DEBUG_SCOPE__` — контекст-макросы С аргументами (НЕ штампуются):** в
  `GetNextToken` ДО `expandPredefMacro` `evalDebug` захватывает `( <args> )`, стирает вызов и
  вставляет ОДИН терм `MACRO_CONTEXT` → term_to_ast строит `DebugStmt`. Guard `buf[0]->m_id ==
  MACRO_CONTEXT` обязателен (как у `@__CHECK_AREA__`): иначе маркер снова оказывается во главе буфера
  и обрабатывается повторно. При `TRUST_TRACE_ENABLED == 0` вызов стирается БЕЗ маркера +
  `Severity::Warning`; completeness-тест пропускает оба имени. Аргументы: `@__DEBUG__(<маски>)`/
  `@__DEBUG__()` — фильтр сообщений; `@__DEBUG_SCOPE__([<маски имён>...] [, key=value...])` — печать
  состояния (не зависит от фильтра); значения опций без кавычек. Валидация опций — ЗДЕСЬ (реестр
  `utils/trace_options.hpp`) с полным списком при ошибке. Описание — корневой `MEMORY.md`.
- **Составные присваивания `+= -= *= /= //= %=` — отдельный терминал `OP_ASSIGN` (Kind `AssignOp`):**
  лексер отдаёт `YY_TOKEN(OP_ASSIGN)`, грамматика — веткой `operator:`; в AST — `Binary(AssignOp)`.
  Арифметика `+ - * / // %` — `OP_MATH`/`MathOp`. Не различать по тексту — только по `kind`.
- **Ловушка: каждое `<<EOF>>`-правило ОБЯЗАНО заканчиваться `yyterminate()`** — иначе Flex, не
  получив return, повторно вызывает yylex на том же EOF → бесконечный цикл. Строковые состояния
  (`state_STRCHAR`/`STRWIDE`) своего `<<EOF>>` не имеют (fallback на глобальный), но незакрытая
  строка без `\n` не репортится.
- **Семантические ошибки разбора → `Severity::Error`, НЕ Fatal:** не бросают исключение; пайплайн (в
  LSP `allow_semantic_on_errors`) продолжает на частичном AST. Fatal/throw — только ошибки
  безопасности.
- **Ловушка (позиция синтаксической ошибки — НЕ flex-курсор):** `Parser::error()` не должен брать
  позицию из `tokenStartOffset()`/`m_current_pos`: `GetNextToken` упреждающе буферизует термины до
  `;`, курсор «убежал» вперёд. Корректная позиция — из реально потреблённых bison-токенов
  `Parser::m_recent`.
- **Атрибуты `@[...@]`: макро-имена внутри НЕ раскрываются** (`m_in_attr`) — иначе бесконечная
  рекурсия для keyword-макросов. Явный `@`-макрос внутри атрибута — ошибка. `@]` лексится как
  ATTR_COMPLETE. Предохранители `kMacroExpansionLimit`/`kMacroNestingLimit` (самовоспроизведение идёт
  через границы чтений парсера, `m_macro_depth` его не ловит).
- **Лексема NONE «_»:** голый `_` — отдельный токен NONE (НЕ NAME). В name-позициях (`ns_part`) —
  `NAME "_"` (none-значение `x := _`, скрытая область, rest-пропуск), в `catch_arg` — catch-any
  (`TermID::NONE` → `IntAny`). `none` — DSL-макрос → `_`. Идентификаторы с ведущим `_` (`_var`)
  остаются NAME.
- **Унифицированная последовательность (`Term::m_sequence`):** верхнеуровневая sequence и вложенные
  блоки строятся из единого нетерминала `expression`. `AppendBlock` сохраняет границу области
  видимости (иначе объявления «протекают» в охватывающий скоуп). Пустые `;` разрешены; пустая
  Sequence → пустой SEQUENCE-терм.
- **`Term::Create`:** копирующая (безопасна для локальных строк) и non-copy (string_view, текст живёт
  в SourceMapper и должен пережить Term). Для намеренного обрезания в parser.y — явная копия.
- **Границы унификации «аргументы vs шаблон» (инварианты против `<`-неоднозначности):** голое имя в
  `<>` — ТИП-аргумент (warning «нужен сигил ':'»); то же голое имя в `()` — ЗНАЧЕНИЕ/переменная.
  `args` и `template_args` НЕ сводимы в один нетерминал без запрета голых выражений-аргументов
  вызова. Голый `logical` в `<>` запрещён, пишется `(expr)`. `type_item`(без `<>`) vs
  `type_item_tmpl`(с `<>`) — намеренный «инвертированный» шаблон: в операнд-позиции `<` обязан
  оставаться сравнением (`1:Bool < var`). `%expect 2` не меняется.
- **Форматтер не должен терять исходник после ошибки лексера:** незакрытая строка/комментарий
  «съедают» хвост файла; в `Emitter::run()` хвостовой зазор от последнего токена до EOF при
  `parseGap == verbatim` сохраняется вербатим.
- **Forward-объявление классов/шаблон-классов через `::=` (`native_class_fwd`):** RHS — ЛЮБОЕ имя
  (native — с ведущим `%`); терм CLASS, чей текст = имя, `m_id=CLASS`, члены — в `m_sequence`. Явная
  форма заполняет m_template RHS; обобщённая — НЕ заполняет. Dot-члены `.method()/.field/.%method()/
  .%field` — через dedicated `class_member` (ведущая '.' в тексте имени — признак экземплярного
  члена), а не общий `assign_lval`.
- **⚠ `class_props` (тело класса/Struct) — СОБСТВЕННЫЙ SEQUENCE-контейнер, НЕ связь через `m_right`:**
  `m_right` члена — его RHS/тело; связывать список членов через `m_right` нельзя (практика
  `RightToBlock` рвала m_right и теряла тела методов/инициализаторы). `class_props` строит
  `TermID::SEQUENCE` и складывает членов в `m_sequence`.
- **Форма `class_type_def: class_type_bases ELLIPSIS` (`:Name ::= :Base ...;`)** — forward-объявление
  класса БЕЗ тела: маркер ELLIPSIS в `m_sequence` CLASS-терма (`%expect` не изменился — 2).
- **⚠ `tmpl_ctor` — явная конструкция шаблон-типа в позиции значения:** `:Box<Int32>(...)` /
  `:vector<Int32>(...)` в RHS `:=`/`=`. Отдельное правило (`type_class LT template_args GT call`),
  даёт РОВНО один дополнительный S/R `type_class • LT` → `%expect 3`. Затрагивает только `:`-формы;
  обычные сравнения `a < b`/`x:Int32 < 5` не тронуты (конфликтует лишь `:Type < expr`). Record-шаблон
  эмитится как `c_Box<int32_t>(...)`; нативный `:vector<Int32>(...)` — `std::vector<Elem>{...}`.
- **⚠ Лямбда: TermID `LAMBDA` (kind FuncDecl) + `LAMBDA_CALL` (kind CallExpr) — НЕ
  терминалы-токены:** `lambda_op` создаёт `LAMBDA`; список захватов кладётся в `m_sequence`
  (НЕ в `m_args` — туда позже `lambda:` пишет параметры через swap), параметры — в `m_args`, тело — в
  `m_right`, тип возврата — в `m_type`. Немедленный вызов `( lambda )(args)` — единый терм
  `LAMBDA_CALL` (`m_left`=лямбда, `m_args`=аргументы); конвертация — кастомный `visit_LAMBDA_CALL`.
  Прямая форма `lambda(args)` НЕ поддерживается: грамматика принимает её ради явной
  ошибки-диагностики. Переиспользование `args` для захватов не даёт новых S/R-конфликтов; `%expect 3`
  НЕ изменился.
- **`FILLING` (`... expr ...`) — Kind `Filling` (класс `Sequence`), операнд в `m_right`,**
  `m_mapperRange` покрывает весь `... expr ...`. Правило `arg` ОБЩЕЕ для всех `args`; `%expect` не
  меняется.
- **⚠ Оператор-объявление — нетерминал `operator_sig: REFLECTION call [types]`:** используется в
  `member_name` (member-оператор в теле класса) и в `assign_seq` (`operator_sig trust_cond_seq CREATE_NAME
  assign_expr` — свободный оператор верхнего уровня). Лексер НЕ меняется (`` `SYMBOL` `` уже REFLECTION);
  символ — текст REFLECTION-терма, параметры в `m_args`, тип в `m_type`. **Контракты едины для функций/
  методов/операторов:** `trust_cond_seq` включён в `assign_seq`, `type_def_seq`, free-оператор и
  `class_member` (форма `member_name trust_cond_seq assign_op assign_expr` ЗАМЕНИЛА прежнюю без conds -
  добавление отдельной альтернативы давало 2 S/R-конфликта); привязка — единая `appendTrustConds`
  (parser.cpp). `%expect 3` не изменился.

- **⚠ Операторы сравнения типов `<~`/`~~`/`~~~`:** `<~` — ОТДЕЛЬНЫЙ лексерный токен
  (`OP_COMPARE`; двухсимвольный, обходит ловушку `:T<...` = шаблон-аргументы); `~~`/`~~~` —
  `OPERATOR_DUCK`. Голая `~` как оператор, формы `:~T`/`:~~T`/`:~~~T` в `type_class`, `!~`/`!~~`/`!~~~`
  и `~=` — УДАЛЕНЫ (токен `TILDE` остаётся только для деструктора `~Name()`).

- **⚠ Наборы типов (`:A + :B`) — грамматика БЕЗ новых конфликтов:** `type_set_expr: types |
  type_set_expr PLUS types | type_set_expr MINUS types` (узел `TermID::TYPE_SET`, левый
  ассоциативный, текст оператора `+`/`-`); используется в RHS `::=` (`type_def_rhs`) и inline
  `:(:A + :B)` (`type_class: COLON LPAREN type_set_expr RPAREN`). Ведущий `:` перед `(` ОБЯЗАТЕЛЕН
  (иначе конфликт с вызовом `name(...)`/словарём). Guard «`m_id = TYPE`» (в `types`/`type_item`/
  `type_call`/`type_call_tmpl`) ОБЯЗАН исключать `TYPE_SET` - иначе набор затирается до типа.
  `%expect 3` НЕ меняется.

## Decisions

## Relations
