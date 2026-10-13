# MEMORY.md

> scope: include/types/func
> role: persistent-memory
> last_reviewed: 2026-11-05
> review_period: 30
> max_size: 6000

## Architecture

`fn_identity` — каноническая идентичность функций (реализация публичного заголовка корня).

## Facts and invariants

- Публичные заголовки остаются в корне компонента: зона содержит реализацию, которую другие компоненты не включают напрямую (граница internal_boundary_guard).

- **⚠ `FunctionIdentity` — идентичность сигнатур для Trust:** канонический код типа, позиционная
  канонизация binder'ов (пак — отдельный вид). Одна сегментная форма `signatureSegments` — единственный
  источник: `sameCppOverload` и хвост объявления сравниваются сегментами, а `compare(..., IdentityScope)`
  возвращает tri-state (`kDifferent|kEquivalent|kTailMismatch`), поэтому «половину» контракта проверить
  нельзя; `cppSuffix` — ПРОЕКЦИЯ того же массива сегментов (имя == identity).

- **⚠ C++-идентичность перегрузки** — типы параметров + variadic/pack (+ позиция пака), БЕЗ return
  type/trust-флагов; единственная реализация — `FunctionIdentity::sameCppOverload` (template-параметр —
  НОМЕР в списке binder'ов; пользовательский алиас — номинальный тип, встроенный representation-алиас
  раскрывается). `cppSuffix` — инъективный канонический код, ПРОИЗВОДНЫЙ от той же identity, поэтому «одна
  Trust-перегрузка ⇔ одно C++-имя». Полный контракт — tri-state `compare(..., kDeclaration)`.
  ⚠ В позиции ВОЗВРАТА отсутствие аннотации (`INVALID`), `Void` и `None` — один C++-тип (`void`); в
  позиции ПАРАМЕТРА `INVALID` — нетипизированный `:Any` (`std::any`), а не `void`.
- **Binder-контекст** — `types/call.hpp` и `symbols/decl_binders.hpp::bindersOf` (единственный
  экстрактор из объявления); `SymbolTable` берёт его из `SymbolOverload::decl`, семантика — из конкретного
  `FuncDecl`. Кодирование по имени допустимо только в `sameSemanticType`.
- **⚠ Набор перегрузок — на ПРИВЯЗКЕ ИМЕНИ, не отдельный тип:** `declareOrComplete` сравнивает через
  `compare(..., kOverload)` с binder-контекстом конкретного объявления: разные identity → `Overloaded`,
  дубль → `Duplicate`, завершение одной перегрузки → `Completed`, несовместимый хвост →
  `SignatureMismatch`. Переименование binder'а — `Completed`; неиспользуемый в сигнатуре типовой параметр —
  диагностика объявления. ⚠ `forward(MyInt)` + `definition(Int32)` — РАЗНЫЕ перегрузки. Для НАТИВНЫХ
  объявлений совпадение C++-сигнатуры → `Duplicate` (`compare(..., kInterop)`). C++-имя —
  `FuncDecl::m_cppSuffix` (ставится ВСЕГДА, применяется кодогеном безусловно); call-site копирует суффикс
  выбранного кандидата по `chosenIndex`.
- **⚠ СИГНАТУРА ВЫЗОВА (`instantiateCallSignature`) — эффективная и интернированная:** объявленная +
  `TemplateBindings` + фактические типы аргументов; pack РАЗВОРАЧИВАЕТСЯ (`paramPackType = INVALID`),
  defaults арность НЕ меняют. Идентичность вызова = (declaredSig, argTypes); порядок аргументов пака значим.

- **`TypeSystem` ⇄ `FunctionIdentity`:** `getOrCreateFunctionType`/`instantiateCallSignature` вызывают
  `internKey` и интернируют результат; `FunctionIdentity` читает реестр (`const`) и НЕ мутирует таблицу
  типов. `TypeKey::tag` вычисляется из `TypeData`; обратной зависимости `type_system.hpp → fn_identity.hpp`
  нет.
- **⚠ `EXPECT` НЕ используется как диагностика пользовательских дублей** (только инварианты реализации);
  второго пути дубль-проверки, кроме `compare`/`declareOrComplete`, нет.
