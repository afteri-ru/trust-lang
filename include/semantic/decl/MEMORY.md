# MEMORY.md

> scope: include/semantic/decl
> role: persistent-memory
> last_reviewed: 2026-11-04
> review_period: 30
> max_size: 9000

## Architecture

Анализаторы объявлений: `DeclAnalyzer` — фасад, делегирует `VarDeclAnalyzer`/`FuncDeclAnalyzer`/
`DestructureAnalyzer`, type-объявления (typedef/enum/variant/record/class, нативный шаблон-тип) остаются
в type-файлах фасада. Проверки сигнатуры — `func_signature_check` (`validate*` — fatal, `check*` —
report-only); порядок вызовов задаёт `analyzeFuncDecl`. Дополнительно: `exported_signature_check`,
`type_completeness`.

## Facts and invariants

- **⚠ Экспортируемая сигнатура не содержит file-private типов:** `exported_signature_check` обходит граф
  типа (`forEachTypeChild` + базы) и выдаёт error при объявлении.
- **⚠ Полнота «по значению» (`semantic/decl/type_completeness`):** неполнота = ВИДИМЫЙ forward из ДРУГОГО
  файла (видимая декларация из экспорт-набора), а НЕ `isCompleteType` реестра и не атрибут; в файле
  владельца forward до определения допустим; `unique` к неполному — ошибка. Определение из тела за
  forward'ом интерфейса помечено `AstNodeBase::isExportImplementation` и НЕ входит в экспортную
  поверхность — клиент видит forward. **Cross-file определяется по МЕСТУ ДИАГНОСТИКИ (`range`), а не
  по узлу аннотации:** при опущенной аннотации узел типа принадлежит callee, поэтому вывод
  (`h := make();`) проверяется по возвращаемому типу callee; узел-маркер `&&`/`&*` разворачивается
  до имени типа.
- **⚠ Record-типы (Struct/Class) — единый анализ:** `:Struct` → POD-группа, `:Class`/пользовательский →
  классы (наследование). **`baseClasses` — ОТДЕЛЬНОЕ поле дескриптора, НЕ `baseType`:** иначе
  `getDesugaredTypeId()` схлопнул бы производный в базовый. Struct не наследует — ошибка. Значения полей:
  forward `:= ...` для поля — ошибка; Struct — default ЗАПРЕЩЁН (NSDMI ломает тривиальность), только
  `:= _`. Шаблон-классы: класс-скоуп до резолва баз, базы — `resolveType`; конструктор `Box(...)`
  типизируется по типу-цели. `@[virtual]`/`@[override]` — встроенные, НЕ реализованы: явная ошибка.
- **⚠ Forward-объявление классов/шаблон-классов (`X ::= %std::...{...};`)** — узел `ClassDecl`,
  обрабатывается из `analyzeTypeDecl` (узел-ребёнок в `analyzeNode` — no-op, иначе двойная регистрация).
  Методы и поля — через `addMethod`; **поле без явного типа или не-найденное — ERROR, НЕ `Any`.** Члены:
  статический — имя с `::` (`Cls.field` → `cppName::name` + `-Wstatic-member-as-field`), экземплярный — без
  `::` (иначе `-Wclass-member-dot`). Шаблонность ВЫВОДИТСЯ из `m_template`.
- **⚠ `:A ::- T;` — ВТОРОЕ имя существующего типа (не новый тип):** тот же `TypeId`; LHS вводится как
  НОВОЕ имя и обязан быть свободен. Обе формы (от `=` и от `::-`) идут в `analyzeTypeDefine`; `=` оставлен
  ТОЛЬКО для реализации/доопределения объявленного типа по RHS record/native. Экспортируемый синоним
  проверяется `checkExportedTypeVisibility` (private — ошибка).
- **Методы:** `findMethod` односторонний (обычное имя находит и обычный, и `%native`); инвариант «одна
  форма имени».
- **Деструктуризация `t1..tN := [...]`** — точная привязка по арности; вложенная не поддерживается;
  rest-цель == источник — `pop_front`; аннотация типа на rest-цели — Error.
- **Нативные шаблоны-типы** регистрируются абстрактным шаблоном (НЕ как функция); резолв
  `vector<Int32>` интернирует инстанциацию; дубль C++-имени со встроенным — Warning.

## Relations

- `semantic/core` (обход/резолв), `types` (реестр/identity/полнота), `semantic/typer` (типы полей),
  `semantic/verify` (проверки).
