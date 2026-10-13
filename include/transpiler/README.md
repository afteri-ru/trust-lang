# Transpiler - Транспилятор в C++

## Назначение

Генерация C++ кода из HIR (производная от AST структурная форма: клон AST + lowering) после успешного семантического анализа. Преобразует узлы HIR (декларации переменных, литералы, type aliases, функции, выражения) в синтаксически корректный C++ код. Исходная AST не мутируется; для эмиссии читается HIR, а данные анализа — ТОЛЬКО из материализованных полей узлов (`IdentName::resolvedTypeId`, `VarDecl::inferredType`, `CallExpr::resolvedSignature`); `SymbolTable`/кэш модели в бэкенд не передаются.

## Особенности реализации

- **Единственный метод генерации** - `generateToFile()` записывает C++ код в выходной файл (созданный через `ctx.source().add_output()`) с построением source-map (RangeMap trust→cpp через стек mapStart/mapStop). Строковая генерация в stdout отсутствует.
- **In-process** - транспиляция выполняется внутри процесса компиляции (через Context), без вызова внешнего компилятора.
- **Source-map** - для каждого узла HIR создаётся маппинг trust-range → cpp-range; имена переменных, функций, типов и параметров функций дополнительно маппятся через `addNameMapping` для hover-ссылок (единый хелпер `mapDeclaredName`). Source map встраивается в ELF-секцию `.debug_trust_map` на этапе компиляции C++ (через `ModuleApi::packToMsgpack()`), внешние `.trust` файлы не используются.
- **Exhaustive consumer (kind-dispatch)** - `CppTranspiler`: контракт полноты задаёт `static_assert(hasAllKindHandlers<CppTranspiler>())` (`ast/kind_dispatch.hpp`), диспетчер - `generateNodeToFile` → `dispatchEmit(node, *this)`; `visit_<Kind>` - тонкие делегирования в компоненты-эмиттеры. `dispatchEmit` получает `KindCategory`: statement-only kind в `emitExpr` отвергается; expression-kind допустим как standalone-выражение в блоке/на верхнем уровне. «Недопустимый здесь» kind - явный `faultUnexpectedKind` (FAULT с именем kind), «осознанно пропустить» - `skipNode`; пустых тел нет (`ErrorExpr` → fault). **Декомпозиция**: общий контекст `CppEmitContext` (`emit/emit_ctx.hpp`) разделён на именованные зоны состояния (`OutputState` — вывод/области имён, `EmitModeState` — режимы точки эмиссии, `DeclFacts` — факты объявлений, `EmissionAccumulators` — инклуды/типы/линковка, `UnitState` — entry/single-file, `ResultBinding`); счётчики временных принадлежат своим эмиттерам. Эмиттеры `TypeEmitter`/`DeclEmitter`/`StmtEmitter`/`ExprEmitter`/`ContractEmitter` (`*_emit.hpp`) разделяют ответственность (типы/инклуды, декларации, операторы, выражения, trust-контракты); `TypeEmitter` — фасад над `TypeNameResolver` (рендеринг C++-имени) и `TypeIncludeCollector` (include/link/POD-проверки); `TypeNameResolver::resolveCppTypeId` диспетчеризует по единственному дискриминатору — альтернативе `TypeData` (`std::visit`, необработанный вариант не компилируется); объявления разрезаны по владельцу: `emit/decl_emit.cpp` (visitor'ы), `type_emit/decl_emit.cpp` (типы), `module/module_emit.cpp` (модульные юниты), общие помощники — `decl_emit/emit_common.hpp`; операторы — `emit/stmt_emit.cpp` (блоки/области/with/try/jump) и `emit/stmt_control_emit.cpp` (if/while/do/match/switch); общие помощники - `emit/emit_common.hpp`. Обход тела блоков - `emitBlockBodyToFile`/`emitBodyNode`/`emitSequenceBody`. Отступ хранится в стеке `m_output.scopeStack` (`ScopeContext{indent}`). Имя функции транспилятору не нужно (его заменяют синтетические узлы lowering); единственный флаг контекста - `m_mode.inCppBlock` (внутри C++ compound statement), определяющий форму эмиссии пользовательских блоков (см. «Блоки и области видимости»).
- **Потоковый вывод** - кодек пишет C++ текст прямо в `m_ctx.source()`, без возврата `std::string`. Единая диспетчеризация ПО KIND: `emitExpr` - это `dispatchEmit(*node, *this)` с инкрементом `m_mode.exprDepth`. Точка с запятой для statement-выражений НЕ выводится транспилятором по контексту: statement-позиции выражений обёрнуты анализатором в `SemicolonStmt`, и `visit_SemicolonStmt` добавляет `;` + source-map (statement-root, без скобок). Вложенные выражения (`m_mode.exprDepth>0`) - только текст, бинарные - в скобках. Source-map строится естественно по позиции в выходном файле; для тестов строка читается из выходного файла (`output_result`).
- **Приведения аргументов/операндов — данные семантики** - `emitCastCallArgs` (нативные
  перегрузки, свободные операторы, `operator()`) и `emitOperatorDispatch` печатают
  `static_cast` по `CallExpr::argCasts`/`Binary::argCasts`; `FunctionTypeData` для выбора
  приведения в этих путях не читается. Слот `INVALID_TYPE_ID` в `CallExpr` = без приведения
  (Any/неизвестный параметр); `Binary::argCasts` заполняется только для пути
  `emitOperatorDispatch` (member 1/free 2); inc/dec, type-check, swap и `ArrayAccess`/`RefTakeExpr`
  имеют свои эмиттеры и эти слоты не читают.
- **Единые хелперы** - `emitTypeNameForNode`/`emitTypeName`/`resolveCppTypeId` (рендер типа: материализованный `resolvedTypeId` → canonical → C++-имя → include), `mapDeclaredName` (расчёт cpp-оффсета имени) используются несколькими `generate*ToFile`, снижая дублирование; бинарные операторы (`//`/`//=` integer-division) обрабатываются единообразно в `emitBinaryOpRaw`; пары mapStart/mapStop обёрнуты RAII `MapperScope`. Codegen обращается к диапазонам/именам только через методы узлов AST (`range()`, `nameRange()`, `blockRange()`, `text()`), не трогая `Term` напрямую.
- **Пользовательские алиасы сохраняют имя** - `resolveCppTypeId` для пользовательского алиаса (`TypeSystem::isUserDefinedType`) возвращает trust-имя (`MyInt`, `Big`), для машинных и встроенных алиасов - каноническое C++-имя (`int64_t`, `std::string`). Include всегда от канонического типа. Нераспознанный/структурный тип - **явная ошибка** через `m_ctx.report(range, OptKind::ParseError, ...)`, fallback (`auto`, исходное имя) запрещён.
- **Pipeline position** - расположен после SemanticAnalyzer; не запускается при наличии семантических ошибок.
- **Не зависит от анализатора (`transpiler → semantic` разорвана)** - кодоген читает только HIR
  (факты материализованы в полях узлов); value-типы режимов живут в `include/analysis`, поведенческие
  режимы (solver/stack-check) передаются как данные (`CppEmitContext::m_behavioral`), а не читаются из
  `diag::Options`. `transpiler_lib` не линкует `semantic_lib`.
- **Манглинг имён** - единая точка C++-имени: `cppName(trustName, CppEntityKind, moduleNamespace)`
  (`emit/emit_common.hpp`) для типов/функций/значений (`ns::Point → c_ns::c_Point`), `utils::qualifiedCppName` /
  `utils::cppDeclName` / `utils::cppIdent` (один сегмент) и `utils::cppNamespacePath` (namespace-
  сегменты). Нативные имена с ведущим `%` остаются C++-символами без изменений (`%add → add`).
  Модульная изоляция — автоматический `namespace <enc>` для ВСЕХ сущностей импортируемого юнита
  (entry `__main__` и top-level `{% %}` — вне конверта); headerless root не изолируется. Номинальное C++-имя типа строит `printType(kCpp)` (namespace-путь и модуль из
  storage-ключа). Вставки `{% ... %}` оставляют C++-текст как есть, кроме маркеров `$name`/`@name`;
  их доступность и модуль имени резолвит семантический анализатор (см. semantic/MEMORY.md).
- **Два механизма подключения инклудов** - (1) *по типу*: во время обхода AST в `m_accumulators.usedTypes`
  собираются только использованные типы (канонические `TypeId`), а директивы из них
  (`TypeSystem::preprocIncludes`) формируются **после** обхода (`collectTypeIncludes`);
  (2) *по рантайм-символу*: для EMBED-узлов и рантайм-функций, где типа нет, заголовки
  подключаются через ЕДИНСТВЕННЫЙ метод `recordRuntimeSymbolHeaders(RuntimeSymbolId)`
  (строковой перегрузки нет). Единый источник имён/заголовков символов - компайлтайм-таблица
  `types/runtime_symbols.hpp` (`RuntimeSymbolId`): известные рантайм-функции кодогенерации передают id
  напрямую (`RuntimeSymbolId::kAnyTo`/`kCheckedCast`); callee вызова резолвится через
  `findRuntimeSymbolByName`; текст EMBED сканируется хелпером `recordRuntimeSymbolsInText`.
  Опечатка в имени символа невозможна. В конце `generateToFile` `collectTypeIncludes()` +
  `emitCollectedIncludes` препендят все директивы в начало файла.


## Блоки и области видимости

`{ ... }` - последовательность операторов, оформленная как одно единое выражение. По метке
перед `{` `visit_ScopeBlock` выбирает C++-эмиссию, сохраняя область видимости:

- **Безымянный блок кода** `{ ... }` (без метки) - только внутри функции/класса → `{ ... }`.
- **Именованная метка** `label { ... }` (одно имя без `::`) - только внутри функции → `{ ... }`
  + метки `label_break:`/`label_continue:` (из lowering).
- **Область имён** `ns:: { ... }` - верхний уровень модуля (вложенность ок, НЕ внутри
  функций/классов) → `namespace ns { ... }`.
- **Глобальная область** `:: { ... }` - верхний уровень → содержимое без namespace-обёртки.
- **Анонимная область имён** `_ { ... }` - верхний уровень → `namespace { ... }`, подавляет экспорт.

Валидность контекста проверяется в транспайлере: безымянный блок/метка вне функции и область
имён внутри функции - диагностика.

**Экспорт** - только имена на верхнем уровне модуля в НЕ анонимной области имён (глобальная `::`
и именованные `ns::`, квалифицированно `ns::x`); из `_` и локальных - не экспортируются.

Контейнер тела модуля (SEQUENCE-терм) разворачивается в top-level операторы (не создаётся
ScopeBlock-обёртка); тело функции уже плоское и эмитится напрямую.


## Генерируемые конструкции

- `VarDecl` - `std::any x = 42;`, типизированные `x:Int32 := 42` → `int32_t x = 42;`
  Квалификаторы из атрибутов: `@[readonly]`/`^` → `const int32_t x = 42;`, `@[thread_local]` → `thread_local int32_t x = 42;`
- `TypeDecl` (`::=`) - `using MyInt = int32_t;`
- `FuncDecl` - сигнатура и тело, forward declaration (`;`), возврат `return expr;`
  Квалификаторы из атрибутов: `@[func_const]` → `__attribute__((const))`, `@[func_pure]` → `__attribute__((pure))`,
  `@[constexpr]` → `constexpr`, `@[noexcept]` → завершающий `noexcept`
- Expression statements - присваивания `=`, `+=`, `-=`, `*=`, `/=`, `%=`, `//=`, целочисленное
  деление/остаток `//`/`%` → проверяемая форма (`static_cast<int64_t>(...)` + контроль нулевого
  делителя и `INT_MIN / -1`)
- `JumpStmt` - `return expr;`, `throw expr;`
- `IfStmt` - `if (cond) { ... } else if (cond2) { ... } else { ... }`
- `WhileStmt` - `while (cond) { ... }`, с else - `while (cond) { ... } else { ... }`
- `DoWhileStmt` - `do { ... } while (cond);`
- `WithStmt` - RAII-менеджер `with(a=expr, b=expr){...} else {...}`. Биндинги — локальные VarDecl,
  видимы в теле; else выполняется при исключении в инициализаторе (объекты уже уничтожены), исключение
  в теле пробрасывается. Флаг «все биндинги захвачены» (`bool _wN := 0;`) создаёт LOWERING (временные —
  уровень анализатора), эмитится через emitSyntheticVar:
  `bool _wN = 0; try { a=...; b=...; _wN = true; {тело} } catch(...){ if(_wN) throw; {else} }`; без else —
  простой блок `{ a=...; b=...; {тело} }`.
- `BreakStmt`/`ContinueStmt` - после lowering только безымянные: `break;`/`continue;`; именованные уже переписаны анализатором в `GotoStmt` (`goto <имя>_break/_continue;`) или void-`ReturnStmt` (`return;`)
- `GotoStmt`/`LabelStmt` - синтетические узлы lowering: `goto <метка>;` / `<метка>:;` (без source-map)
- `SemicolonStmt` - statement-выражение: выражение без скобок + `;`
- `MatchStmt` - временная переменная + `if (_m == p1 || _m == p2) { ... } else { ... }`
- Standalone literals и `EmbedExpr` (`{% ... %}`) - выводятся как есть с source mapping