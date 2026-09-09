# MEMORY.md

> scope: include/ast
> role: persistent-memory
> last_reviewed: 2026-09-18
> review_period: 30
> max_size: 14900

## Architecture

Поток: Lexer → Sequence (`SyntaxToken = variant<Lexeme, AstNodePtr>`) → MMProcessor → Sequence →
ParserAST. `AstNodeBase` хранит ТОЛЬКО `m_kind` и `TermPtr m_term`; `text()` читается из `m_term`,
`range()` для простых узлов — из `m_term`, для составных — вычисляется по детям. Две НЕ смешиваемые
группы конструкторов: терм-`X(Kind, TermPtr, Context*)` (при ctx!=null сам рекурсивно строит детей)
и manual-без-TermPtr (тесты).

## Facts and invariants

- **⚠ узел без `m_term` (manual/test-only):** `range()` — невалидный `{}` (без EXPECT), `text()` —
  EXPECT (родители считают охват по `child->range()`). `TermToAstConverter` НЕ мутирует исходный Term.
  Manual-конструкторы без Term — ТОЛЬКО для осознанного синтетического/тестового создания (lowering,
  временные анализатора, unit-тесты). Синтетические узлы (LabelRef/SemicolonStmt/LastResultCapture/
  ErrorExpr/hoist-временные) — легальны и архитектурно необходимы (их создаёт анализ, не транспилятор).
- **⚠ Наборы типов (`:A + :B`) — term→AST SUGAR:** `Name ::= :A + :B;`/inline `:(:A + :B)` → N
  `FuncDecl` общим телом (`type_set_expand.cpp`; ≤1 различный набор/прототип; `m_noSourceMap` — маппинг 2..N).
- **⚠ ref-вид: единая точка разбора — `ast/ref_syntax.hpp`:** `refKindFromAttrArgs` (аргументы
  `@[reftype(...)]`), `refKindOfAttr`, `refKindOfTypeNode` (маркеры `&&`/`&*`/`&?`) и
  `refKindOfTypeSpec` (вид ТИПОВОЙ стороны объявления = сигл ИЛИ `@[reftype]`-атрибут на узле типа).
  НЕ дублировать `refTypeFromString`/`refTypeFromTypeSigil` по месту (строки/маркеры — в
  `types/typekind.hpp`, X-macro). Исключение — syntax-уровень: `term_to_ast` превращает маркер перед
  именем в `@[reftype(...)]` (вход — Term, не AST).
- **`Binary::m_op` (enum `BinaryOp`) — конкретный оператор:** `kind` задаёт класс (MathOp/AssignOp/
  CompareOp), `m_op` — сам. Заполняется ОДИН раз в `Binary(TermPtr)` из текста (`parseBinaryOp`);
  потребители различают операцию по `m_op`, НЕ по `text()` (`text()` — только диагностика/рендер).
  Предикаты `isSwapOp`/`isIntDivOp`/`isPlainAssignOp`/`isCompoundAssignOp` — над `BinaryOp`;
  `utils/operators.hpp` удалён.
- **⚠ trap (kind↔класс НЕ 1:1):** РАЗНЫЕ kinds — ОДИНАКОВЫЙ класс при той же структуре данных;
  отношение задаётся ВТОРЫМ полем `PARSER_TOKEN_KINDS`. ЗАПРЕЩЕНО создавать новый класс под новый kind.
  Машинная проверка: `AST_NODE_CLASS_LIST` + `static_assert` — каждый kind обязан маппиться на класс из
  канонического списка (новый класс под новый kind → ошибка компиляции), мёртвых классов нет.
- **Типизированный доступ к AST без RTTI (R1):** `is<T>()`/`as<T>()` — на основе kind→класс через
  `std::is_base_of_v` (работает и для БАЗОВЫХ классов: `is<IdentName>()` верно для `IdentType`/
  `VarDecl`/`FuncDecl`). `as<T>()` при неверном kind — `FAULT` (не тихий nullptr).
  `dynamic_cast`/`static_pointer_cast` по AST в semantic/transpiler ЗАПРЕЩЕНЫ. Trust-контракты
  объявления — `trustContracts()`/`hasTrustProperty()` (EXPECT/FAULT при инородном/пустом элементе
  `m_trust` — молчаливого отбрасывания НЕТ).
- **`visit`:** полный набор `visit_<Kind>` у потребителей — осознанная compile-time проверка полноты,
  НЕ дублирование; no-op в default-базу выносить нельзя.
- **`ArgNode` — ЕДИНЫЙ узел позиции списка аргументов** (параметр/элемент коллекции/аргумент вызова);
  три формы raw/AssignOp/ArgNode устранены.
- **⚠ Семейство многоточия в списках — kind `Filling` (`... expr ...`) и `Ellipsis` (`...`/`... src`)
  на классе `Sequence` (новых классов НЕТ):** операнд (если есть) — единственный/последний ребёнок
  `m_body`. Число позиций задаёт контекст; список МАТЕРИАЛИЗУЕТ семантика → в AST у кодогена готовый
  список, отдельных полей для capacity/fillCount НЕТ. ⚠ `Sequence::lower` для `Filling`/`Ellipsis`
  понижает `m_body` как ОПЕРАНДЫ — иначе операнд-выражение оборачивалось бы в `SemicolonStmt` и давало
  лишнюю `;`.
- **`CallExpr::resultType`** — разрешённый СЕМАНТИКОЙ тип результата конструктора record-шаблона
  (`Box(...)` → инстанциация) по типу-цели контекста: кодоген эмитит `c_Box<int32_t>(...)` (в т.ч.
  пустой список), не полагаясь на CTAD.
- **`CheckAreaStmt` из маркера `@__CHECK_AREA__`:** `term_to_ast` перехватывает терм `MACRO_CONTEXT` с
  этим текстом и строит лист (не через generic dispatch; НЕ ContextMacro). Аргументы (дочерние
  NAME-термы) → `area`/`behavior`; имена атрибутов-требований резолвятся через `ctx.attrs().lookup` и
  добавляются `add_attr(id, /*manual=*/false)` — обрабатываются семантикой, а не «unhandled».
- **`DebugStmt` из маркеров `@__DEBUG__`/`@__DEBUG_SCOPE__`:** `term_to_ast` перехватывает `MACRO_CONTEXT`
  (как `CheckAreaStmt`) и строит лист с `mode` (`Filter`/`Scope`) и `args`. Семантика применяет эффект и
  УДАЛЯЕТ узел; транспилятор — `FAULT` (маркер не должен дожить).
- **MODULE-конвертация — только рекурсивная конвертация данных** (`m_body`); реальную загрузку файла
  делает `ctx.loader()` ЦЕЛИКОМ в pipeline. SEQUENCE-термы рекурсивно разворачиваются в top-level,
  BLOCK сохраняется как `ScopeBlock`.
- **Trust-контракты:** `@{ kind --> <logical> @}` (kind всегда явный); маркеры с ВЕДУЩИМ `@` (`}@`
  конфликтует с `@@`). `collectChildren()` пуст — обходят `m_expr` явно.
- **⚠ trap:** trust-контракт-термы после имени кладутся в `m_sequence` терма-имени →
  `hasConvertibleChildren` обязан ПРОПУСКАТЬ их, иначе имя с контрактами (`x @{...@} := ...`) станет
  `CallExpr`.
- **`AstNodeBase::m_trust`** — контракты объявления (после имени/цикл); НЕ входят в children; из
  `term->m_left->m_sequence` (фильтр TRUST_*).
- **Термины решателя (`TrustElem`)** — операторы Z3, первый аргумент — строго маркер из
  `SMT_Z3_TERM_LIST`; квантор: первый `m_args` — связка. НЕ элементы языка.
- **Нереализованные TermID** (`Kind=Unimplemented`) → `Severity::Error` + nullptr; `END` — FAULT.
- **Защита стека:** `attr::StackGuard` → `attr::StackCheck` (необязат. целый N); функции контроля —
  `%`-нативные вызовы; арность/целочисленность атрибута валидирует семантика.
- **Система модулей:** идентификация ТОЛЬКО по индексу; `moduleId` — только при загрузке (детекция
  цикла).
- **Признак обработки атрибута:** раскладка `AttrId` и правила `-Wunhandled-attr`/`-Wunknown-attributes`
  — в `include/attrs/MEMORY.md` (value-тип и реестр вынесены в `attrs_lib`).
- **⚠ ErrorExpr — УНИВЕРСАЛЬНАЯ error/recovery-заглушка («diagnose-then-replace»):** пасс, первым
  обнаруживший ошибку и владеющий контекстом, сообщает её СРАЗУ и заменяет ошибочный под-узел на
  `ErrorExpr`-владельца. `m_original` НЕ входит в `children()`/`collectChildren` → следующие пассы
  содержимое игнорируют → нет повторной/каскадной диагностики, узел НЕ удаляется (хранится для
  дампов/диагностик). Переиспользовать вместо передачи причины в следующий пасс (введён capture-проходом
  `$^`).
- **`RecordDecl` (kind `StructDecl`) — единый узел Struct/Class (`:Name ::= :Base{, :Base}{ ... }`):**
  один класс для обоих (различие — имя абстрактной базы, НЕ поле-флаг). `m_baseTypes` (базы, IdentType),
  `m_body` (VarDecl-поля, FuncDecl-методы), `m_templateParams`. Базы НЕ входят в `children()`
  (тип-ссылки резолвит семантика объявления); члены тела — входят. Терм-конструктор читает из
  операторного терма `::=` (m_left=имя, m_right=CLASS-терм: текст=первая база, m_right-цепочка=остальные,
  m_sequence=члены). `m_body` — `std::optional`: `nullopt` — forward (маркер ELLIPSIS в m_sequence) →
  `struct c_Name;`; engaged (возможно ПУСТОЙ) — полное определение.
- **`RecordDecl::lower` — понижение членов тела:** тела методов обязаны проходить lowering (SemicolonStmt
  и пр.); без него операторы-выражения в методах не оборачивались → assignment эмитился без
  завершающей `;`. Базы-типы с типовыми аргументами (`:Base<:T>`) сохраняются в `m_baseTypes` с
  `m_template` **явно**.
- **Лямбда — `FuncDecl` (kind FuncDecl), а НЕ отдельный класс:** терм `LAMBDA` даёт узел `FuncDecl`;
  признак «лямбда» — `isLambda()` (`m_captures.has_value()`; список захватов есть всегда, возможно
  пустой `[]`). Поля: `m_params`, `m_body`, `m_type`, `m_captures` (каждый — `ArgNode`). Отдельного
  bool-флага нет. **⚠ Лямбда несёт СИНТЕТИЧЕСКУЮ метку-имя** (`__lambda__<fileIdx>_<offset>`,
  детерминировано по позиции): это делает её возврат штатным — `@return v;`/`@__FUNCTION__ ++ v ++`
  валидируются и понижаются КАК У ИМЕНОВАННОЙ функции. Имя НЕ регистрируется в скоупе и НЕ эмитится как
  C++-функция; меняет только `@__FUNCTION__`/`@__FUNCSIG__` внутри лямбды. Конструктор обрабатывает
  лямбду РАНЬШЕ ветки CREATE_NAME: захваты валидирует (только имя → ArgNode; `&name`/тип/значение →
  Error) и пишет диагностику сам. `m_captures` НЕ входят в `children()`; тело лямбды проходит
  `FuncDecl::lower`. 

- **Операторы сравнения типов — `Binary` (kind `CompareOp`), конкретика в `BinaryOp`:** `TypeIsA`(`<~`),
  `TypeDuck`(`~~`), `TypeStrict`(`~~~`); предикат `isTypeCheckOp`. Результат — `Bool`. ЕДИНСТВЕННОЕ
  доп. поле узла — `Binary::m_typeCheckConst` (`optional<bool>`): семантика статически свёртывает
  результат (наследование — транзитивные `baseClasses`; поля — `findField`), кодоген эмитит
  `true`/`false`. `nullopt` = проверка не выполнена (семантика уже сообщила ошибку) — `analyzeNodeTail`
  заменяет узел на `ErrorExpr` (диагностика НЕ дублируется, кодоген ничего не эмитит). Нового класса
  узла НЕТ (инвариант kind↔класс). LHS типовой (`:T`) резолвится через `resolveTypeRef` (обозначаемый
  тип); `exprType` — тип ВЫРАЖЕНИЯ (бывш. `resolvedType`).

- **Контроль переполнения — resolved-поле `Binary::m_overflowCheck` (`optional<bool>`):** семантика
  классифицирует `+ - *`/`+= -= *=` (предикат `isOverflowCheckableOp` в `binary_op.hpp`); кодоген только
  читает и комбинирует с `-foverflow-check`. `nullopt` — узел вне класса; `true` — знаковая машинная
  арифметика; `false` — контролируемый класс без проверки (unsigned wrap/BigInteger/Rational/не-целые/Bool).

- **⚠ Оператор-объявление — `FuncDecl` с флагом `m_isOperator`** (новых kind/классов НЕТ): признак —
  `TermID::REFLECTION` в `m_left` оператора `:=` (member-форма — из `member_name`, free — из `assign_seq`).
  ⚠ Текст = символ КАК ЕСТЬ: для оператора `normalizeTermText` НЕ применяется (он срезает хвостовой `^`
  и маркеры имён, что уничтожило бы символ). Шаблонный оператор (`<T> \`==\`(...)`) НЕ помечается
  `m_isNativeTemplateCtor` (иначе объявление молча пропадало): типовые параметры кладутся в
  `m_templateParams`, явную ошибку выдаёт `validateOperatorDecl`.

## Decisions

## Relations
