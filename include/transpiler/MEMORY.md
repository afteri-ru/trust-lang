# MEMORY.md

> scope: include/transpiler (public API)
> role: persistent-memory
> last_reviewed: 2026-11-13
> review_period: 30

## Architecture

`CppTranspiler` — единственный публичный API (`transpiler/transpiler.hpp`, флаги — `transpiler/emit/diag.hpp`):
фасад-драйвер, exhaustive consumer (`ast/kind_dispatch.hpp`); statement-only kind в `emitExpr` → FAULT. Вход — HIR;
данные анализа — только из полей узлов. Транспилятор — ТОЛЬКО кодогенерация (анализ структуры зашит lowering'ом).
Конструируется из порта `CodegenServices` (read-model реестра/целевого контракта + приёмники карты/диагностики),
а не из `Context` (R12/R39; порт собирает `Pipeline`). `CodegenServices` — ПСЕВДОНИМ единого порта прогона
`RunServices` (`session/run_services.hpp`); поэтому `session_lib` — PUBLIC-зависимость транспилятора.

| internal-подкаталог | MEMORY | ответственность |
|---|---|---|
| `transpiler/driver/` | — (в коде) | фасад-драйвер `CppTranspiler` (диспетчеризация kind, single-file предикат) |
| `transpiler/emit/` | есть | `CppEmitContext` и эмиттеры Decl/Stmt/Expr/Contract |
| `transpiler/type/` | есть | `TypeEmitter`/`TypeNameResolver`/`TypeIncludeCollector`: C++-имя, include/link/POD |
| `transpiler/module/` | есть | модульные юниты: заголовок модуля, out-of-line, umbrella-фасад |

## Facts and invariants

- **⚠ Вход — HIR** (клон AST + lowering): исходная AST не читается/не мутируется; `SemicolonStmt`, метки/goto,
  временные материализованы; MIR не подаётся. Данные анализа — только из полей узлов (`IdentName::resolvedTypeId`,
  `CallExpr::resolvedSignature`, `VarDecl::inferredType`); `SymbolTable` не передаётся; `-Wunhandled-attr` — по HIR.
- **Term-изоляция:** транспилятор не обращается к Term — только `MapperRange`/текст узлов-носителей.
- **⚠ Класс эмитит ЕДИНЫЙ генератор `DeclEmitter::emitCppClass(CppClassSpec)`:** record, обёртка нативного
  типа и генерируемый builtin-класс лишь СОБИРАЮТ спеку (keyword/namespaces/bases/preamble/поля/members/trailer);
  эмиссия stateful (offset'ы source-map через `mapDeclaredName`, инклюды, `DeclFacts`, `forwardDeclOnly`) —
  «чистой функции модель→текст» быть не может, но раздельных путей эмиссии класса нет. Члены — в порядке
  объявления; `public:/protected:/private:` — по `AstNodeBase::visibility`.
- **Наборы типов (`:A + :B`) кодоген не эмитит:** как ЗНАЧЕНИЕ — явная ошибка (без молчаливой пустой инициализации).
  Копий семантических фактов нет: форма/тип/имя — через аксессоры `types` (R1/R14).
- **⚠ Имена в `--stack-check-functions` / `stack_check_set_limit(...)` — ДАННЫЕ ДЛЯ ЛИНКЕРА:** кодоген
  эмитит C++-имя объявления при ЛОКАЛЬНОМ резолве, иначе — строку дословно (символ разрешит линкер);
  резолв — только раннее предупреждение `-Wstack-check-fn-symbol`, эмиссия от него не зависит (R53).
- **⚠ C++-имя нативной привязки кодоген НЕ хранит (R1/R12):** читается у владельца `target::DeclCppFacts`
  по `DeclId` (`CppEmitContext::nativeCppNameOf`), локальной копии нет. Предикат `@[matcher("fn")]` резолвит
  семантика (`MatchStmt::m_matcherDecl`), кодоген по тексту повторно НЕ резолвит (R53).

## Decisions

- **Временные переменные создаёт ТОЛЬКО анализатор (lowering)** — реальные узлы AST с уникальным именем и
  выведенным типом; исключения: entry в однофайловом «скрипт»-режиме, `x :=: _`, постфиксный `x++`/`x--`.
- **Кодоген НЕ зависит от анализатора (`transpiler → semantic` разорвана):** общие продукты — `include/analysis`;
  режимы передаются ДАННЫМИ; владелец флагов (`FlagKind`) — `semantic`.

## Relations

- `ast`, `semantic` (классификация, суффиксы), `types` (`TypeId`, шаблоны, ref-виды), `analysis`,
  `utils` (манглинг, ELF, source map), `package`, `pipeline`.
