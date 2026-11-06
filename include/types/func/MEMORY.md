# MEMORY.md

> scope: include/types/func
> role: persistent-memory
> last_reviewed: 2026-11-12
> review_period: 30

## Architecture

`fn_identity` — каноническая идентичность функций (реализация публичного заголовка корня).

## Facts and invariants

- **⚠ `FunctionIdentity` — идентичность сигнатур:** канонический код типа, позиционная канонизация
  binder'ов (пак — отдельный вид). Одна сегментная форма `signatureSegments` — единственный источник:
  `sameCppOverload` и хвост сравнятся сегментами, `compare(..., IdentityScope)` возвращает tri-state
  (`kDifferent|kEquivalent|kTailMismatch`), «половину» контракта проверить нельзя; `cppSuffix` — ПРОЕКЦИЯ
  того же массива (имя == identity).
- **⚠ C++-идентичность перегрузки** — типы параметров + variadic/pack (+ позиция), БЕЗ return
  type/trust-флагов; единственная реализация — `sameCppOverload` (template-параметр — НОМЕР binder'а;
  пользовательский алиас — номинальный тип, встроенный representation-алиас раскрывается). `cppSuffix` —
  инъективный код, ПРОИЗВОДНЫЙ от той же identity, поэтому «одна Trust-перегрузка ⇔ одно C++-имя».
  ⚠ В возврате отсутствие аннотации (`INVALID`), `Void`, `None` — один C++-тип (`void`); в параметре `INVALID` —
  нетипизированный `:Any` (`std::any`), не `void`.
- **Binder-контекст** — `types/call.hpp` и `symbols/decl_binders.hpp::bindersOf` (единственный экстрактор);
  `SymbolTable` берёт его из `SymbolOverload::decl`, семантика — из конкретного `FuncDecl`. Кодирование по
  имени допустимо только в `sameSemanticType`.
- **⚠ Набор перегрузок — на ПРИВЯЗКЕ ИМЕНИ:** `declareOrComplete` сравнивает через `compare(..., kOverload)` с
  binder-контекстом конкретного объявления: разные identity → `Overloaded`, дубль → `Duplicate`, завершение →
  `Completed`, несовместимый хвост → `SignatureMismatch`. ⚠ `forward(MyInt)` + `definition(Int32)` — РАЗНЫЕ
  перегрузки. Для НАТИВНЫХ совпадение C++-сигнатуры → `Duplicate` (`kInterop`). C++-имя — целевой суффикс из
  `DeclCppFacts` (ключ — `DeclId` объявления); call-site читает суффикс по `chosenIndex` (`declCppSuffix`/`methodCppSuffix`).
- **⚠ СИГНАТУРА ВЫЗОВА (`instantiateCallSignature`) — эффективная и интернированная:** объявленная +
  `TemplateBindings` + фактические типы аргументов; pack РАЗВОРАЧИВАЕТСЯ (`paramPackType = INVALID`), defaults
  арность НЕ меняют. Идентичность вызова = (declaredSig, argTypes); порядок args пака значим.
- **`TypeSystem` ⇄ `FunctionIdentity`:** интернируют результат; `FunctionIdentity` читает реестр (`const`), не
  мутирует. `TypeKey::tag` — из `TypeData`; обратной зависимости `type_system.hpp → fn_identity.hpp` нет.
- **⚠ `EXPECT` НЕ используется как диагностика пользовательских дублей** (только инварианты); второго пути
  дубль-проверки, кроме `compare`/`declareOrComplete`, нет.
