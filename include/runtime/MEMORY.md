# MEMORY.md

> scope: include/runtime
> role: persistent-memory
> last_reviewed: 2026-11-10
> review_period: 30

## Architecture

Рантайм входит в пакет `stdlib.trust` (единый способ доставки); публикуется под `@trust/…`
(рантайм-примитивы) и `@stdlib/builtin/…` (заголовки встроенных типов), версионируется с компилятором;
`--no-stdlib` их НЕ отключает (в отличие от загрузки реализаций `\std();`). `trust-runtime` несёт только
скомпилированный код (io/backtrace). Runtime-заголовки — один из провайдеров реализации встроенного типа
(Int*/Bool — intrinsic; StrChar/StrWide — генерируются из stdlib/builtin/string.src, а UTF-8 и
`std::formatter` — рантайм-примитивы `@trust/string_support.hpp`). **Rational/BigInteger — header-only в stdlib**
(`stdlib/builtin/*.hpp`, только `-lgmp`), TU рантайма их не содержит. Детали — `runtime/README.md`.

## Facts and invariants

- **⚠ `unique` монополен, guard'а нет:** без deleter — `trust::StaticUnique<T>` (zero-cost, move-only); с
  `@[deleter(D)]` — `trust::Unique<T,D>`. Заёма/alias НЕТ.
- **⚠ `trusted-cpp.hpp` (ВСЕГДА при ссылке) не включает `<thread>/<mutex>/<chrono>`;** межпотоковая
  синхронизация — ОТДЕЛЬНО `trusted-cpp-sync.hpp` (`AccessShared<V,Mutex>`), доступ ТОЛЬКО через
  `lock()/lock_const()`. Политики — только `shared`/`weak`.
- **⚠ guard — `detail::GuardBase` (CRTP), non-copyable/movable;** `Locker<V,RO>` — shared-capture наблюдатель,
  POLICY-AGNOSTIC. Сырые ссылки (`&`/`*`) и `unique` guard'а НЕ имеют.
- **⚠ observer-заём — ТОЛЬКО у `Shared`:** `borrow()/borrow_const()` → `SharedBorrowedRef<V,RO>` (над weak_ptr):
  после гибели владельца `valid()==false`/`access()` бросает.
- **⚠ три разных «borrow» не смешивать:** `Shared::borrow()` — RUNTIME-наблюдатель; `@[borrowed]`/
  `-Wborrow-*` — статическая проверка инвалидации производных представлений при мутации (не заём Rust);
  см. `semantic/ref/BORROW_CHECKER.md`.
- **Конверсии владения:** `Unique↔Shared` = `to_shared()`/`to_unique()` (`use_count()==1`; перенос ЗНАЧЕНИЯ →
  смена идентичности); `borrow` требует `use_count()==1`.
- **⚠ таймаут детектора дедлока — ОДНО значение `syncDeadlockTimeout()`** (дефолт 5s; `>0` ждать/истёк→IntMinus,
  `0` try-once, `<0` блокировать; булева флага НЕТ). Перекрытие: `-fsync-deadlock=`,
  `--trust:f(sync|no-sync)-deadlock`, конструктор `AccessShared(value, timeout)` (в `@[reftype]` не задаётся).
- **⚠ многоуровневая ссылочность — валидный ТИП, но пересечение уровней в ОДНОМ операторе запрещено**
  (цепочечного `ref.lock().lock()` нет).
- **Прерывания:** `IntAny` + `IntPlus` (control flow) + `IntMinus`. ⚠ **`IntMinus` наследует
  `std::runtime_error`** — ловится и trust-блоком, и C++; `IntPlus` НЕ `std::exception`. НЕИМЕНОВАННОЕ `++ v ++`
  всегда бросает; ИМЕНОВАННОЕ — return (валидирует semantic).
- **Защита стека:** `stack_overflow : public IntMinus`; резерв `reserve` (default 8192) добавляется ВСЕГДА:
  `check_overflow(N)`/`check_stack_limit()` сравнивают `free` с `N/reserve` (`m_stack_limit` из `.stack_sizes`).
- **Рантайм-парсер CLI:** `_main.cppt` отделяет аргументы среды по `--trust:`; из остальных строит ДВА
  `trust::Dict`: `argv` (позиционные) и `args` (`--key=value`/`--flag`→"true"); `--` — разделитель; `TRUST_*`
  рантайм читает через `getenv()`.
- **⚠ Move-only `Unique`:** копирование эксклюзивного владения диагностирует СЕМАНТИКА (не C++-ошибка);
  рантайм-заголовки ссылок выбирает единая `refTypeRuntimeIncludes`.
- **`@[include("@trust/<h>")]` на нативном классе** → on-use; для библиотечных — `@stdlib/<h>`.
  `EnableSharedFromThis<V>` — миксин самоадресации; `shared_from_this()` → IntMinus, если не во владении/истёк;
  копия ссылку НЕ наследует.
- **GMP — внешняя зависимость:** её отсутствие обязано давать диагностику, а не ошибку линковки.
- **⚠ `trust/tensor.hpp` — self-contained value type:** `trust::Tensor<Elem>` + `TensorHandle`; C ABI —
  `tensor_backend.hpp` (`trust_tensor_*`), реализация — плагин `tensor_cpu`; НИКАКИХ torch-заголовков.
  `TensorHandle` не хранит C++-тип: dtype/rank/shape — у бэкенда.
- **⚠ `TypeId`/`QualType` определены ОДИН раз — `runtime/trust/type_state.hpp`** (там же `Group`, `TypeKind`,
  `makeGroupTypeKind`); компилятор включает ЭТОТ же файл → layout не может разойтись;
  `types/type_id.hpp` — только слой компилятора. Идентичность — через `TypeId::operator==` (qualifier-биты
  сняты), в т.ч. в `Dict::operator==`.
- **⚠ Рантайм-форма identity:** продюсеры рантайма строят `TypeId::fromKind(...)` (index=0); компилятор на
  ABI-границе (`TypeNameResolver::toRuntimeType`) — то же для встроенных и их прозрачных алиасов, у номинальных
  типов `registry_index` сохраняется (слот реестра в рантайме не воспроизводим).

## Decisions

- **`runtime/torch/` — internal-подкаталог компилятора:** `TorchTensor` в пакет не входит, единственный
  потребитель — runtime-тесты.
- **Заголовки рантайм-МЕХАНИЗМОВ живут в `include/runtime/trust/`, публикуются как `trust/<h>.hpp`;**
  заголовки встроенных типов и тензора — рядом со своими `.src` (`stdlib/core/builtin/*.hpp`,
  `stdlib/tensor/*.hpp`), публикуются как `stdlib/builtin/<h>.hpp`/`stdlib/tensor.hpp`.
  **Типы-обёртки (`trusted-cpp`, `trusted-cpp-sync`, `resource`, `interrupts`) — в stdlib**
  (`include/stdlib/core/*.hpp`, `@stdlib/<h>.hpp`), здесь не находятся.
  `${PROJECT_INCLUDE_DIR}/runtime` добавляется ТОЛЬКО `trust-runtime` и runtime-тестам (переопределяемые
  заголовки не должны быть доступны компилятору как обычные файлы). FAULT/EXPECT компилятора используют
  `trust::utils::formatMessage`, а не `trust/assert.hpp`.

## Relations

- Заголовки библиотечных типов и зонтичный модуль — `stdlib/core`; встроенность/предобъявление — `types`;
  C++20-модуль компилятора — `trust`.
