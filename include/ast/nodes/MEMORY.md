# MEMORY.md

> scope: include/ast/nodes
> role: persistent-memory
> last_reviewed: 2026-11-13
> review_period: 30

## Architecture

Определения классов узлов (`expr_nodes.hpp`, `decl_nodes.hpp`, `stmt_nodes.hpp`, `contract_nodes.hpp`,
`z3_term.hpp`) и общие помощники (`ast_helpers.hpp`). Публичный путь — `ast/ast_nodes.hpp`; части извне
компонента не включаются.

## Facts and invariants

- **⚠ Наборы типов (`:A + :B`) — узел `Sequence` (kind `TypeSet`), НЕ отдельный класс:** члены — `ArgNode`,
  знак — в тексте. Разворота в N `FuncDecl` нет: комбинации проверяет семантика; в кодоген набор попадает
  только как ЗНАЧЕНИЕ и даёт явную ошибку.
- **⚠ `IncDecOp` (`++`/`--`) — kind класса `Binary`, УНАРНЫЙ:** глиф один, позицию кодирует СЛОТ операнда
  (постфикс → `m_left`, префикс → `m_right`). `Binary::m_op` для обычных операторов заполняется один раз из
  Term; неизвестный глиф — `EXPECT`.
- **`RefTakeExpr::TakeLowering::UserOperator`:** `*obj` для типа с объявленным `` `*` ``; вариант выбирает
  СЕМАНТИКА (до ссылочной ветки).
- **⚠ `<T...>` у `FuncDecl`/`RecordDecl`/`ClassDecl` — `ArgNode::isTypePack`:** обязан сохраняться из
  `pterm->m_isPack` при построении template-параметров (иначе пак теряется).
- **`ArgNode` — ЕДИНЫЙ узел позиции списка** (параметр/элемент/аргумент); форма — полем.
- **⚠ многоточие — kind `Filling`/`Ellipsis` на классе `Sequence`:** операнд — единственный/последний
  ребёнок `m_body`; список МАТЕРИАЛИЗУЕТ семантика, полей capacity/fillCount нет. ⚠ `Sequence::lower` понижает
  их `m_body` как ОПЕРАНДЫ (иначе операнд обернулся бы в `SemicolonStmt` и дал лишнюю `;`).
- **`CallExpr::resultType`** — тип результата конструктора record-шаблона, разрешённый семантикой по
  типу-цели (`Box(...)` → инстанциация); кодоген не полагается на CTAD.
- **`RecordDecl` (kind `StructDecl`) — единый Struct/Class:** отличие — имя абстрактной базы, не флаг.
  `m_body = nullopt` — forward.
- **Лямбда — `FuncDecl`:** признак `isLambda()`; синтетическое имя `__lambda__<idx>_<offset>` (в скоуп не
  регистрируется, C++ не эмитится); захваты вне `children()`.
- **Сравнение типов (`<~`/`~~`/`~~~`) — статическая свёртка** в `m_typeCheckConst`; `nullopt` — проверка не
  выполнена (узел заменён на `ErrorExpr`).
- **Переполнение — `Binary::m_overflowCheck`:** классифицирует семантика, кодоген читает; `nullopt` — вне
  класса, `true` — знаковая машинная.
- **⚠ `CallExpr::resolvedSignature` — ЭФФЕКТИВНАЯ сигнатура вызова** (подставленные типы, пак развёрнут):
  авторитетна для арности/типизации; C++-имя — от объявленной. `boundArgs` — элементы `ArgNode::m_value` в
  порядке параметров (кодоген — вместо `m_args`).
- **⚠ `argCasts` (CallExpr/Binary) — материализованные цели неявных конверсий** (заполняет
  `materializeArgCasts`); эмиттеры печатают строго по слотам, не пересчитывая из сигнатуры.
- **⚠ Оператор-объявление — `FuncDecl` с `m_isOperator`;** отдельных kind/классов нет, идентичность — системное
  имя. `__...__` зарезервировано. Dispatch-поля ставятся только при >1 сигнатуре.
- **⚠ Вариант наследования баз — ПОЛЕ рядом с базой:** `RecordDecl`/`ClassDecl` хранят базы как
  `TypeBase{type, access}` / `NativeBase{cppName, access}` (`access` — `trust::Visibility`,
  единый X-macro `types/visibility.hpp`; тот же уровень, что и видимость объявлений).
  Маркер в синтаксисе — standalone-элемент списка (`::`/`_::`/`__::`), действует на все последующие
  базы до нового; дефолт — Public для ВСЕХ типов (private/protected — явным маркером). Признак маркера —
  по ТЕКСТУ терма (не TermID): `class_type_def` перезаписывает `m_id` головного терма на CLASS.
- **⚠ Видимость ЧЛЕНОВ тела класса — `AstNodeBase::visibility` (member-смысл):** standalone-маркер
  `::`/`_::`/`__::` встречается в `m_sequence` как term `TermID::NAMESPACE`; единый
  `convertClassMembers` (`src/ast/nodes/ast_decl.cpp`) трактует его как уровень для ВСЕХ последующих членов и в
  `m_body`/`m_definition` НЕ кладёт, а каждому члену ставит текущий уровень (дефолт Public). Иное
  имя-область (`foo::`) — диагностика. Кодоген печатает `public:/protected:/private:` при смене
  уровня В ПОРЯДКЕ ОБЪЯВЛЕНИЯ (см. `transpiler/emit`).
- **⚠ `IdentName::resolvedTypeId` — материализованный тип-имя:** пишет семантика, кодоген рендерит без резолва;
  `INVALID` = не тип. `IdentType::resolvedState` — ПОЛНЫЙ тип аннотации (const/reftype/policy/impl),
  материализуется один раз `resolveTypeRef`; кодоген НЕ переприменяет квалификаторы.
- **⚠ MemberAccess: тип vs значение:** `lhsType` одинаков у `s.field` и `MyStr.field`; тип-операнд
  определяется `IdentName::resolvedTypeId`, иначе кодоген эмитит `(obj).field`.
- **⚠ Единое определение `Definition` (`FuncDecl::m_definition`, `RecordDecl::m_definition`):**
  payload'ы (`m_body` XOR `m_forwardedTo`) **приватны**, задаются только фабриками
  (`fromBody`/`forwardTo`) — состояние «тело И переадресация» невозможно. `DefinitionKind` —
  ПРОИЗВОДНЫЙ от payload. Нативная привязка — отдельный факт `Decl::m_bindsNativeCpp` (предикат);
  само C++-имя живёт у владельца `DeclCppFacts` (`target`) под `DeclId`; до назначения id — временный
  носитель `Decl::m_pendingNativeCppName`, который `assignDeclarationIds` переносит и очищает.
  (`Decl::nativeRole()`); ABI — `cppLinkageOf(name)` (`::` ⇒ C++). Доступ — `definitionKind()`,
  `isDefinition()`, `body()`, `forwardingTarget()`; `hasImplementation()` = определение | native-import.
  `isNativeImport()/isNativeExport()` — ПРОИЗВОДНЫЕ в БАЗЕ `Decl` (единая формула над `nativeRole()`).
  Слот тела — `DefBody`; переадресация — строка, не ребёнок. Пустое тело `f() := {};` — определение.
- **⚠ `VarDecl::initKind()` — ПРОИЗВОДНЫЙ факт, поля-состояния нет:** `Forward` (нет
  инициализатора/места хранения), `DeferredInit` (`x := _`), `Uninitialized` (`x:T := _`),
  `Initialized` (payload; все синтетические временные), `Bound` (значение задаёт конструкция —
  `catch(e:T)`, флаг `m_isCatchBinding`). Экспорт требует `Initialized`; роль (`nativeRole()`) —
  ПРОИЗВОДНАЯ от `initKind()` и наличия нативного имени.
- **⚠ Два уровня факта «определение»:** вид — свойство узла (производный payload); полнота
  СУЩНОСТИ — производный факт `Symbol`/`SymbolOverload` (слияние редеклараций). Обратной ссылки
  «узел → завершающее объявление» нет намеренно: AST клонируется (`buildHir`) и пересоздаётся;
  потребитель полноты сущности идёт через символ, а не через конкретный узел.

## Relations

- `ast/slots` (состав слотов), `ast/convert` (создание узлов), `types` (identity/state), `semantic`
  (материализация фактов), `transpiler` (чтение фактов).
