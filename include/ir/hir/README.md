# HIR - производная от AST структурная форма (internal-подкаталог `ir/hir`)

## Назначение

Один структурный вход для бэкендов и flow-анализов: `HirModule` (`ir/hir_module.hpp`) владеет
деревом узлов, построенным клоном AST + lowering (`buildHir(astNodes, ctx)`). Строится в
`Pipeline` (`lowerAndAnalyze`: buildHir → `verifyHir` → DA/borrow) после семантики; `SemanticPassRunner`
выполняет только семантику. Internal-подкаталог расположен НИЖЕ `semantic`/`transpiler` и зависит от `ast`
(узлы, `cloneTree`) и `session` (`Context`); lowering (`LowerCtx`, `lowerBody`) принадлежит
этому internal-подкаталогу (`ir/lowering.hpp`, `src/ir/hir/lowering.cpp`), а не AST.

## Ключевые свойства

- **AST не мутируется.** Lowering выполняется на глубоком клоне (`ast/clone.hpp`), поэтому исходная
  AST остаётся pre-lowering и доступна solver'у, диагностикам и LSP после построения HIR.
- **Форма структурная, не SSA.** `if`/`while`/`match` остаются структурированными узлами;
  развёрнут только «сахар»: `SemicolonStmt`, метки/`goto`, временные анализатора.
- **Фазы — у `Pipeline`.** semantic → buildHir → verify → flow → codegen. HIR строится ровно один
  раз; HIR-фаза выполняется и на частичном AST (LSP recover), гейт верификатора — только на успехе.
- **`verifyHir` — единственный гейт контракта HIR:** нет recovery-узлов; тип-имена
  материализованы (`IdentName::resolvedTypeId`), левый операнд `MemberAccess` разрешён; метки
  `goto` корректны и не дублируются; именованные `break`/`continue` развёрнуты lowering'ом;
  `SemicolonStmt` не вкладывается; у нетипизированной переменной есть `inferredType`;
  `argCasts` согласован по арности; контролируемая арифметика классифицирована; синтетические
  временные уникальны. Диагностика синтетических узлов использует `origin`.
  Нарушение — ICE-политика `reportInternalError` (Fatal в CLI, Error в LSP).
- **`origin`** у узла HIR — узел исходной AST (nullptr у синтетических узлов lowering):
  единственное хранимое направление соответствия AST↔HIR (обратное выводится по требованию);
  используется верификатором для адресации диагностик синтетических узлов.
- **Один владелец и один вход.** `HirModule` неизменяем после построения; потребители —
  `transpiler` (`generateToFile(const HirModule&)`) и `ir` (CFG/DA/borrow). `--emit-ast`
  семантику не запускает, поэтому HIR там не строится (печатается дерево разбора).
- **Диагностики — не здесь.** Пользовательские диагностики выдаёт анализ/lowering в общий `Context`;
  HIR-билдер только строит форму.
