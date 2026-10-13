# MEMORY.md

> scope: include/syntax/grammar
> role: persistent-memory
> last_reviewed: 2026-11-04
> review_period: 30
> max_size: 4000

## Architecture

Вход генератора парсера: `parser.y.in` (Bison); правила Flex — `syntax/lexer/lexer.l`. `%token`-блок собирается на configure из
единственного источника терминалов `syntax/term/term_types.h` (`TERMS`/`SYMBOL_TOKENS`) и подставляется в
`parser.y.in`; сгенерированные `parser.yy.{cpp,h}` и `lexer.yy.{cpp,h}` кладутся в `<build>/syntax`.
Подключение — `cmake/generate_parsers.cmake`, цель `gen_syntax_parsers`.

## Facts and invariants

- **Списки терминалов не дублируются в грамматике:** источник — `term_types.h`; новый терминал
  добавляется там (x-macro), а не в `parser.y.in`.
- **`%expect 3` — намеренный контракт грамматики** (см. `syntax/parser/MEMORY.md`); изменение числа
  S/R-конфликтов требует осознанного решения, а не «подгонки».

## Relations

- `syntax/term/term_types.h` (терминалы), `cmake/generate_parsers.cmake`, `syntax/parser`, `syntax/lexer`.
