# MEMORY.md

> scope: include/syntax/grammar
> role: persistent-memory
> last_reviewed: 2026-11-11
> review_period: 30

## Architecture

Вход генератора парсера: `parser.y.in` (Bison); правила Flex — `syntax/lexer/lexer.l`. `%token`-блок собирается на configure из
единственного источника терминалов `syntax/term/term_types.h` (`TERMS`/`SYMBOL_TOKENS`) и подставляется в
`parser.y.in`; сгенерированные `parser.yy.{cpp,h}` и `lexer.yy.{cpp,h}` кладутся в `<build>/syntax`.
Подключение — `cmake/generate_parsers.cmake`, цель `gen_syntax_parsers`.

## Facts and invariants

- **Списки терминалов не дублируются в грамматике:** источник — `term_types.h`; новый терминал
  добавляется там (x-macro), а не в `parser.y.in`.
- **`%expect 4` — намеренный контракт грамматики** (4 задокументированных S/R в шапке `parser.y.in`;
  см. `syntax/parser/MEMORY.md`); изменение числа S/R-конфликтов требует осознанного решения, а не
  «подгонки». Конфликт 4 — `native • LT` (C++-аргумент шаблона реализации builtin-типа
  `Complex32 = %std::complex<float> { };`; shift→аргументы, а не сравнение нативного имени).
- **⚠ Маркеры наследования (`::`/`_::`/`__::`) НЕ добавляют S/R-конфликтов:** `::` — `ns_start`;
  `_::`/`__::` — ОДНА продукция `class_type_base: ns_part NAMESPACE` (текст = `<ns_part> + "::"`).
- **⚠ Голый `::` как ИМЯ удалён из `name_no_attr`:** иначе `class_type_base: ns_start` (маркер `::`)
  давал R/R на lookahead LBRACE (`native_class_fwd: name LBRACE` vs `class_type_def: ... LBRACE`).
  Маркер читается по ТЕКСТУ терма (`::`/`_::`/`__::`), а не по TermID: `class_type_def` перезаписывает
  `m_id` головного терма на CLASS.
- **⚠ Маркеры видимости ЧЛЕНОВ тела класса — тот же приём, без нового нетерминала:** `class_member`
  принимает `ns_start` (public `::`) и `ns_part NAMESPACE` (protected `_::` / private `__::`)
  НАПРЯМУЮ (`_` приходит токеном NONE, но `ns_part: NAME | NONE` нормализует его в `_`). Число
  S/R-конфликтов не меняется (`%expect 4`). Текст нормализуется к полному маркеру; уровень читает
  AST-конвертер (`visibilityOf`).

## Relations

- `syntax/term/term_types.h` (терминалы), `cmake/generate_parsers.cmake`, `syntax/parser`, `syntax/lexer`.
