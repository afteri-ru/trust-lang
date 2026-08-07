# Transpiler — Транспилятор в C++

## Назначение

Генерация C++ кода из AST и SymbolTable после успешного семантического анализа. Преобразует узлы AST (декларации переменных, литералы, type aliases, функции, выражения) в синтаксически корректный C++ код.

## Особенности реализации

- **Единственный метод генерации** — `generateToFile()` записывает C++ код в выходной файл (созданный через `ctx.source().add_output()`) с построением source-map (RangeMap trust→cpp через стек mapStart/mapStop). Строковая генерация в stdout отсутствует.
- **In-process** — транспиляция выполняется внутри процесса компиляции (через Context), без вызова внешнего компилятора.
- **Source-map** — для каждого узла AST создаётся маппинг trust-range → cpp-range; имена переменных, функций, типов и параметров функций дополнительно маппятся через `addNameMapping` для hover-ссылок (единый хелпер `mapDeclaredName`). Source map встраивается в ELF-секцию `.debug_trust_map` на этапе компиляции C++ (через `ModuleApi::packToMsgpack()`), внешние `.trust` файлы не используются.
- **Наследник KindVisitor** — `CppTranspiler : KindVisitor` (`ast/kind_visitor.hpp`); `generateNodeToFile` → `dispatchKind(node, *this)`, `visit_<Kind>` — члены класса (compile-time проверка полноты: новый kind не обработан → не компилируется). Обход тела блоков — `emitBlockBodyToFile`/`emitSequenceBody`. Отступ хранится в стеке `m_scopeStack` (`ScopeContext{indent}`). Имя функции транспилятору не нужно (его заменяют синтетические узлы lowering); единственный флаг контекста — `m_inCppBlock` (внутри C++ compound statement), определяющий форму эмиссии пользовательских блоков (см. «Блоки и области видимости»).
- **Потоковый вывод** — кодек пишет C++ текст прямо в `m_ctx.source()`, без возврата `std::string`. Единая диспетчеризация ПО KIND: `emitExpr` — это `dispatchKind(*node, *this)` с инкрементом `m_exprDepth`. Точка с запятой для statement-выражений НЕ выводится транспилятором по контексту: statement-позиции выражений обёрнуты анализатором в `SemicolonStmt`, и `visit_SemicolonStmt` добавляет `;` + source-map (statement-root, без скобок). Вложенные выражения (`m_exprDepth>0`) — только текст, бинарные — в скобках. Source-map строится естественно по позиции в выходном файле; для тестов строка читается из выходного файла (`output_result`).
- **Единые хелперы** — `resolveCppType` (разрешение типа: findType→canonical→cppName→include), `mapDeclaredName` (расчёт cpp-оффсета имени) используются несколькими `generate*ToFile`, снижая дублирование; бинарные операторы (`//`/`//=` integer-division) обрабатываются единообразно в `emitBinaryOpRaw`; пары mapStart/mapStop обёрнуты RAII `MapperScope`. Codegen обращается к диапазонам/именам только через методы узлов AST (`range()`, `nameRange()`, `blockRange()`, `text()`), не трогая `Term` напрямую.
- **Пользовательские алиасы сохраняют имя** — `resolveCppType` для пользовательского алиаса (`TypeRegistry::isUserDefinedType`) возвращает trust-имя (`MyInt`, `Big`), для машинных и встроенных алиасов — каноническое C++-имя (`int64_t`, `std::string`). Include всегда от канонического типа. Нераспознанный/структурный тип — **явная ошибка** через `m_ctx.report(range, OptKind::ParseError, ...)`, fallback (`auto`, исходное имя) запрещён.
- **Экспорт символов** — `exports()` возвращает список `ExportEntry{trustName, cppName}`, собранный в процессе генерации (например для `x := 42` и `%func() ::= {}`).
- **Pipeline position** — расположен после SemanticAnalyzer; не запускается при наличии семантических ошибок.
- **Манглинг имён** — все trust-идентификаторы в C++-выводе конвертируются через единый конвертер
  `utils::name_to_cpp`: `x → c_x`, `MyInt → c_MyInt`, `ns → c_ns`; нативные имена с ведущим `%`
  срезаются и остаются без изменений (`%add → add`). Вставки `{% ... %}` оставляют C++-текст как есть,
  кроме маркеров `$name`/`@name` (trust-имена → `name_to_cpp`); их доступность проверяет семантический
  анализатор (см. semantic/MEMORY.md).
- **Два механизма подключения инклудов** — (1) *по типу*: во время обхода AST в `m_usedTypes`
  собираются только использованные типы (канонические `TypeId`), а директивы из них
  (`TypeRegistry::preprocIncludes`) формируются **после** обхода (`collectTypeIncludes`);
  (2) *по рантайм-символу*: для EMBED-узлов и рантайм-функций, где типа нет, заголовки
  подключаются через ЕДИНСТВЕННЫЙ метод `recordRuntimeSymbolHeaders(RuntimeSymbolId)`
  (строковой перегрузки нет). Единый источник имён/заголовков символов — компайлтайм-таблица
  `types/runtime_symbols.hpp` (`RuntimeSymbolId`): известные рантайм-функции кодогенерации передают id
  напрямую (`RuntimeSymbolId::kAnyTo`/`kCheckedCast`); callee вызова резолвится через
  `findRuntimeSymbolByName`; текст EMBED сканируется хелпером `recordRuntimeSymbolsInText`.
  Опечатка в имени символа невозможна. В конце `generateToFile` `collectTypeIncludes()` +
  `emitCollectedIncludes` препендят все директивы в начало файла.


## Блоки и области видимости

`{ ... }` — последовательность операторов, оформленная как одно единое выражение. По метке
перед `{` `visit_ScopeBlock` выбирает C++-эмиссию, сохраняя область видимости:

- **Безымянный блок кода** `{ ... }` (без метки) — только внутри функции/класса → `{ ... }`.
- **Именованная метка** `label { ... }` (одно имя без `::`) — только внутри функции → `{ ... }`
  + метки `label_break:`/`label_continue:` (из lowering).
- **Область имён** `ns:: { ... }` — верхний уровень модуля (вложенность ок, НЕ внутри
  функций/классов) → `namespace ns { ... }`.
- **Глобальная область** `:: { ... }` — верхний уровень → содержимое без namespace-обёртки.
- **Анонимная область имён** `_ { ... }` — верхний уровень → `namespace { ... }`, подавляет экспорт.

Валидность контекста проверяется в транспайлере: безымянный блок/метка вне функции и область
имён внутри функции — диагностика.

**Экспорт** — только имена на верхнем уровне модуля в НЕ анонимной области имён (глобальная `::`
и именованные `ns::`, квалифицированно `ns::x`); из `_` и локальных — не экспортируются.

Контейнер тела модуля (SEQUENCE-терм) разворачивается в top-level операторы (не создаётся
ScopeBlock-обёртка); тело функции уже плоское и эмитится напрямую.


## Генерируемые конструкции

- `VarDecl` — `std::any x = 42;`, типизированные `x:Int32 := 42` → `int32_t x = 42;`
  Квалификаторы из атрибутов: `@[readonly]`/`^` → `const int32_t x = 42;`, `@[thread_local]` → `thread_local int32_t x = 42;`
- `TypeDecl` (`::=`) — `using MyInt = int32_t;`
- `FuncDecl` — сигнатура и тело, forward declaration (`;`), возврат `return expr;`
  Квалификаторы из атрибутов: `@[func_const]` → `__attribute__((const))`, `@[func_pure]` → `__attribute__((pure))`,
  `@[constexpr]` → `constexpr`, `@[noexcept]` → завершающий `noexcept`
- Expression statements — присваивания `=`, `+=`, `-=`, `*=`, `/=`, `%=`, целочисленное деление `//` → `static_cast<int64_t>(...) / ...`
- `JumpStmt` — `return expr;`, `throw expr;`
- `IfStmt` — `if (cond) { ... } else if (cond2) { ... } else { ... }`
- `WhileStmt` — `while (cond) { ... }`, с else — `while (cond) { ... } else { ... }`
- `DoWhileStmt` — `do { ... } while (cond);`
- `BreakStmt`/`ContinueStmt` — после lowering только безымянные: `break;`/`continue;`; именованные уже переписаны анализатором в `GotoStmt` (`goto <имя>_break/_continue;`) или void-`ReturnStmt` (`return;`)
- `GotoStmt`/`LabelStmt` — синтетические узлы lowering: `goto <метка>;` / `<метка>:;` (без source-map)
- `SemicolonStmt` — statement-выражение: выражение без скобок + `;`
- `MatchStmt` — временная переменная + `if (_m == p1 || _m == p2) { ... } else { ... }`
- Standalone literals и `EmbedExpr` (`{% ... %}`) — выводятся как есть с source mapping