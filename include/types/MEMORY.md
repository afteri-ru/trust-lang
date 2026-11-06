# MEMORY.md

> scope: include/types
> role: persistent-memory
> last_reviewed: 2026-11-13
> review_period: 30

## Architecture

Закрытая система типов: `TypeKind` — идентификация без реестра; `TypeSystem` — метаданные
пользовательских/параметризованных. `TypeId` — identity-view, `QualType` — qualifier-view того же raw.
`TypeKind`/`TypeId`/`QualType` определены ОДИН раз в `runtime/trust/type_state.hpp` (общий ABI-заголовок);
параметризованные типы интернируются по структуре (`TypeKey`).

**Встроенный тип — предобъявление + отдельная реализация:** предрегистрируется при создании реестра;
реализация — ОПРЕДЕЛЕНИЕ `=` (`::=` для встроенного имени запрещён). Нет реализации при кодогенерации —
диагностика provider-gate `implementation of built-in type X is not defined` (имя модуля компилятору
неизвестно и не называется), НЕ «тип X неизвестен».
- **⚠ stdlib-обеспеченные builtin (StrChar/StrWide, Rational/BigInteger, Dict, Array, Range, Tensor,
  Float16, Complex32/64, Any) — интерфейс в ядре, реализация в `.src`:** дескриптор без C++-имени/include
  (они — целевой факт `CppTypeFacts` компонента `target`);
  методы — forward без нативного имени; C++-тип/include/`@[link]`/привязки `%` — из `stdlib/builtin/*.src`
  (Tensor — провайдер). Одна реализация на TypeId: повтор `=` — диагностика `-Wbuiltin-override`
  (дефолт warning); provider-gate (БЕЗ имён модулей) — в Pipeline до HIR. Реализация обязана ПОКРЫВАТЬ
  все интерфейсные методы типа (лишние члены допустимы).
  Интринсики (Bool/Int*/UInt*/Float32/64, CString) несут C++-имя в целевых фактах (`CppTypeFacts` компонента `target`); IntMinus/Plus и
  access/deleter-типы — forward-интерфейс без C++-имени (C++-имя из `exceptions.src`/`mem.src`).
- **⚠ `Any` (std::any) vs `Auto` (внутренний sentinel) — РАЗНЫЕ группы:** `Group::kAuto` (0) — «тип не
  выведен» (variadic-слот, dims тензора, элемент абстрактного контейнера); НЕ гейтится и НЕ доходит до
  кодогена. `Group::kAny` — верхний МАТЕРИАЛИЗУЕМЫЙ RTTI-тип: грузится ЯВНО (`builtin/any.src`,
  `\any();`/`\std();`); provider-gate требует его при материализации (явный `:Any`, widening,
  forward-переменные, нетипизированный параметр).
- **⚠ `@[borrowed]`-метки — в `TypeSystem`:** имена классов/методов с `@[borrowed]` собирает семантика
  объявлений (`markBorrowedClass/Method`, чистятся `reset()`); borrow-анализ читает их ОТТУДА, а не только из
  корневого AST — AST импортируемых модулей в корень не входит, иначе `@[borrowed]` `Iterator` невидим.

| internal-подкаталог | MEMORY | ответственность |
|---|---|---|
| `types/call/` | `call/MEMORY.md` | `resolveCall`, сигнатуры методов, binder'ы, унификация шаблонов, операторы |
| `types/func/` | `func/MEMORY.md` | `FunctionIdentity`: `cppSuffix`, набор перегрузок |
| `types/names/` | `names/MEMORY.md` | `StorageKey`, видимость на сайте импорта |
| `types/registry/` | `registry/MEMORY.md` | состояние/печать, ядро встроенных, интернирование, структурные виды |
| `types/subst/` | `subst/MEMORY.md` | подстановка типовых параметров, унификация |
| `types/type/` | `type/MEMORY.md` | `data_*.hpp`, `Group`, `RefType`, представление `TypeId` |

## Facts and invariants

- **⚠ `TypeId` — identity-view, `QualType` — qualifier-view:** `TypeId` — ключ интернирования/перегрузок;
  `QualType` — identity + const/inferred, НЕ ключ, без `==`/`std::hash`. Квалификаторы НЕ входят в ключ
  (`const T`/`T` делят дескриптор).
- **⚠ Три отношения identity — единая точка `identityOf(TypeId, TypeIdentityScope)`:** `kStructural` (ключи
  интернирования), `kSemantic` (алиасы раскрыты, пользовательские номинальны), `kCanonical` (корень
  alias-цепочки, C++-представление); `kCppInterop` = `kCanonical` + раскрытие алиасов на каждом уровне.
  Глубокая эквивалентность — только `FunctionIdentity::typeCode`.
- **⚠ Пользовательский алиас (`Name ::= <base>`) — НОВЫЙ номинальный тип Trust** (отдельный `TypeId`; в C++ —
  `using`), встроенные алиасы прозрачны. **`:A ::- T;` — ВТОРОЕ имя того же `TypeId`.** Отсюда `MyInt`/`Int32` —
  разные перегрузки, `Array<MyInt>`/`Array<Int32>` — разные типы.
- **⚠ Три константности не смешивать:** type-level `kConstFlag`, declaration `attr::ReadOnly` (→ `const` +
  запрет записи), became-const (flow-факт DA). `const_cast` — только явный `obj.method^()`.
- **⚠ Имена типов: storageKey ≠ display name** (ключ СТРУКТУРНЫЙ, текст не кодируется); `nominalCppName`
  квалифицирует ТОЛЬКО namespace-путём (`ns::Point`); имя модуля в C++-имя НЕ входит (ABI прозрачен).
- **⚠ Единый `resolveCall` для ВСЕХ форм вызова**; `:Any` — конкретный тип, `INVALID`-аргумент Any-like.
- **⚠ `TypeReadModel` — READ-ONLY view зон;** мутации — только через зоны записи. Берётся через
  `Context::readTypes()`/`TypeSystem::read()`.
- **⚠ RefType:** X-macro единый источник (`&&`/`&*`/`&?`), над алиасом — составной узел, deleter — часть типа
  только для `unique`, двухосевая модель (владение × доступ). Детали — `type/`.
- **`RuntimeSymbol`** — только не-типовые функции (X-macro). **Два имени:** `runtimeSymbolSourceName`
  (имя в trust-коде, напр. `trust__print__`) и `runtimeSymbolName` (C++-символ, напр.
  `trust::trust__print__`); `findRuntimeSymbolByName` принимает любое из них. **BigInteger — часть Rational**
  (GMP, pimpl) в ДЕФОЛТНОМ провайдере `rational.src`; `kArbitraryPrecision`; `isArithmeticGroup` их НЕ включает.

- **⚠ Три уровня видимости — ЕДИНЫЙ источник `types/visibility.hpp`** (X-macro
  `TRUST_VISIBILITY_LEVELS`: имя enum, C++-ключевое слово, имя области). Из той же таблицы
  производится разбор spelling (`visibilityOf`) — отдельного перечисления строк нет. Тот же тип
  (`Visibility`) — вариант наследования базы (`BaseClass::access`, AST-базы, `CppBase`) и
  C++-доступ ЧЛЕНА класса: `TupleElementData::access` (поле) / `CallableSignature::access` (метод),
  заполняются семантикой из `AstNodeBase::visibility` объявления; проверка — `semantic/typer/member_access.hpp`.
- **⚠ Один владелец формы record — типовой слой:** поля объявленного record пишет только
  `NamedTypeRegistrar::defineRecord` (единственный вызов — `semantic/decl/record_decl_analyzer.cpp`), читаются
  только через аксессор формы; второе хранилище запрещено (R1 корневого `MEMORY.md`). Тип поля — ТОЛЬКО форма
  (`TupleElementData::type`); `Symbol::type` поля — предикат объявления (E13), `VarDecl::inferredType` поля —
  проекция AST-фактов для бэкенда (E2/R6), а не владелец.
- **⚠ Связь «член AST ↔ поле формы» — по ключу `DeclId` (`TupleElementData::declId`), НЕ по индексу
  (R14):** кодоген ищет поле формы по `VarDecl::declId()`; параллельных векторов (типы/имена по
  индексу) нет. Типовые параметры шаблона: один носитель (до определения — `m_declaredRecords`, при `defineRecord`
  переносятся в форму, маркер стирается — `takeDeclaredRecord`).

- **⚠ Целевые decl-факты ВЫНЕСЕНЫ из `types` в компонент `target`:** C/C++-имя нативной привязки и
  C++-суффикс сигнатуры хранит `DeclCppFacts` (`target/decl_cpp_facts.hpp`) под `DeclId`; владелец — per-run
  `TargetContract` (создаёт `Pipeline`), порт — `Context::target()`/`Context::declCppFacts()`. `TypeStore`/
  `TypeReadModel` эти факты больше НЕ хранят и НЕ выдают (ребро `types→target` для decl-части разорвано, R34).
  Языковая модель имя НЕ хранит: `Decl` несёт только предикат `m_bindsNativeCpp`; на фазе конвертации имя живёт
  во временном носителе `Decl::m_pendingNativeCppName`, `assignDeclarationIds` переносит его в `DeclCppFacts` и
  очищает узел (R1). Хранилище НЕ очищается `TypeSystem::reset()`. **Зона `MethodTable` хранит только КЛЮЧ,
  не факт:** `CallableSignature::decl` (зона `types`) несёт `DeclId` объявления метода, а сам C++-суффикс
  остаётся у владельца — `DeclCppFacts::cppSuffixOf(decl)`; читатель — `methodCppSuffix` (семантика). У
  builtin/синтетических методов объявления нет → ключ пуст, суффикс не назначается.
  **Тип-факты `CppTypeFacts`, делегаты методов и рантайм-символы также вынесены в `target`:** `types`
  целевых C++-фактов НЕ хранит; C++-рендеринг — кодоген (`CppTypeRenderer`); читатели — порт кодогена
  `transpiler/type/TypeCppFactsView` и путь семантики через `Context`.

## Decisions

- **Алиас — привязка ИМЕНИ, не свойство типа:** alias-метаданные живут в объявлении/analysis-таблицах; в
  рантайме алиасов нет (C++-имя нормализовано на цель).
- **Единая точка C++-идентичности сигнатур — `FunctionIdentity`** (AST-независима).
- **Native-контракт:** C++-делегация — только native free C-linkage single-signature; native-имена (вкл.
  статические члены) не манглируются.

## Relations

- `semantic` (`declaredCppName`-предикат в `provider_check` — читается через `Context`/`target`), `transpiler`
  (`cppSuffix`; C++-рендеринг — `CppTypeRenderer`, вынесен из `types`; порт целевых фактов —
  `TypeCppFactsView`), `runtime` (`type_state.hpp` — единственное
  определение `TypeId`/`QualType`), `symbols` (binder-контекст).
