# Semantic - Семантический анализатор

## Назначение

Однопроходный семантический анализ AST: единое ядро разрешения имён (`NameResolutionPass`) и
параллельные анализаторы (`InlineAnalysisHook`) под feature-флагами. Результат — материализованные в
полях узлов AST факты, которые читают кодогенерация и производные представления.

## Архитектура

- **Однопроходность:** имя обязано быть объявлено до использования. Ядро — драйвер обхода и скоуп-стека;
  тяжёлая логика — в анализаторах (`DeclAnalyzer`/`ExprTyper`/`AccessResolver`/`TrustAnalyzer`),
  разделяющих `AnalysisContext`.
- **Опциональные анализаторы** подключаются к ядру как хуки (события `enterScope`/`exitScope`/`onDeclare`/
  `onResolve`/`onNode`/`finalize`); `SemanticPassRunner` содержит только упорядоченный реестр фабрик,
  условие включения объявляет сам хук.
- **Факты — в ПОЛЯХ узлов AST** (единственная точка чтения/записи type-state — `ExprFacts`);
  согласованность проверяет `semantic::verifyTypedAst`; кодоген не нуждается в `SymbolTable`.
- **Общие продукты** (`SymbolTable`/`Symbol`/`SymbolIndex`) — в компоненте `analysis`;
  поведенческие режимы (solver/stack-check) — value-типы в `analysis/modes.hpp`.
- **HIR и flow-анализы — НЕ в `run()`:** их выполняет `Pipeline` (LowerHir → BuildMir → DA/borrow по MIR);
  исходная AST после прогона неизменяема (кроме ErrorExpr-восстановления).

- **Проверка — в точке записи:** `verifyTypedAst` покрывает только корневой AST; инварианты формы импортируемых модулей проверяет писатель (R13).

## Документация

- `MEMORY.md` — публичные правила; `core/MEMORY.md`, `decl/MEMORY.md`, `typer/MEMORY.md`,
  `ref/MEMORY.md`, `verify/MEMORY.md` — детали зон.
