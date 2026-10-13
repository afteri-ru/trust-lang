# MEMORY.md

> scope: include/ast/nodes
> role: persistent-memory
> last_reviewed: 2026-11-04
> review_period: 30
> max_size: 9000

## Architecture

Определения классов узлов (`expr_nodes.hpp`, `decl_nodes.hpp`, `stmt_nodes.hpp`, `contract_nodes.hpp`,
`z3_term.hpp`) и общие помощники (`ast_helpers.hpp`). Публичный путь — `ast/ast_nodes.hpp`; части
напрямую извне компонента не включаются.

## Facts and invariants

- **⚠ Наборы типов (`:A + :B`) — узел `Sequence` (kind `TypeSet`), НЕ отдельный класс:** члены —
  `ArgNode`, знак — в тексте `ArgNode`. Разворота в N `FuncDecl` нет: комбинации проверяет семантика;
  в кодоген набор попадает только как ЗНАЧЕНИЕ и даёт явную ошибку.
- **⚠ `IncDecOp` (`++`/`--`) — kind класса `Binary`, УНАРНЫЙ:** глиф один, позицию кодирует СЛОТ
  операнда (постфикс → `m_left`, префикс → `m_right`). `Binary::m_op` для обычных операторов заполняется
  ОДИН раз из Term; неизвестный терм-глиф — `EXPECT` (не `None`-fallback).
- **`RefTakeExpr::TakeLowering::UserOperator`:** `*obj` для типа с объявленным `` `*` ``; вариант
  выбирает СЕМАНТИКА (до ссылочной ветки), кодоген эмитит `(*obj)`.
- **⚠ `<T...>` у `FuncDecl`/`RecordDecl`/`ClassDecl` — `ArgNode::isTypePack`:** обязан сохраняться из
  `pterm->m_isPack` при построении template-параметров (иначе пак теряется).
- **`ArgNode` — ЕДИНЫЙ узел позиции списка** (параметр/элемент/аргумент); форма позиции кодируется полем.
- **⚠ многоточие — kind `Filling`/`Ellipsis` на классе `Sequence`:** операнд — единственный/последний
  ребёнок `m_body`; список МАТЕРИАЛИЗУЕТ семантика, полей capacity/fillCount нет. ⚠ `Sequence::lower`
  понижает их `m_body` как ОПЕРАНДЫ (иначе операнд обернулся бы в `SemicolonStmt` и дал лишнюю `;`).
- **`CallExpr::resultType`** — тип результата конструктора record-шаблона, разрешённый семантикой по
  типу-цели контекста (`Box(...)` → инстанциация); кодоген не полагается на CTAD.
- **`RecordDecl` (kind `StructDecl`) — единый Struct/Class:** отличие — имя абстрактной базы, не флаг.
  `m_body = nullopt` — forward, engaged — определение; `lower` обрабатывает тела методов.
- **Лямбда — `FuncDecl`:** признак `isLambda()`; синтетическое имя `__lambda__<fileIdx>_<offset>` (в скоуп
  не регистрируется, C++ не эмитится); захваты вне `children()`.
- **Сравнение типов (`<~`/`~~`/`~~~`) — статическая свёртка** в `m_typeCheckConst` (свёрнута семантикой);
  `nullopt` — проверка не выполнена (узел заменён на `ErrorExpr`).
- **Переполнение — `Binary::m_overflowCheck`:** классифицирует семантика, кодоген читает; `nullopt` — вне
  класса, `true` — знаковая машинная, `false` — без проверки.
- **⚠ `CallExpr::resolvedSignature` — ЭФФЕКТИВНАЯ сигнатура вызова** (подставленные типы, пак развёрнут):
  авторитетна для арности/типизации; идентичность C++-имени — от объявленной. `boundArgs` — элементы
  `ArgNode::m_value` в порядке параметров (кодоген использует вместо `m_args`).
- **⚠ `argCasts` (CallExpr/Binary) — материализованные цели неявных конверсий,** заполняет
  `materializeArgCasts`; эмиттеры печатают приведение строго по слотам, не пересчитывая из сигнатуры.
- **⚠ Оператор-объявление — `FuncDecl` с `m_isOperator`;** отдельных kind/классов нет, идентичность —
  системное имя. `__...__` зарезервировано. Dispatch-поля (`resolvedOperatorSignature`/`IsMember`)
  ставит семантика только при >1 сигнатуре.
- **⚠ `IdentName::resolvedTypeId` — материализованный тип-имя** (аннотация, тип-операнд, `TypeDecl`, база
  алиаса, callee-конструктор): пишет семантика, кодоген рендерит без резолва; `INVALID` = не тип.
  `IdentType::resolvedState` — ПОЛНЫЙ тип аннотации (const/reftype/policy/impl), материализуется один
  раз `resolveTypeRef`; кодоген НЕ переприменяет квалификаторы.
- **⚠ MemberAccess: тип vs значение:** `lhsType` одинаков у `s.%field` и `MyStr.field`; тип-операнд
  определяется `IdentName::resolvedTypeId`, иначе кодоген эмитит `(obj).field`.

## Relations

- `ast/slots` (состав слотов), `ast/convert` (создание узлов), `types` (identity/state), `semantic`
  (материализация фактов), `transpiler` (чтение фактов).
