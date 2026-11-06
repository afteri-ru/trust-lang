# Transpiler - Транспилятор в C++

## Назначение

Генерация C++ кода из HIR (производная от AST структурная форма: клон AST + lowering) после успешного
семантического анализа. Исходная AST не мутируется; для эмиссии читается HIR, а данные анализа — ТОЛЬКО
из материализованных полей узлов (`IdentName::resolvedTypeId`, `VarDecl::inferredType`,
`CallExpr::resolvedSignature`); `SymbolTable`/кэш модели в бэкенд не передаются. Транспилятор — только
кодогенерация: требующее анализа структуры (goto-метки, `;`, break/continue) зашито lowering'ом.

## Архитектура

- **Единственный публичный API** — `transpiler/transpiler.hpp` (`CppTranspiler`); флаги — `emit/diag.hpp`.
  `generateToFile()` пишет C++ прямо в `ctx.source().add_output()` (in-process, без внешнего компилятора)
  и строит source-map (стек `mapStart`/`mapStop`; имена — `addNameMapping`/`mapDeclaredName`). Строковая
  генерация в stdout отсутствует.
- **Exhaustive kind-dispatch:** контракт полноты — `static_assert(hasAllKindHandlers<CppTranspiler>())`
  (`ast/kind_dispatch.hpp`); `generateNodeToFile` → `dispatchEmit(node, *this)` с `KindCategory`;
  «недопустимый здесь» kind — `faultUnexpectedKind`, «пропустить» — `skipNode`; пустых тел нет.
- **Декомпозиция эмиттеров:** общий контекст `CppEmitContext` разделён на зоны состояния
  (`OutputState`/`EmitModeState`/`DeclFacts`/`EmissionAccumulators`/`UnitState`/`ResultBinding`);
  эмиттеры `TypeEmitter`/`DeclEmitter`/`StmtEmitter`/`ExprEmitter`/`ContractEmitter` разделяют
  ответственность. Детали — в `emit/MEMORY.md`, `type/MEMORY.md`, `module/MEMORY.md`.
- **Единая точка C++-имени — `cppName(trustName, CppEntityKind)`;** include/link собираются в аккумулятор
  и препендятся в начало файла единым `emitCollectedIncludes`.

- **Нет копий семантических фактов (R1/R14):** форма/тип/имя читаются через аксессоры `types`; параллельные векторы и ordinal coupling запрещены.

## См. также

- `MEMORY.md` — публичные правила и инварианты; `emit/MEMORY.md` — ресурсы эмиссии (переполнение,
  source-map, нативные, шаблоны, лямбды); `type/MEMORY.md` — C++-имена типов; `module/MEMORY.md` —
  модульные юниты и заголовки.
