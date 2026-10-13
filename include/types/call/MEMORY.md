# MEMORY.md

> scope: include/types/call (+ src/types)
> role: persistent-memory
> last_reviewed: 2026-11-05
> review_period: 30
> max_size: 5200

## Architecture

Единый конвейер выбора вызова и сигнатур: `overload_resolve.hpp` (`OverloadCandidate`,
`resolveCall`), `method_signatures.hpp`, `binders.hpp`, `callable_info.hpp`, `template_unify.hpp`,
`promotion.hpp`, `operator_registry.hpp`, `int_literal.hpp`. Публичный доступ — через umbrella
`types/call.hpp`. ВЫБОР — `selectCallCandidate(const TypeReadModel&, span<OverloadCandidate>)` (READ-ONLY,
без `StructuralInterner`); `resolveCall(TypeWriteContext{read, interner})` = селект + инстанцирование
`effectiveSig` выбранного template-aware кандидата. Селект без подстановки: шаблонная позиция (унификация
прошла) → `kExact`, конкретная → `overloadConversionRank` напрямую.

## Facts and invariants

- **⚠ Один алгоритм выбора:** формы free/method/operator/`[]`/`()` отличаются только заполнением
  `argTypes` и флагом `templateAware`; dominance-алгоритм — `resolveCall`.
- **⚠ Селекция READ-ONLY:** `selectCallCandidate` только `TypeReadModel&` (без `substituteTemplateParams`);
  `effectiveSig` строит только `resolveCall` (выбранный template-aware кандидат).
- **⚠ `:Any` — КОНКРЕТНЫЙ тип:** `Any→Any` exact, `Any→concrete` not viable; `INVALID`-аргумент
  остаётся Any-like (без каскада). Bound-путь — тот же кандидат, не второй алгоритм.
- **⚠ Native single-signature операторы/методы НЕ обходятся:** `[]`, `()`, унарные/бинарные всегда
  проходят `resolveCall`; C++-делегация — только native free C-linkage с единственной сигнатурой.
  Native-template инстансы — полноценные операторные типы; статические перегрузки native-класса — набор
  `SymbolOverload` с фиксированным C++-именем (`Class::member`, без trust-суффикса).
- **⚠ Методы — полный ключ-имя → НАБОР `CallableSignature`:** ключ кодирует нативность (ведущий `%`) и
  константность (хвостовой `^`). `addMethod` ДОБАВЛЯЕТ сигнатуру (C++-идентичный дубль — `EXPECT`), другой
  ключ с тем же bare-именем+константностью — `EXPECT` «одна форма имени». Алиас повторяет семантику цели;
  const/не-const — разные записи. `findMethodInfo` отдаёт `MethodRef` с `owner` (тип дескриптора, где
  объявление найдено: конверсия неявного object-параметра); перегрузку выбирает `resolveCall`.
  `hasOwnMemberName` запрещает два поля с одним именем.
- **⚠ `CallableParamInfo` (имена/default) НЕ входят в `TypeKey`/C++-суффикс:** сигнатура — только типы
  параметров. Методы инстанса record-шаблона НЕ копируются энергично: они материализуются on-demand из
  шаблона при первом доступе (`MethodTable::findMethodInfo` → `StructuralInterner::materializeRecordInstanceMembers`,
  подставляется только `signature`) и мемоизируются в `desc->methods`. Пустые метаданные = native/builtin
  (named-binding не применяется). AST-узлы в `types` НЕ хранятся.
- **⚠ Единый конвертер целого литерала:** `0/1(+)`→Int8 (НЕ Bool), влезающий → минимальный знаковый Int,
  `>2^63` → BigInteger; знак учитывается; Bool из `0/1` — ТОЛЬКО явная аннотация. Границы —
  `fitsIntegerValue`/`intTypeForWidth`/`intTypeForLiteral`.
- **Продвижение арифметики — TypeId-aware, канонизирует алиасы:** `commonArithmeticType` всегда даёт
  знаковый Int32/Int64; беззнаковый результат — только в `promoteSingleNumeric`.
- **Доступ к членам/индексации:** операторный тип — record | native class | native template instance;
  отсутствие оператора/поля — явная диагностика; словарный/`Any`/`Bool` fallback ЗАПРЕЩЁН.

## Decisions

## Relations

- `types/func` (`FunctionIdentity`), `types/registry` (интернирование), `types/types.hpp`.
