# MEMORY.md

> scope: include/types/call (+ src/types)
> role: persistent-memory
> last_reviewed: 2026-11-12
> review_period: 30

## Architecture

Единый конвейер выбора вызова и сигнатур: `overload_resolve.hpp` (`resolveCall`), `method_signatures.hpp`,
`binders.hpp`, `callable_info.hpp`, `template_unify.hpp`, `promotion.hpp`, `operator_registry.hpp`,
`int_literal.hpp`; публичный доступ — `types/call.hpp`. ВЫБОР — `selectCallCandidate(const TypeReadModel&,
span<OverloadCandidate>)` (READ-ONLY, без `StructuralInterner`); `resolveCall(TypeWriteContext)` = селект +
инстанцирование `effectiveSig` template-aware кандидата. Селект без подстановки: шаблонная позиция
(унификация прошла) → `kExact`, конкретная → `overloadConversionRank`.

## Facts and invariants

- **⚠ Один алгоритм выбора:** формы free/method/operator/`[]`/`()` отличаются только заполнением `argTypes` и
  флагом `templateAware`.
- **⚠ Селекция READ-ONLY:** `selectCallCandidate` — только `TypeReadModel&`; `effectiveSig` строит только
  `resolveCall`.
- **⚠ `:Any` — КОНКРЕТНЫЙ тип:** `Any→Any` exact, `Any→concrete` not viable; `INVALID`-аргумент остаётся
  Any-like. Bound-путь — тот же кандидат, не второй алгоритм.
- **⚠ Native single-signature операторы/методы НЕ обходятся:** `[]`, `()`, унарные/бинарные всегда проходят
  `resolveCall`; C++-делегация — только native free C-linkage с единственной сигнатурой. Статические
  перегрузки native-класса — набор `SymbolOverload` с фиксированным C++-именем (без trust-суффикса).
- **⚠ Методы — структурный `MethodKey` → `MethodInfo`:** ключ хранит имя-форму (`name` +
  `isConst`/`isStatic`); запись — `overloads` + `isNative` (C++-проекция, НЕ идентичность).
  trust-spelling разбирается один раз `methodKeyFromSpelling`. `addMethod` ДОБАВЛЯЕТ сигнатуру
  (C++-идентичный дубль — `EXPECT`); другой ключ с тем же именем+константностью — EXPECT «одна форма
  имени». `findMethodInfo` отдаёт `MethodRef` с `owner`/`isNative`; перегрузку выбирает `resolveCall`.
  `hasOwnMemberName` запрещает дубль-поля.
- **⚠ `CallableParamInfo` (имена/default) НЕ входят в `TypeKey`/C++-суффикс:** сигнатура — только типы
  параметров; `CallableSignature::decl` (`DeclId`) — лишь КЛЮЧ целевого суффикса (владелец — `DeclCppFacts` в
  `target`), не сам факт. Методы инстанса record-шаблона НЕ копируются энергично — материализуются on-demand при первом
  доступе и мемоизируются в `desc->methods`. Пустые метаданные = native/builtin. AST-узлы в `types` НЕ
  хранятся.
- **⚠ Единый конвертер целого литерала:** `0/1(+)`→Int8 (НЕ Bool), влезающий → минимальный знаковый Int,
  `>2^63` → BigInteger; Bool из `0/1` — ТОЛЬКО явная аннотация.
- **Продвижение арифметики — TypeId-aware, канонизирует алиасы:** `commonArithmeticType` всегда даёт знаковый
  Int32/Int64; беззнаковый результат — только в `promoteSingleNumeric`.
- **Доступ к членам/индексации:** операторный тип — record | native class | native template instance;
  отсутствие — явная диагностика; словарный/`Any`/`Bool` fallback ЗАПРЕЩЁН.
- **⚠ Глиф оператора — ОДИН источник:** строки-символы заданы в `utils/operator_spelling.hpp`
  (`trust::spell`); `OperatorInfo` хранит только `symbol` (+dunder/arity/флаги). C++-имя (`operator<symbol>`)
  и число параметров (`operatorParamCount`) ВЫЧИСЛЯЮТСЯ. AST-таблица `ast/binary_op.hpp` — проекция
  над теми же константами (тест `GlyphsMatchAstSpellings`).

## Relations

- `types/func` (`FunctionIdentity`), `types/registry` (интернирование), `types/types.hpp`.
