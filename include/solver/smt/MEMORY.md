# MEMORY.md

> scope: include/solver/smt (+ src/solver/smt)
> role: persistent-memory
> last_reviewed: 2026-11-05
> review_period: 30
> max_size: 3000

## Architecture

Операторы SMT-LIB 2 (`smt_op.hpp`: таблицы знаков/сортов) и печать скрипта (`smt_printer.cpp`).
Публичные заголовки — корень `solver/` (`smt_ast.hpp`, `smt_printer.hpp`).

## Facts and invariants

- **⚠ Оператор объявлен ОДИН раз:** единый список порождает `enum SmtOp`, `smtOpName` и `parseSmtOp`;
  пользовательские функции живут в `SmtTerm::fun_name`, у них `op = nullopt`.
- **⚠ Экранирование SMT-имён:** имя с недопустимыми символами печатается как `|...|`
  (`escapeSymbol`); допустимы `[a-zA-Z0-9_+*/%?!.$~&^<>=:#-]`.
- **⚠ Карта .smt2 → trust-источник (`buildSmt2Map`):** диапазон печатается как
  `файл:строка:кол-строка:кол` с 1-based отсчётом; символ, отсутствующий в `.smt2`, в карту
  не попадает; команда i идёт на строке `(logic ? 2 : 1) + i`.
- **⚠ Квантификаторы:** `quant_var_sorts` обязан быть параллелен `quant_vars` по индексу.

## Relations

- `solver/impl` (решатели), `verify` (ProofBackend), `pipeline/backend`.
