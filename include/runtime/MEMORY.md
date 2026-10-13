# MEMORY.md

> scope: include/runtime
> role: persistent-memory
> last_reviewed: 2026-11-02
> review_period: 30
> max_size: 9200

## Architecture

Рантайм-заголовки ВСТРОЕННЫХ типов входят в пакет `stdlib.trust` как `include/trust/*.hpp` (единый способ
доставки), публикуются под префиксом `@trust/…`, версионируются вместе с компилятором и `--no-stdlib`
их НЕ отключает (в отличие от `@stdlib/…`). `trust-runtime` несёт только скомпилированный код.
Runtime-заголовки — лишь один из провайдеров реализации встроенного типа (Int*/Bool — intrinsic,
StrChar — C++ std, Rational/BigInteger — GMP). Детали и полный список — `runtime/README.md`.

## Facts and invariants

- **⚠ trap: `unique` монополен, guard'а нет.** Без deleter'а — `trust::StaticUnique<T>` (zero-cost,
  move-only); с `@[deleter(D)]` — `trust::Unique<T,D>`. Заёма/alias НЕТ.
- **⚠ `trusted-cpp.hpp` (ВСЕГДА при ссылке) не включает `<thread>/<mutex>/<chrono>`;** межпотоковая
  синхронизация — ОТДЕЛЬНО в `trusted-cpp-sync.hpp` (`AccessShared<V,Mutex>`), доступ ТОЛЬКО через
  `lock()/lock_const()`. Политики — только `shared`/`weak`; пользовательских политик нет.
- **⚠ trap: guard — `detail::GuardBase` (CRTP), non-copyable/movable;** `Locker<V,RO>` — shared-capture
  наблюдатель, POLICY-AGNOSTIC. Сырые ссылки (`&`/`*`) и `unique` guard'а НЕ имеют.
- **⚠ trap: observer-заём — ТОЛЬКО у `Shared`:** `borrow()/borrow_const()` → `SharedBorrowedRef<V,RO>`
  (над weak_ptr): после гибели владельца `valid()==false`/`access()` бросает.
- **⚠ trap: три разных «borrow» не смешивать:** `Shared::borrow()` — RUNTIME-наблюдатель;
  `@[borrowed]`/`-Wborrow-*` — статическая проверка ИНВАЛИДАЦИИ производных представлений при мутации
  источника (не заём Rust): `include/semantic/ref/BORROW_CHECKER.md`.
- **Конверсии владения:** `Unique→Shared` = `to_shared()`; `Shared→Unique` = `to_unique()`
  (`use_count()==1`; перенос ЗНАЧЕНИЯ → смена идентичности); `borrow` требует `use_count()==1`.
- **⚠ trap: таймаут детектора дедлока — ОДНО значение `syncDeadlockTimeout()`** (дефолт 5s; `>0`
  ждать/истёк→IntMinus, `0` try-once, `<0` блокировать; булева флага НЕТ). Перекрытие:
  `-fsync-deadlock=`, `--trust:f(sync|no-sync)-deadlock`, конструктор `AccessShared(value, timeout)`
  (в `@[reftype]` таймаут НЕ задаётся).
- **⚠ trap: многоуровневая ссылочность — валидный ТИП, но пересечение уровней в ОДНОМ операторе
  запрещено** (цепочечного `ref.lock().lock()` нет).
- **Прерывания:** `IntAny` + `IntPlus` (control flow) + `IntMinus`. **`IntMinus` наследует
  `std::runtime_error`** — ловится и trust-блоком, и C++; `IntPlus` НЕ `std::exception`. НЕИМЕНОВАННОЕ
  `++ v ++` всегда бросает; ИМЕНОВАННОЕ — return (валидирует semantic).
- **Защита стека:** `stack_overflow : public IntMinus`, несёт size/info/frame; резерв `reserve`
  (default 8192) добавляется ВСЕГДА: `check_overflow(N)` / `check_stack_limit()` сравнивают `free` с
  `N/reserve` (`m_stack_limit` из `.stack_sizes`, кеш).
- **Рантайм-парсер CLI:** `_main.cppt` отделяет аргументы среды по префиксу `--trust:`; из остальных
  строит ДВА `trust::Dict`: `argv` (позиционные) и `args` (`--key=value`/`--flag`→"true"); `--` —
  разделитель; `TRUST_*` рантайм читает сам через `getenv()`.
- **⚠ Move-only `Unique`:** копирование эксклюзивного владения диагностируется СЕМАНТИКОЙ (не
  C++-ошибкой); рантайм-заголовки ссылок выбирает единая `refTypeRuntimeIncludes`.
- **`@[include("@trust/<h>")]` на нативном классе** → заголовок подключается on-use; для библиотечных
  типов — `@stdlib/<h>`. `EnableSharedFromThis<V>` — миксин самоадресации; `shared_from_this()` →
  IntMinus, если не во владении/истёк; копия ссылку НЕ наследует.
- **GMP — внешняя зависимость:** её отсутствие обязано давать диагностику, а не ошибку линковки.
- **⚠ `trust/tensor.hpp` — self-contained value type:** `trust::Tensor<Elem>` + `TensorHandle`; C ABI —
  `tensor_backend.hpp` (`trust_tensor_*`), реализация — плагин `tensor_cpu`; НИКАКИХ torch-заголовков.
  `TensorHandle` не хранит C++-тип: dtype/rank/shape спрашиваются у бэкенда.
- **⚠ `TypeId`/`QualType` определены ОДИН раз — в `runtime/trust/type_state.hpp`** (там же `Group`,
  раскладка `TypeKind` и `makeGroupTypeKind` — единственный кодировщик Group/Data). Компилятор включает
  этот же файл как `runtime/trust/type_state.hpp`, поэтому layout не может разойтись;
  `types/type_id.hpp` — только слой компилятора. Идентичность сравнивается через `TypeId::operator==`
  (qualifier-биты сняты), в т.ч. в `Dict::operator==`.
- **⚠ Рантайм-форма identity:** продюсеры рантайма строят `TypeId::fromKind(...)` (index = 0);
  компилятор на ABI-границе (`TypeNameResolver::toRuntimeType`) делает то же для встроенных типов и их
  прозрачных алиасов, у номинальных пользовательских типов `registry_index` сохраняется. Причина: слот
  реестра — порядок регистрации в конкретном реестре и в рантайме не воспроизводим.

## Decisions

- **`runtime/torch/` — internal-подкаталог компилятора:** `TorchTensor` (torch-адаптер) в пакет не входит,
  другими компонентами не включается; единственный потребитель — runtime-тесты.
- **Заголовки встроенных типов живут в `include/runtime/trust/`, но публикуются как `trust/<h>.hpp`.**
  `${PROJECT_INCLUDE_DIR}/runtime` добавляется ТОЛЬКО целям, которым нужна реализация по умолчанию
  (`trust-runtime`) и runtime-тестам: переопределяемые заголовки не должны быть доступны компилятору как
  обычные файлы. FAULT/EXPECT компилятора используют `trust::utils::formatMessage`, а не `trust/assert.hpp`.

## Relations

- Заголовки БИБЛИОТЕЧНЫХ типов и prelude — `stdlib/core`; встроенность/предобъявление — `types`;
  C++20-модуль компилятора — `trust`.
