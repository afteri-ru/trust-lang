# MEMORY.md

> scope: include/transpiler
> role: persistent-memory
> last_reviewed: 2026-08-20
> review_period: 30
> max_size: 65000

# C++ Transpiler Architecture

## Purpose
Generates C++ code fragments from validated AST and symbol table.
Called after semantic analysis succeeds.

## Манглинг trust-имён в C++-идентификаторы

Все trust-идентификаторы, попадающие в C++-вывод, конвертируются в корректные
C++-идентификаторы через единый конвертер `utils::name_to_cpp` (`utils/strings.hpp`):
- простые ASCII-имена получают префикс `c_` (`x → c_x`, `MyInt → c_MyInt`, `ns → c_ns`);
- юникод-имена транслитерируются / HEX-кодируются (`привет → ru_privet`);
- `::`-квалификация кодируется как `cpp_std$$vector`.
- **Нативные имена с ведущим `%`** (`%add`, `%std::max`) - это уже C++-символы рантайма:
  сам `name_to_cpp` срезает `%` и оставляет имя без изменений (`%add → add`), иначе ломается
  линковка. Обратный `cpp_to_name` для имени без префикса восстанавливает маркер `%`
  (`add → %add`), обеспечивая round-trip.

### Две формы нативных функций: forward-декларация и импорт

Принципиально различаются:

- **Предварительное объявление (forward-declaration)** - `%name(...):T := ...;`. Нативное имя
  (`%sqrt`) - это и есть C-style идентификатор (`sqrt`); имя и аргументы совпадают с нативной
  сигнатурой (адаптации нет), реализацию линковщик ищет по имени.
- **Импорт** - `name(...):T := %native...;`. Trust-имя слева связывается с нативным именем справа;
  имя и аргументы **могут отличаться**. Это **алиас**, а не новая функция: C++-функция `name` НЕ
  эмитится, а вызовы `name(...)` переписываются в прямой вызов `native(...)` (см. `m_nativeImports`
  в транспиляторе). `...` после нативного имени = «аргументы вызова повторяют аргументы слева»;
  в общем случае аргументы задаются явно и при генерации вставляется вызов правой функции
  (при необходимости с `static_cast` для приведения аргументов). Атрибут `@[link]` ставится
  на импорт (обычное trust-имя слева), а не на forward-декларацию.

### Линковка нативной декларации: `extern "C"` vs C++-линковка

Правило определяется наличием `::` в нативном имени (после среза `%` через `name_to_cpp`):

- **без `::`** (`%sqrt`, `%abs`, `%open`) - это C-символ libc/libm: forward-декларация эмитится
  с `extern "C"` (`extern "C" Float64 sqrt(Float64);`), чтобы линковщик нашёл символ с
  C-линковкой (без неё такие C-символы не линкуются).
- **с `::`** (`%std::sqrt`) - C++-линковка: `extern "C"` НЕ добавляется.

`extern "C"` добавляется **только forward-декларациям (без тела)** - это настоящие C-символы
libc/libm. **Определения (с телом)** - пользовательские C++-функции, `extern "C"` для них неверен
(например `auto`-возврат кортежа несовместим с C-линковкой → warning `-Wreturn-type-c-linkage`)
и не нужен: при наличии одноимённого forward-объявления определение наследует C-линковку по
правилу [dcl.link], поэтому связка «forward + определение» остаётся консистентной без явного
`extern "C"` на определении. Импорт-алиасы (`name(...) := %sym...;`) кодогенерацию не эмитят и
этого не касаются. `extern "C"` добавляется к ведущему префиксу сигнатуры (`lead`), поэтому
source-map смещения имён/параметров учитывают его автоматически (`lead.length()` уже входит в расчёт).

### Атрибут `@[link("имя")]` - линковка нативных библиотек

Нативная декларация может нести атрибут `@[link("имя")]@` - имя библиотеки, которое
добавляется к линковке как `-l<имя>`. Это единственное назначение атрибута: существование
символа/библиотеки **не проверяется** (ответственность линковщика), используется только
статическая линковка. Транспилятор собирает собранные `-l<имя>` в `std::set<std::string>`
`m_linkLibs` (метод `collectLinkLib`, вызывается в `generateFuncDeclToFile`/`generateVarDeclToFile`
для каждого объявления с атрибутом), доступен pipeline через `linkLibs()`. Имя библиотеки
читается из `AstNodeAttr::attr_args(attr::Link)` - список строковых аргументов атрибута,
сохранённый конвертером Term→AST в `convertAttrsToNode` (по `@[link("имя")]` хранится один
аргумент - имя библиотеки).

Манглинг применяется к: именам переменных, функций, параметров, типов-алиасов,
использованию пользовательских алиасов (`resolveCppTypeId` для `isUserDefinedType`),
идентификаторам в выражениях (`visit_Ident`) и именам областей имён
(`namespaceCppName` → `ns → c_ns`). В маппингах имён (`mapDeclaredName`) trust-имя
сохраняется исходным, C++-имя - манглированным; длины trust-range считаются по сырому
trust-имени (без `%`).

### Вставки C++ (EmbedExpr) и маркеры `$`/`@`
`{% ... %}` эмитится как есть, **кроме** идентификаторов с префиксами `$` и `@`,
которые интерпретируются как trust-имена и конвертируются через `name_to_cpp`
(`transform_embed_cpp`): `$name` - локальное имя, `@name` - имя (допускается `::`).
`name_to_cpp` срезает ведущий `$`-сигил локальной переменной (как `%` у нативных), поэтому
и нормализованная локальная `$x`, и embed-ссылка `{% $x %}` отображаются в `c_x`.
Чтение имени - через `extract_name` (единая точка входа; включает юникод и `::`).
Trust-имена из вставок дополнительно валидируются семантическим анализатором
(см. semantic/MEMORY.md): отсутствующее имя → предупреждение.
В грамматике `embed` - первичное выражение (альтернатива в `factor`), поэтому
`{% ... %}` допустимо и как аргумент вызова / подвыражение, а не только как
инициализатор/условие. `condition` при этом - просто `logical` (embed уже его часть).

### Доступ к элементам словаря (d.two / d.1 / d[0])
Грамматика строит узлы доступа: `d[expr]` - INDEX→ArrayAccess, `d.name`/`d.1` - FIELD→
MemberAccess (объект в m_left, ключ/индекс в m_right; у INDEX все индексы в m_args).
Литерал `(1, two=2, name='3',)` эмитит `trust::Dict{{"", TypedValue{kind, bool(1)}}, {"two",
TypedValue{kind, int8_t(2)}}, ...}`. Контракт: все элементы m_body - единый узел `ArgNode` (имя в text(),
значение в m_value), строятся из канонических пар грамматики `args` (см. `term_to_ast::visit_DICT`);
(единая форма ArgNode, без raw/AssignOp/ParamDecl). `kind` - **TypeKind значения** из **единого источника** -
`getKindFromId(tid)`, где `tid` = `ArgNode::resultType` элемента (семантика сохраняет из `resolvedType`,
покрывает литералы, вложенные словари и выводимые выражения, не только `Literal::typeId`). Значение -
точный C++-тип из единого источника `TypeRegistry::getCppTypeName` (`emitTypedDictValue`), без ручного
маппинга группа→имя; предикат литералов - `ast::is_literal_kind`; экранирование имён -
`utils::escape_cpp_string`. Доступ (`emitDictElementAccess`): для поля с конкретным типом (выведен
семантикой из литерала) - `std::any_cast<Cpp>(obj.at(key).value)`; для Any/неизвестного -
`obj.at(key)` (TypedValue). Каст `:Type(d.field)`: конкретный тип поля → `trust::checked_cast<Type>`
(контроль диапазона); тип поля Any/неизвестен → `trust::any_to<Type>(expr)` (диспетчеризация по
`kind`, конверсия с контролем диапазона, диагностика неподдерживаемого каста). Для числового
операнда - `trust::checked_cast`.
Заголовки: `trust/dict.hpp`, `trust/any_convert.hpp`.

### Оператор добавления элемента (X []= v)
`[]=` (APPEND, kind `AppendStmt`) - аналог `push_back`: `X []= v` → `X.push_back(v)` (X - простой
контейнер; вложенный LHS `d['x'] []= v`/`d[0] []= v`/`d.field []= v` отклонён семантикой как
«не реализовано»). Ветка выбирается по каноническому C++-типу контейнера (`Binary::lhsType`,
`resolveCppTypeId`): `trust::Dict` → `d.push_back("", TypedValue{kind, v})` (пустое имя =
позиционный элемент; `Dict` имеет только двухаргументный `push_back(name, value)`), значение - через
`emitTypedDictValue`; `std::string`/`std::wstring` → `.append(...)` (узкий литерал `'c'` в wide-контейнер
расширяется до `L"c"`). Ширина строки RHS должна совпадать с типом строкового контейнера
(проверка в семантике). `AppendStmt` - expression-statement (оборачивается `SemicolonStmt`).

**Spread-merge** `X []= ... dict` (RHS - узел `Ellipsis`, `Sequence`; операнд в `m_body[0]`) - добор
всех элементов словаря-операнда: для `trust::Dict`-цели → `X.extend(<операнд>)` (метод рантайм-типа
`trust::Dict::extend`, безопасный для self-append). Операнд - литерал (эмитится через
`visit_DictLiteral` как `trust::Dict{...}`), переменная-словарь (`c_d2`) или выражение (его значение).
Допустимо только для контейнера-словаря (проверка в семантике).

**Деструктуризация `t1, ..., tN := [... ]source;`** - узел `DestructureDecl` (`ast_nodes`): цели слева
(имена как `IdentName`, построены напрямую из термов lval; `m_isSpread` - был ли RHS `...`; суффикс `...`
у цели (`rest...`, C++-pack) - «остаток»; `_...` - отброс остатка), источник справа. **Без маркера -
точная привязка** (как кортеж): каждая цель - один элемент. **Spread** (`... source`, Dict) -
`visit_DestructureDecl`/`emitDestructureDict`: источник-выражение оценивается ОДИН раз во временную
`__trust_dst_<N>` (идиома мутации `item, dict... := ... dict` - pop'ы прямо в источник); каждая цель →
`pop_front()`, типизированная per-element runtime-типом (`int64_t c_ti = std::any_cast<int64_t>(...)`,
Bool/Double/StrChar аналогично; рантайм Dict хранит int как int64_t). ВНУТРИ цикла (`m_inLoop`) тип
расширен до МАКСИМАЛЬНОГО среди элементов (Bool/Int8 → Integer, float → Double) и присваивается через
runtime-конвертеры (`trust::detail::anyToInt64/anyToDouble/anyToString`), т.к. гетерогенность (bool+int)
ломает строгий `any_cast`; невыводимый тип → `std::any`. `_` - skip, `rest...` - «остаток»
(`trust::Dict c_rest = src`), `_...` - отброс.
**Кортеж** (без `...`) - `emitDestructureTuple`: `auto c_ti = std::get<idx>(c_source);`, `rest...` -
`std::make_tuple(std::get<k>(c_source)...)` остатка (`#include <tuple>`), арность проверена семантикой.
Истинность словаря в `@while(dict)` - через `trust::Dict::operator bool`.
**Присваивание** (`t1, ..., tN = [... ]source;`, `DestructureDecl::m_isAssign`) - цели НЕ объявляются:
кодген эмитит `c_ti = std::get<idx>(...)` (кортеж) / `c_ti = std::any_cast<T>(src.pop_front())` +
`rest = src` (словарь); объявление не создаётся (существование цели проверяет семантика). **Явная
аннотация типа цели** (`a:Type`, `m_targetTypeNodes`) фиксирует тип ДЕКЛАРИРУЕМОЙ переменной
(`m_targetDeclaredTypes`, для кортежа `int32_t c_a = std::get<0>(...)` вместо `auto`), а тип `any_cast`
берётся из `m_targetTypes` = natural runtime тип элемента (соответствует хранению Dict, int → int64_t).
Rest-цель эмитится фиксированно: `trust::Dict c_rest = src` (словарь) / `auto c_rest = make_tuple(...)`
(кортеж); аннотация типа на rest-цели отклоняется семантикой (Error), поэтому кодген не хранит/не
использует её. Переиспользование имени rest-цели (кроме мутации-идиомы spread-словаря) также
отклоняется семантикой - кодген получает только корректные (валидные C++) конфигурации rest.
**Маппинг на .cppt:** `DestructureDecl` - НЕ statement-выражение (не оборачивается `SemicolonStmt`),
поэтому `visit_DestructureDecl` открывает собственный `MapperScope` на весь оператор
(`DestructureDecl::range()` переопределён как [первая цель, конец источника], а не базовый `:=`),
и все эмитируемые строки (temp-источник + runtime-guard + pop_front'ы/std::get + rest) мапятся на
исходный оператор - иначе раскрытие словаря/кортежа не имело бы записи в source map.
**Диапазоны термов раскрытия:** в `syntax/parser.y.in` термы `dictionary` (`$$ = $1`, где `$1` -
`LPAREN`) и эллипсиса (`... <операнд>`) при конструировании расширяются до полного охвата
(`( ... )` / `... <операнд>`). Иначе `range()` литерала словаря заканчивался на `(` (маппинг/подсветка
`x := (4, 5,)` покрывал бы только имя `x`), а эллипсис - на `...` (операнд `e` в `d []= ... e`
выпадал из маппинга).

### Кортеж `:Tuple(...)` / `(...):Tuple`
Кортеж - структурный/компайлтайм-тип: семантика по типу из реестра ставит узлу `kind==Tuple`
(`analyzeDictLiteral`, `DictLiteralNode::setKind`), и такой узел всегда эмитится
в `std::tuple` (`#include <tuple>`) через `std::make_tuple(elem, ...)` (`visit_Tuple`).
Позиционные и именованные элементы: **имена конвертируются в индексы** (по порядку списка), сами
имена в C++-код не попадают. Тип переменной кортежа - структурный `Tuple`-тип (семантика создаёт его
через `TypeRegistry::getOrCreateTupleType`, элементы в `TupleTypeData`), в C++ резолвится в `auto`
(`resolveCppTypeId`), т.к. конкретный `std::tuple<T...>` выводится из инициализатора.
Доступ к элементам `t.name` / `t.0` / `t[idx]` → `std::get<N>(obj)` (`emitTupleElementAccess`);
индекс резолвит семантика (Binary::tupleIndex из `TupleTypeData`). Динамический индекс `t[expr]`
нерезолвим статически (std::get требует константу) - диагностика.

### Строка-формат `"{}"(args)` / `'{}'(args)`
Строковый литерал как callee `CallExpr` (каль = `Literal` StrWide/StrChar) → `visit_CallExpr` эмитит
`std::format(L"…", args...)` (StrWide) / `std::format("…", args...)` (StrChar) (`emitFormatCall`),
записывая `#include <format>`. Семантика типизирует результат как StrWide/StrChar и проверяет число
плейсхолдеров `{}` против числа аргументов (`checkFormatStringArgs`, `{{`/`}}` - литеральные скобки).

## Scope (Phase 1)
- Variable declarations with initialization → `auto x = 42;`
- Literals → C++ literal syntax
- Type aliases → `using MyInt = int32_t;`
- Control flow: `if`/`else-if`/`else` → `if (...) { ... } else if (...) { ... } else { ... }`; `while`/`while-else` → `while (...) { ... } else { ... }`; `do-while` → `do { ... } while (...);`
- Jump statements: `return expr;`, `throw expr;`, void `return;`
- `break`/`continue` → `goto <метка начала/выхода цикла>;`
- `match` → временная переменная + `if/else-if/else`
- Явный каст `:Type(expr)` → `trust::checked_cast<Type>(expr)` (рантайм-проверка диапазона;
  инклуды `trust/checked_cast.hpp` + `trust/assert.hpp`, извлекаются из trust-runtime)
- Бинарные операторы тип-зависимы: `std::any`-операнд → универсальный `trust::detail::anyToInt64(operand)`
  при приведении к целому (вместо строгого `std::any_cast<int32_t>` - тот ломается на гетерогенных
  значениях словаря bool/int8/...); для элемента словаря - `(...).getAs<Cpp>()` (fast-path variant)
  (типы операндов/общий тип берутся из `Binary::lhsType/rhsType/commonType`, заполненных семантикой)

## Единая раскладка control-flow в Term (parser.y)
`FOLLOW`/`WHILE`/`DOWHILE` используют одинаковые роли полей:
- `m_left`  - условие (`cond`) - всегда;
- `m_right` - `else` (для if/while; у do-while пуст) - "else известен сразу";
- `m_sequence` - тело в `[0]`; для `elseif` - branch-термы с `[1..]` (каждый `m_left`=cond, `m_right`=body).
Полный диапазон statement'а вычисляется в term_to_ast (`expandControlFlowRange`): от min-begin до max-end по детям
(для do-while это `[body.begin, cond.end]` - текстовый порядок «тело→условие» сохраняется в range, а не в полях).

## Интеррапты: break / continue / return / throw (parser.y `exit_part`)
Все операторы прерывания - `exit_part: interrupt | interrupt rval interrupt`, где
`interrupt: INT_PLUS | INT_MINUS | INT_REPEAT`. Роль определяется по TermID и наличию значения:
- `INT_PLUS` (`++`) без значения → **break**; со значением → **return**.
- `INT_REPEAT` (`-+` / `+-`) → **continue**.
- `INT_MINUS` (`--`) → **throw**.
- Void-return записывается явно `++ _ ++` (значение - служебный символ `_`, `m_value = nullptr`).
Имя блока (label) из `exit_prefix` живёт в `m_text` терма и попадает в `JumpStmt::m_label`.

**AST:** break/continue/return/throw - класс `JumpStmt` с разными `Kind`
(`BreakStmt`/`ContinueStmt`/`ReturnStmt`/`ThrowStmt`), `m_label` (опционально), `m_value` (для return/throw).

## Оператор match (parser.y `match`)
`[значение] ==> { [шаблоны] --> тело; ...; [...] --> else; }`.
Раскладка MATCHING-терма: `m_left`=значение, `m_right`=match_body (BLOCK),
`m_right->m_sequence` = [item1, item2, ..., elseItem]. Каждый item: `m_left`=шаблоны
(matches: первый шаблон - сам терм, остальные - в его `m_sequence`), `m_right`=тело;
else: `m_left` = ELLIPSIS.
**AST:** класс `MatchStmt{MatchingStmt}`: `m_value`, `m_cases` (список `MatchCase{patterns, body}`), `m_default`.

## Codegen: break/continue, return, match и форматирование
> **Разделение ответственности.** Транспилятор - ТОЛЬКО кодогенерация C++ по «низкоуровневому»
> AST. Всё, что требует анализа структуры (имя текущей функции/стек имён, метки goto, точка
> с запятой для конца выражения), выполняется СЕМАНТИЧЕСКИМ АНАЛИЗАТОРОМ на проходе lowering
> и зашивается в AST в виде синтетических узлов `GotoStmt`/`LabelStmt`/`SemicolonStmt` (см. semantic/MEMORY.md).
> Транспилятор лишь эмитит их содержимое.
- **break/continue.** После lowering сюда доходят только БЕЗЫМЯННЫЕ прерывания → обычные C++
  `break;`/`continue;`. Именованные break/continue анализатор переписывает в `GotoStmt`
  (`goto <имя>_break/_continue`) или в void-`ReturnStmt` (break по имени функции = `return;`).
- **Метки именованных блоков.** Вставляются анализатором как `LabelStmt` (см. semantic/MEMORY.md):
  `<имя>_break:;` - после тела блока, `<имя>_continue:;` - первый цикл в теле (перед while /
  в конце тела do-while). Транспилятор эмитит `LabelStmt` как `name:;` на своей строке без
  source-map (у синтетических узлов нет исходного trust-текста, их range невалиден).
- **return/throw.** `++ значение ++` = `return значение;`; void-return `++ _ ++` = `return;`;
  `throw <expr>;`. `func:: ++` (break по имени функции) уже превращён анализатором в void-`ReturnStmt`.
- **match.** Значение вычисляется во временную переменную `auto _match<N> = <value>;` (на отдельной
  строке), затем ветки - через `if (_matchN == p1 || _matchN == p2) { body } ... else { default }`.
- **Точка с запятой (statement-выражения).** Анализатор оборачивает statement-позиции выражений
  (бинарные kinds, литералы, `CallExpr`) в `SemicolonStmt`; транспилятор в `visit_SemicolonStmt` эмитит
  ребёнка в statement-root (без внешних скобок) и добавляет `;` + source-map. Никакой
  самодеятельности по `;` в visit_<expression-kind> нет.
- **Нормальное форматирование.** Тела блоков (функций, if/while/do-while/match) генерируются
  многострочно с отступами (4 пробела на уровень): `{` в конце строки, каждый оператор
  на новой строке с отступом, `}` на своём уровне. Ветки последовательностей (ScopeBlock/sequence)
  внутри блоков также форматируются с отступом. На верхнем уровне (namespace-scope, indent==0)
  сохраняется зеркалирование строк исходника.
- **Стек контекстов вложенности (только отступ).** Используется единый стек `m_scopeStack`
  (элементы `ScopeContext{indent}`). При входе в тело функции/блока пушится контекст с отступом +1.
  `indentLevel()`/`indentPrefix()` берут отступ из вершины. Имя функции транспилятору не нужно
  (метки/goto уже решены анализатором). Единственный флаг контекста - `m_inCppBlock` (см. ниже).

### Генерация блоков и областей видимости

`{ ... }` в Trust - это последовательность операторов, оформленная как одно единое выражение.
`visit_ScopeBlock` различает вид блока по `text()`/`is_hidden()`/`is_anonymous()` и эмитит
C++-скоуп, сохраняя область видимости (объявления не «протекают» наружу):

| Trust | text() | Контекст | C++ |
|---|---|---|---|
| безымянный блок кода `{ ... }` | `""`/`"{"` | только внутри функции/класса | `{ ... }` |
| именованная метка `label { ... }` (без `::`) | `label` | только внутри функции | `{ ... }` + метки `label_break:`/`label_continue:` |
| область имён `ns:: { ... }` | `ns::` | верхний уровень модуля (вложенность ок) | `namespace ns { ... }` |
| глобальная область `:: { ... }` | `::` | верхний уровень | содержимое без обёртки |
| анонимная область имён `_ { ... }` | `_` | верхний уровень модуля | `namespace { ... }` |

Валидность контекста проверяется в `visit_ScopeBlock`: безымянный блок/метка разрешены только
внутри функции/класса (`m_inCppBlock == true`), иначе - диагностика; области имён (`ns::`, `_`)
разрешены только на верхнем уровне модуля (`m_inCppBlock == false`), иначе - диагностика.
Глобальная область `::` - содержимое эмитится без namespace-обёртки.

**Экспорт.** Из модуля экспортируются имена переменных и функций на верхнем уровне в НЕ
анонимной области имён: глобальная `::` и именованные `ns::` (квалифицированно, `qualifiedCppName`
через стек `m_namespaceStack` → `ns::x`); из `_` (`m_hiddenNamespaceDepth > 0`) и из локальных
(внутри функций/блоков кода, `m_inCppBlock`) - не экспортируются.

`m_inCppBlock` устанавливается в `emitBlockBodyToFile` на время тела (save/restore). Сам range
блока не маппится `MapperScope` (терм блока несёт range только открывающей `{` - дегенеративен,
иначе коллизия ключа в `mapStop` с дочерним узлом); имена внутри маппятся собственными скоупами.

**Тело модуля.** Контейнер тела модуля (SEQUENCE-терм) не оборачивается в ScopeBlock:
`ModuleNode`/`termToAst` разворачивают его в фактические top-level операторы (`convertModuleBody`/
`convertSeq`/`flattenInto` рекурсивно разворачивают SEQUENCE-термы - синтаксические контейнеры,
не пользовательские скоупы). Поэтому `visit_ScopeBlock` вызывается только для пользовательских
блоков.

**Тело функции.** `func_node.m_body` уже плоский (SEQUENCE развёрнут), поэтому
`generateFuncDeclToFile` эмитит его напрямую через `emitBlockBodyToFile`; вложенные блоки внутри
тела оборачиваются `visit_ScopeBlock` как `{ }`.

**Анонимные блоки.** Парсер хранит `text() = "{"` для безымянного блока (терм LBRACE), поэтому
`ScopeBlock::is_anonymous()` учитывает `text() == "{"`, чтобы lowering не считал безымянный блок
именованным и не вставлял метки `_break`/`_continue`.




## Pipeline Position

`Parser → SemanticAnalyzer → [on success] → CppTranspiler`.

## Components

### CppTranspiler
Walks the AST and produces C++ code directly to a file (no string-based generation).

**Constructor:** `explicit CppTranspiler(Context& ctx)` - единственный. Работает с реестром типов через `ctx.types()` (а также `ctx.source()`, `ctx.diag()`). Таблица символов (`SymbolTable`) живёт в `SemanticAnalyzer` и доступна через `analyzer.symbols()`; у `Context` метода `symbols()` НЕТ.

**Методы:**
- `generateToFile(ast_nodes, output_idx)` - единственный метод генерации. Записывает C++ код в выходной файл с построением source map через `mapStart/mapStop`. Для каждого узла AST создаётся RangeMap input (trust) → output (cpp).
- `exports()` - возвращает список `ExportEntry{trustName, cppName}`, собранных в процессе генерации (например для `type Byte = Int8`). Экспортируются только **определения**: функция с телом (`m_body`) и переменная с инициализатором (`m_initializer`); forward-объявления в exports не попадают (для них `&::<name>` не связался бы).

**Приватные методы:**
- `generateVarDeclToFile()` - генерация объявления переменной (`:=`). Для forward-объявления
  (`<name>:Type := ...;`, `m_initializer == nullptr`) эмитится `extern <cpp_type> <name>;` (C++-декларация
  без определения); нетипизированная ненативная `x := ...;` → `extern std::any x;`. Смещение имени в
  source-map учитывает префикс `extern `.
- Манглинг - прямой вызов `utils::name_to_cpp` (срез ведущего `%` у нативных имён/функций
  `%add → add`, обычные trust-имена → `c_`-префикс). Используется в `generateFuncDeclToFile`,
  `generateVarDeclToFile`, `visit_Ident`, именах областей имён и пользовательских алиасах.
- `generateTypeDeclToFile()` - генерация объявления типа (`::=`)
- `emitExpr()` - потоковая генерация выражения прямо в `m_ctx.source()` (без возврата `std::string`); null-узел → `"{}"`
- `emitBinaryStmtOrExpr()` / `emitBinaryOpRaw()` - бинарные kinds: statement-root (без скобок, ';' добавляет SemicolonStmt) или выражение (в скобках)
- `generateIfToFile()` / `generateWhileToFile()` / `generateDoWhileToFile()` - генерация control-flow
- `emitBlockBodyToFile()` / `emitBodyNode()` - генерация тела `{ ... }` с зеркалированием строк
- `emitTypeName(typeId)` / `emitTypeNameForNode(typeNode)` - C++-имя типа + запись всех его инклудов (единая точка сбора типов; нерезолв = ошибка с диагностикой)

**Маппинг control-flow:** каждый узел if/while/do-while маппится через `mapStart(node.range())/mapStop(node.range())`
(весь statement → сгенерированный C++). Тело каждой ветки маппится отдельно через диапазон блока `{ ... }`.
Исключение - do-while: begin statement'а совпадает с begin тела (начинается с `{`), что дало бы коллизию ключа
в `mapStop` (ключ = begin диапазона). Поэтому тело do-while не оборачивается собственным `mapStart/mapStop`
(`emitBodyNode(..., mapBlock=false)`) - всё покрывает единый range statement'а.

**Маппинг:** Для каждого узла AST создаётся RangeMap input (trust) → output (cpp) через стек mapStart/mapStop в Context. Имена объявлений (переменных, функций, типов) и параметров функций дополнительно регистрируются через `addNameMapping` для hover-ссылок (trust → cpp).

**VarDecl и `:=`:** терм оператора `:=` (CREATE_NAME) несёт range только оператора; в `term_to_ast` его `m_mapperRange` расширяется до `[left.begin, right.end]` (вся строка, если name и initializer в одном файле), чтобы `mapStart` покрывал весь statement. Для точного name-маппинга `VarDecl::nameRange()` возвращает диапазон реального имени из базового `m_term->m_left` (не дублирующее поле) - он используется в `generateVarDeclToFile` вместо вывода из `range().begin` (важно при макро-раскрытии, где range() может указывать на оператор).

**Бинарные statement-операторы (`+=`, `-=`, `=`, `//=` и т.п.) и `:=`:** терм оператора (напр. `+=` из `logical operator arithmetic`) несёт range только оператора. Общий хелпер `expandTermRangeToChildren` (в `term_to_ast`) расширяет `m_mapperRange` до `[left.begin, right.end]` для всех составных (бинарных) узлов - вызывается и в ветке `CREATE_NAME` (`:=`), и в generic-пути `TermToAstConverter` для бинарных kinds (через `if constexpr std::is_same_v<node_type, Binary>`). Guard одинаков: только когда left/right валидны, в одном файле и begin <= end. Это нужно, чтобы `emitBinaryStmtOrExpr`/`mapStart` покрывал весь statement, а trust-lsp подсвечивал/наводил на всю строку, а не только на оператор.
**FuncDecl (`::=` + NATIVE-lval):** сигнатура и тело маппятся раздельно. Диапазон statement'а функции - `[m_left.begin, оператор.end]` (имя + оператор, БЕЗ тела): в ветке `CREATE_TYPE && m_left->NATIVE` (`term_to_ast`) `m_mapperRange` сужается до левой границы из `m_left` и конца оператора (в отличие от `expandTermRangeToChildren`, который охватил бы и тело). В `generateFuncDeclToFile` этим диапазоном маппится сигнатура (`void func(...)`), а тело (`{ ... }`) - отдельным `mapStart/mapStop` из `FuncDecl::blockRange()` (`m_term->m_right`), чтобы скобки отображались в C++. Имя функции регистрируется name-маппингом из `range().begin` (теперь указывает на начало имени `%func`).

**Точка входа (`@main` → `<модуль>__main__`):** entry-функция (trust-имя заканчивается на `__main__`,
верхний уровень) эмитится с СЫРЫМ именем (без манглинга `c_`), типом возврата `int` (если явный
тип не задан) и `return 0;` в конце тела, если оно не заканчивается явным `return`. Так она
совпадает с `extern int <модуль>__main__()` в сгенерированном pipeline-файле `_main.cppt`
(иначе манглинг `c_…`/тип `void` не слинковались бы с точкой входа).

**Рациональный литерал `num\den` (RationalLiteral):** отдельная лексема (`TermID::RATIONAL` →
`ParserToken::Kind::RationalLiteral`, не IntLiteral). Транслируется в `trust::Rational("num\den")`
(однострочный конструктор Rational парсит `num\den` внутри; в C++-строковом литерале обратная
косая экранируется `\` → `\\`); заголовок `trust/rational.hpp` подключается **по типу**
(механизм №1): `emitTypeName` отмечает тип Rational в `m_usedTypes`, инклуд формируется после обхода AST.

**Строковые литералы (StrChar/StrWide):** текст литерала хранится лексером как есть (escape-
последовательности не декодируются, см. `lexer.l`). Экранирование требуется **только для
StrChar** (`'…'` → `"…"`, и узкий StrChar-литерал в wide-контейнер → `L"…"`): ограничитель
StrChar - `'`, поэтому голый `"` в нём допустим и обязан стать `\"` в C++ (иначе литерал
обрывается). Логика встроена инлайн в `visit_StrChar` (и в `.append`-ветку wide-контейнера):
существующие `\X`-последовательности копируются как есть (backslash + следующий символ), голый
`"` → `\"`. **StrWide** (`"…"` → `L"…"`) экранирования не требует: голый `"` в нём невозможен
(закрыл бы строку), кавычка вставляется как `\"` и уже валидна в C++.

### Унифицированные хелперы генерации (снижение дублирования)

Транспилятор не обращается к `m_term` напрямую - диапазоны/имена читаются только через методы
узлов (`range()`, `nameRange()`, `blockRange()`, `text()`). Это контракт Term-изоляции: изменения
раскладки `Term`/`term_to_ast` не должны влиять на codegen, кроме методов самих узлов AST.

Общие операции вынесены в единые хелперы (используются несколькими `generate*ToFile`):
- **Диспетчеризация по типу узла** - `CppTranspiler : KindVisitor` (`ast/kind_visitor.hpp`).
  `generateNodeToFile` → `dispatchKind(node, *this)`, который вызывает `visit_<Kind>` - члены класса
  (statement-контекст). Новый kind в `PARSER_TOKEN_KINDS`, не обработанный здесь, не даст
  скомпилироваться (абстрактный `KindVisitor`). Обход тела блоков - `emitSequenceBody`;
  намеренно не генерируемый kind - явный no-op `{}`.
- **Потоковый вывод выражений** - `emitExpr(node)` пишет C++-текст выражения прямо в `m_ctx.source()`
  (в `m_out`), без возврата `std::string`. Единая диспетчеризация ПО KIND: `emitExpr` - это
  `dispatchKind(*node, *this)` с инкрементом глубины `m_exprDepth` (никакого отдельного switch
  выражений). `visit_<Kind>` - ЕДИНСТВЕННАЯ генерация для kind: на верхнем уровне
  (`m_exprDepth==0`, statement) добавляет mapStart/mapStop и `;`; во вложенном выражении
  (`m_exprDepth>0`) - только текст. Для бинарных kinds общий `emitBinaryStmtOrExpr` + `emitBinaryOpRaw`
  (сырой текст с обработкой `//`/`//=`), различающие контекст по `m_exprDepth`. Классификация операторов
  (целочисленное деление `//`/`//=`, простое/составное присваивание) - из общего
  `utils/operators.hpp` (`isIntDivOp`/`isCompoundAssignOp`/`isPlainAssignOp`), того же, что использует семантика.
- `emitTypeNameForNode(type_node)` - единая точка эмиссии C++-имени типа из узла-типа (TypeName):
  вызывает `resolveCppType` (единая последовательность
  `findType → getCanonicalTypeId → getCppTypeName → getPreprocInclude`), **отмечает тип как
  использованный** (по-типу, механизм №1) и возвращает имя. Нерезолвящееся имя - ВСЕГДА ошибка
  с обязательной диагностикой (`m_ctx.report(...)`) и возврат "". Fallback запрещён.
  None/Void обрабатываются явно на сайтах return/каста (это не fallback, а явное отображение на
  C++ `void`); переменная типа Void - ошибка. Используется в `generateVarDeclToFile`,
  `generateFuncDeclToFile` (return/params), `emitTypedConstruction`.
- `emitTypeName(type_id, displayName)` - то же по известному `TypeId` (через `resolveCppTypeId`).

**Подключение инклудов - два независимых механизма:**
- **Механизм №1 - по типу (TypeRegistry).** `resolveCppTypeId` вызывает `recordUsedType(canonical)`,
  который кладёт использованный КАНОНИЧЕСКИЙ `TypeId` в `m_usedTypes` (ТОЛЬКО типы, не файлы!).
  Сами директивы инклудов из собранных типов формируются **ПОСЛЕ полного обхода AST**:
  `collectTypeIncludes()` проходит `m_usedTypes` и для каждого типа записывает весь
  `getPreprocIncludes` (m_runtimeHeaders/m_requiredIncludes). Вызывается в конце `generateToFile`
  перед `emitCollectedIncludes`. Так любой тип, эмитированный через `emitTypeName`/`emitTypeNameForNode`,
  автоматически попадает в наборы заголовков - но файлы выводятся из собранных типов только после обхода.
- **Механизм №2 - по рантайм-символу (RuntimeSymbol).** Используется там, где типа нет: EMBED-узлы
  (`{% %}`) содержат только текст, поэтому поиск по тексту/имени - единственный способ определить
  нужные заголовки. Единый источник имён/заголовков символов - компайлтайм-таблица
  `types/runtime_symbols.hpp` (`RuntimeSymbolId` + `runtimeSymbolName`/`runtimeSymbolHeaders`).
  ЕДИНСТВЕННЫЙ способ записи заголовков - `recordRuntimeSymbolHeaders(RuntimeSymbolId)` (строковой
  перегрузки нет): `emitTypedConstruction` (`trust::checked_cast`/`trust::any_to`) передаёт id напрямую
  (`RuntimeSymbolId::kAnyTo`/`kCheckedCast`); `visit_CallExpr` - через `findRuntimeSymbolByName(callee)`;
  `visit_EmbedExpr` - через хелпер `recordRuntimeSymbolsInText(text)` (substring-скан текста вставки),
  внутри вызывающий id-метод. Список заголовков символа - полное транзитивное замыкание (для
  co-извлечения). Рантайм-символы - только не-типовые функции; типы Dict/Rational заголовки получают
  по-типу (механизм №1), как символы они не регистрируются. `std::any_cast` (`<any>`) подключается
  по-типу: `recordUsedType` на типе операнда Any.
- `mapDeclaredName(output_idx, trustRange, prefixLen, name, cppName)` - единый расчёт
  cpp-оффсета имени для `addNameMapping` (hover): `mapStackTop().outputBegin.offset() + prefixLen`.
  Заменяет три копии арифметики оффсета (var/type/func + параметры). ВАЖНО: начало вывода не
  фиксировано в offset 1 - `output_prepend` (инклуды) сдвигает `outputBegin`, поэтому оффсет
  считается строго от `outputBegin.offset()`.

**Embed-блоки (`{% ... %}`):** range EMBED-токена покрывает только содержимое между разделителями (не `{%`/`%}`). Конкатенация EMBED-блоков в парсере убрана: каждый `{% ... %}` - отдельный узел (`EmbedExpr`) со своим range (правило `sequence: sequence EMBED` позволяет соседним блокам без `;` быть отдельными `expression`).

**Переводы строк между блоками (все блоки, не только EMBED):** перенос строки между последовательными узлами в C++ управляется `emitBlockSeparator(prev, node, output_idx)`, вызываемым в циклах обхода (top-level `generateToFile`, `sequence`/`ScopeBlock`/`ModuleNode`). `'\n'` вставляется между блоками ТОЛЬКО если строка конца `prev` != строки начала `node` (`source().line()`). Если блоки на одной строке исходника - перевод строки не выводится, а между ними ставится пробел для читаемости (`emitSameLineSpace`), но только если на границе нет пробельного символа (не дублирует пробелы из EMBED-содержимого). Хвостовых `\n` в отдельных функциях генерации нет - перевод строки расставляется на границах блоков, поэтому внутреннее форматирование сохраняется корректным.

**Зеркалирование раскладки исходника внутри функции:** тело функции (блок `{ ... }`) также повторяет строки исходника - `'{'`/`'}'` и операторы размещаются по строкам исходника. Диапазон блока берётся из `FuncDecl::blockRange()` (`m_term->m_right`); для непустого тела строки `{`/`}` сравниваются с первым/последним оператором (`emitBlockSeparator`-логика). Для пустого тела диапазон блока покрывает только `{` (не `}`), поэтому `{` и `}` всегда разделяются переводом строки (`{\n}`).

### Source Map
Source map генерируется и встраивается в ELF-секцию `.debug_trust_map` на этапе компиляции C++.
Содержит:
- forward mapping: trust → cpp
- backward mapping: cpp → trust
- name mappings
- хеши файлов для верификации

## Changes (June 2026)

### Документирующие комментарии (Document)
Доки - отдельные leaf-узлы AST (`kind=Document`, класс `AstNodeAttr`), элементы `m_body`
в исходном порядке. `CppTranspiler::visit_Document` эмитит текст в statement-позиции;
перенос/отступ расставляют циклы обхода. Trust-доки `##`/`##<` невалидны в C++ (префикс `#` -
препроцессор), поэтому при выводе нормализуются в однострочные C++ `///`/`///<`; `/** … */` и
`///` выводятся как есть. Подавление - опция `-Wno-comments` (булев feature-флаг
`FlagKind::Comments` из `OPTIONS_FLAGS`): `isSuppressedDoc(kind)` в `generateToFile`/
`emitSequenceBody`/`emitBlockBodyToFile` пропускает `Document`-узлы. AST всегда хранит доки;
подавление - только кодогенерация. Чтобы подавленные доки/пустые doc-bundle'ы не оставляли
пустых строк, перенос добавляется ПОСЛЕ каждого реально выведенного узла (и не дублируется,
если вывод уже заканчивается `\n`).

- CppTranspiler принимает Context& вместо SymbolTable& (SymbolTable остаётся в SemanticAnalyzer, доступна через `analyzer.symbols()`; `ctx.symbols()` не существует)

## Enum-типы (visit_EnumDecl)

`visit_EnumDecl` эмитит самодостаточную `struct c_Color` с `using Value = <тип значений>;`,
полями `value`/`ordinal`, `constexpr`-конструкторами, операторами `<,>,<=,>=,==,!=` (по ординалу)
и `count()`. Члены - `static const` + out-of-class `const c_Color c_Color::c_MEMBER{...};`
(`static constexpr` собственного типа невозможен - неполный тип в точке объявления; удовлетворяет
требованию «const»). `Color.RED` → `c_Color::RED`; тип-уровневые методы → `c_Color::count()`.
`resolveCppTypeId` для enum возвращает `name_to_cpp(имени)` (самодостаточная структура).

- Генерация - только через `generateToFile()`
- Добавлен потоковый `emitExpr()` для правой части выражений (пишет текст прямо в `m_ctx.source()`, без возврата строки)
- Source map встраивается в ELF (`ModuleApi::packToMsgpack()`), внешние `.trust` файлы не используются
- `ExportEntry` - структура для экспортированных символов (trustName → cppName)
- `emitBinaryStmtOrExpr()`/`emitBinaryOpRaw()` - для statement-level выражений (присваивание, ++, --)
- `generateTypeDeclToFile()` - для объявлений типов (`::=`)

### Квалификаторы из атрибутов (const/thread_local/функции)
Генератор C++ эмитит C++-квалификаторы по атрибутам узлов (`has_attr(m_ctx.attrs(), attr::X)`):
- **VarDecl:** `attr::ThreadLocal` → лидирующий `thread_local`. Лидирующий `const` объявления
  даёт `attr::ReadOnly` на узле переменной (`^` на имени или `@[readonly]@`) - это const «в типе»
  объявления. Источник const объявления НЕ берётся из бита `Symbol::type`: переменная может стать
  константной позже (became-const, `x := 42; x^ += 1;`), но её ДЕКЛАРАЦИЯ обязана остаться не-const
  (переменная мутировалась до финализации). Для типизированных, `std::any` и forward
  `extern const ...`; префикс учитывается в `namePrefixLen`/source-map. `x^ := 42` →
  `const int8_t c_x = 42;`; `x := 42; x^ += 1;` → `int32_t c_x = 42; c_x += 1;`.
- **Нетипизированная переменная:** тип берётся из `VarDecl::inferredType`, который семантика ставит
  **всегда** - конкретный тип либо **явно `std::any`** (для тип-less инициализаторов: C++-вставка
  `{% %}`, вызов с неизвестным результатом, отрицательный литерал - и для нетипизированного
  forward-объявления `x := ...;`). Голый тип-имя в правой части `:=` (`x := :Int32`) - **невалиден**:
  семантика выдаёт ошибку (в `:=` справа значение; тип объявляется через `::=`). Any - обычный
  выводимый тип, эмитится единообразно `emitTypeName(inferred)` (без отдельной ветки «угадывания»).
  `INVALID` - диагностика ошибки «unable to infer type...»; тихий fallback на `std::any` запрещён
  (AGENTS rule 5).
- **FuncDecl:** лидирующие `attr::FuncConst` → `__attribute__((const))`, `attr::FuncPure` → `__attribute__((pure))`,
  `attr::FuncConstexpr` → `constexpr`; завершающий `attr::NoExcept` → `noexcept`. Лидирующий префикс
  учитывается в оффсетах сигнатуры и имён параметров. `ReadOnly` у функций не обрабатывается.

Атрибуты объявления (`@[...]` и `^` на имени) в `term_to_ast.cpp` берутся из терма-имени (`term->m_left`
оператора `:=`) и проставляются на узел `VarDecl`/`FuncDecl` (`convertAttrsToNode`).

### Импорт модулей: forward-decl вместо тела

`visit_ModuleDecl` для **сайта импорта** (`ModuleNode::isImport() == true`) вызывает
`emitModuleImportDecls()` вместо `emitSequenceBody()`: на месте оператора `\module(mod, masks)`
эмитятся только forward-объявления экспортов (`m_exports` - отфильтрованный интерфейс):
- переменная → `extern <type> <name>;`;
- функция → прототип `<ret> <name>(<params>);`;
- тип → алиас `using <name> = <base>;`.

Режим `m_forwardDeclOnly` подавляет определение (инициализатор/тело) в `generateVarDeclToFile`/
`generateFuncDeclToFile`. **Source-map для forward-decl подавляется** (`SourceMapWriter::suppressMapping`):
синтетическое объявление на сайте импорта не должно заявлять исходный диапазон модуля - этот диапазон
маппится в собственном `.cppt` модуля (иначе коллизия trustKey «уже замаплено»). Сопоставление
экспортов - по указателям термов (`m_exports` содержат те же `TermPtr`, что и узел-декларация в
`m_body`).


## Facts and invariants

- **Массив (Array<Elem>)** → C++-имя: `std::vector<ElemCpp>` (mutable) / `std::array<ElemCpp,N>`
  (константная форма, `:Array^(...)`), строится в `resolveCppTypeId` (записывает `<vector>`/`<array>`);
  константность - kConstFlag-бит TypeId (`typeIsConst`), как у любых типов.
  Литерал `[1,2,3,]`/`[...]:Type` → `visit_ArrayInit`/`emitArrayLiteral` (std::vector<Elem>{...});
  `:Array(...)` → `emitTypedConstruction` (по `DictLiteralNode::arrayType`); `:Array^` → const
  (attr::ReadOnly на контейнере → withConst → std::array). Доступ `a[i]` → `(obj).at(i)`.
  Определения типов `:Elem[N]`/`:Elem[N,M]` распознаются `emitTypeNameForNode` (Array<Elem,dims>).
  Многомерные (dims>1 или вложенный элемент-массив) - кодогенерация «не реализовано»
  (`isMultiDimArray`). Ограничение: параметр-массив с телом-возвратом `x[0]` - баг парсера
  (`:= expr` + постфиксный индекс в теле функции), см. .tasklog.
- **Значения членов Enum/Variant** (`emitEnumStruct`/`emitVariantStruct`) - только СКАЛЯРНЫЕ
  литералы. Составные (массив/словарь/диапазон) НЕ реализованы: диагностика «не реализовано»
  выдаётся В САМОЙ генерации значения - по факту невозможности сформировать C++-литерал
  (`valNode && !is_literal_kind(valNode->kind())`), а не отдельной проверкой типа значения заранее
  (см. `emitEnumStruct`/`emitVariantStruct`). `memberValueCpp` форматирует только скалярные литералы;
  иначе было бы тихий сброс значения (`int8_t c_a{}`) или невалидный C++ (`c_a{,0}`). Анализ/регистрация

- **Enum/Variant-типы: source-map (отображение на cpp).** `generateTypeDeclToFile` (ветки Enum/Variant)
  создаёт `MapperScope` на весь `TypeDecl` (range-маппинг trust→cpp). name-маппинг имени типа и
  членов выполняется ВНУТРИ `emitEnumStruct`/`emitVariantStruct`: оффсет имени вычисляется из
  фактического вывода (`out.size()` после `"struct "` / после типа в `static const ...`), а НЕ
  магической константой. Член: `static const c_X c_m;` → источник `m`.
  Для членов нужен ВАЛИДНЫЙ source-range: именованные ARGUMENT-члены строятся ЧЕРЕЗ ТЕРМ в
  `appendDictElementsFromArgs` (term_to_ast.cpp) - иначе `el->range()` невалиден и маппинг пропускается
  (голые/безымянные значения - без терма, маппинг не добавляется).


> ⚠ trap (addNameMapping): cpp-оффсет имени нельзя считать от 1 - `output_prepend` (инклуды) сдвигает `mapStackTop().outputBegin`; оффсет = `outputBegin.offset() + длина префикса` сгенерированной строки.

- **addNameMapping (cpp-оффсет имени):** нельзя предполагать начало вывода в offset 1 - `output_prepend` (инклуды #include для std::any/int32_t и др.) сдвигает `mapStackTop().outputBegin`. Оффсет считается только от `outputBegin.offset() + длина префикса` сгенерированной строки.
- **addNameMapping** вызывается для ВСЕХ объявляемых имён: переменных (`generateVarDeclToFile`), типов (`generateTypeDeclToFile`), функций и параметров (`generateFuncDeclToFile`). `trustRange` для имени функции = `[range().begin, begin+text().length()]`.
- **Именованные блоки-метки** (`label { ... }`) для break/continue эмулируются через РУЧНЫЕ goto-метки (`label_break:`/`label_continue:` + goto) в lowering, а НЕ через нативный C++26 `break label;`/`continue label;` (P2552). Причина: используемый компилятор clang++-22 фичу не поддерживает. Если компилятор обновится до поддержки P2552 - можно перейти на нативный синтаксис.

