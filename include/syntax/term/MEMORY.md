# MEMORY.md

> scope: include/syntax/term
> role: persistent-memory
> last_reviewed: 2026-11-05
> review_period: 30
> max_size: 3000

## Architecture

Реализация Term-дерева (`term.cpp`): `Term` — узел с `m_id` (`TermID`), текстом (`std::variant<std::string, std::string_view>`),
аргументами `m_args` (`ArgsList` = список пар «имя, значение»), последовательностью `m_sequence` и
необязательными слотами `m_type`/`m_template`/`m_attr`/`m_docs`. Реализация Term-дерева (`src/syntax/term/term.cpp`) и его заголовки (`term.h`, `term_types.h`).

## Facts and invariants

- **⚠ Единственный источник TermID — x-macro `TERMS` в `term_types.h`** (терминалы и AST-узлы в одном
  списке). Новый терм добавляется только там; `Term::tokenFromID`/`symbolToID` — проекции того же списка.
- **⚠ `getText()` и `m_mapperRange` — независимые факты.** При раскрытии макроса spelling остаётся из
  определения, range переприсваивается на call-site (или становится невалидным). Выводить текст из range
  нельзя и наоборот.
- **Владение текстом:** source-backed терм держит `string_view` в буфер `SourceMapWriter` и валиден только
  пока жив источник; `setTextOwned`/`mutableText` переводят текст в owned `std::string` (единственный путь
  записи). `Clone()` сохраняет природу текста; интернирование в пул строк делает конвертация в AST.
- **`m_sequence` — единое поле последовательности:** и верхнеуровневый `sequence`, и вложенные
  `{ ... }`/тела конструкций. Отдельного поля тела нет.

## Relations

- `syntax/parser`, `syntax/lexer`, `syntax/macro` (создание/мутация термов), `ast/convert` (Term→AST),
  `location` (`MapperRange`, `ModuleIndex`).
