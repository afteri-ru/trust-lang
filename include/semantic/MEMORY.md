# MEMORY.md

> scope: include/semantic
> role: persistent-memory
> last_reviewed: 2026-08-25
> review_period: 30
> max_size: 12388

## Architecture

Semantic analyzer — **pass manager**: единое однопроходное ядро `NameResolutionPass`
(обход + скоуп-стек `SymbolTable` + резолв имён) плюс опциональные анализаторы, подключаемые
параллельно через `InlineAnalysisHook` (например `LintHook`, gate `FlagKind::Lint`). Семантика
**однопроходная**: имя должно быть объявлено до использования (forward references исключены
синтаксисом). Пайплайн: `Parser → SemanticPassRunner → [on success] → CppTranspiler`.

`NameResolutionPass` — **драйвер** (издатель событий, НЕ хук): владеет обходом и скоуп-стеком,
публикует события подписчикам-хукам. Тяжёлая логика разнесена по внутренним анализаторам
(`DeclAnalyzer`/`ExprTyper`/`AccessResolver`/`TrustAnalyzer`), разделяющим `AnalysisContext` (m_actx)
и обращающимся к драйверу через `m_core` (дружба). Всегда-подключённый хук — `ContextMacroExpander`
(раскрывает контекст-макросы в том же обходе; эталон для написания новых анализаторов).

`AnalysisContext` (`semantic/pass.hpp`) — единые query-сервисы (без повторного разбора скоуп-стека/
реестра типов): `namespacePath()`/`currentFunc()`, `resolveType()`, `resolvedType(node)`. Типы
выражений кешируются в `AnalysisContext::m_exprTypes` (node → TypeId).

## Facts and invariants

- **lowering переносит «анализирующую» логику в AST** (транспилятор — только кодогенератор):
  `lowerBody` оборачивает statement-выражения в `SemicolonStmt`, переписывает именованные break/
  continue в `GotoStmt`/`LabelStmt`, break по имени функции — в синтетический void-`return`. Синтетические
  узлы (`LabelRef`) имеют НЕВАЛИДНЫЙ range (не маппятся); `SemicolonStmt` делегирует range обёрнутому
  выражению.
- **trust-контракты** (`TrustContract`, единый класс; kind в `TrustContract::kind`, expr в `m_expr`)
  `collectChildren()` НЕ отдают — ядро обрабатывает их ЯВНО. `processTrustConditions` — единая точка по
  ДВУМ ортогональным опциям: severity `-Wsolver` (presence-диагностика, default warning, выдаётся только
  когда `--solver-mode` НЕ задан) и поведенческий `--solver-mode` (assert/export/calculate). `export`/
  `calculate` НЕ генерируются здесь — сбор VCs в SMT-LIB 2 делает отдельный проход `PipelineSteps::Solver`
  (`solver::TrustToSmt`). В пред-условии (Pre) использование имени самой функции — ошибка; в
  пост-условии (Post) имя функции = возврат, легально.
- **Защита от авто-вывода типа:** тип данных с trust-условиями (бит `kTrustFlag`) не может быть выведен
  автоматически — в `typeExpr` при `typeIsTrusted(resolvedType(init))` ошибка с требованием явной
  аннотации.
- **⚠ trap (enum):** члены сравниваются ПО ЗНАЧЕНИЮ, но type-safe (оба операнда — enum; нельзя
  `Weight.ZERO == 0`). **Enum** — единый тип значений у всех членов; **Variant** — каждый член СВОЕГО
  типа. Явный тип члена `name:Type` хранится в `ArgNode.m_type` (отдельный слот).
- **Тип переменной — монотонное расширение:** нетипизированная `x := expr` получает тип из
  инициализатора с битом `withInferred`; по присвоениям тип расширяется (join). Инициализатор без
  выводимого типа (C++-вставка `{% %}`, вызов с неизвестным результатом, отрицательный литерал) ЯВНО
  маркируется `std::any` — `INVALID` у переменной с инициализатором в кодогенерации трактуется как
  ошибка вывода (без fallback). Голый тип-имя справа в `:=` — ошибка (тип объявляется через `::=`).
- **Auto-Bool vs явный Bool:** auto-Bool (`mult := 1`) в составной числовой арифметике продвигается до
  максимального Int (Int64); явный `:Bool` так НЕ расширяется — для него это ошибка.
- **Константность:** при `^` на имени или `@[readonly]@` семантика ставит бит на `Symbol::type`
  («константность в типе»). `x^ := 42` → `const int8_t`; «become-const» (`x := 42; x^ += 1;`) делает
  переменную константной ДАЛЕЕ, но ДЕКЛАРАЦИЯ остаётся не-const. Обычная запись в уже константную —
  ошибка.
- **Сужение (checkAssignmentNarrowing):** в ЯВНО-типизированную цель проверяется по ширине; переменная
  шире цели → ошибка + fixit «use cast `:Type(expr)`». Inferred-цели не проверяются (монотонно расширяются).
- **Деструктуризация `t1,...,tN := [...];`:** без маркера — точная привязка по статической арности.
  Вложенная деструктуризация не поддерживается. rest-цель == источник — мутация `pop_front`
  (единственное допустимое переиспользование имени rest-цели); прочее переиспользование — Error
  (кодген даёт C++-redefinition/UB). Аннотация типа на rest-цели (`rest:Type...`) — Error.
- **Методы (obj.method):** поиск `findMethod` ОДНОСТОРОННИЙ: обычное имя (`c_str`) находит и обычный, и
  нативный (`%c_str`) метод; нативное имя — только точное. `addMethod` отклоняет регистрацию, если уже
  присутствует вторая форма имени (EXPECT) — инвариант «одна форма имени».
- **Формат-проверка `@[format("printf", strIdx, firstToCheck)]`** — индексы 1-based (GCC-конвенция);
  формат-строка обязана быть строковым литералом; для `%s` ожидается `const char*` (CString),
  StrChar-переменная требует явного `.c_str()`.
- **SymbolCollectorHook:** имя берётся из `sym.decl->text()` (на `onDeclare` `sym.name` уже moved-from).
  `takeSymbolIndex()` отдаёт символы даже при ошибках (на частичном AST), если флаг `-Wsymbols` включён.
- **Ловушка (ContextMacroExpander):** обход детей — через единый `AstNodeBase::collectChildren`
  (ссылки на слоты, чтобы можно было заменять `ContextMacro` на `Literal`/`IdentName`); мутирующий
  `onNode(AstNodePtr&)` возвращает true, если узел заменён (ядро пропускает `handleNode`, но обходит детей).
