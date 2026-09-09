# MEMORY.md

> scope: include/runtime
> role: persistent-memory
> last_reviewed: 2026-09-16
> review_period: 30
> max_size: 24000

## Architecture

Рантайм-заголовки ВСТРОЕННЫХ типов (`include/runtime/trust/*.hpp`) вшиты `#embed` в ELF-секции
внутри `trust-runtime.so/.a`; пайплайн извлекает заголовок во временный каталог `trust/` ТОЛЬКО при
использовании типа. Публикуются под префиксом `@trust/…`; версионируются вместе с компилятором;
`--no-stdlib` их НЕ отключает (в отличие от `include/stdlib/`, префикс `@stdlib/…`).

## Критерии отнесения типа к встроенным (scope-инвариант)

Тип встроен ⟺ выполняется ЛЮБОЕ:
1. **Цель синтаксиса:** порождается литералом/оператором/формой объявления (`'...'`→StrChar,
   `42`→Int*, `m\n`→Rational, `(…)`→Dictionary, `[…]`→Array).
2. **Точка встречи конверсии:** соглашение между несвязанными сторонами (`:Dictionary` +
   `trust::any_to`); целевой тип конверсии может ещё не существовать.
3. **Специальная кодогенерация:** представление в C++ невыводимо из существующего кода (рефлексии нет
   → агрегат эмитится: Enum/Variant/Tuple/Struct).
4. **Анализаторные правила:** доступ к членам (имя→индекс/статика/тег), статические проверки размера
   агрегата, требование сравнимости типа значений, инварианты владения/заёма.
5. **Обязан работать без stdlib:** если при `--no-stdlib` сборка/кодоген ломается — встроенный; если
   нет — библиотечный.

Обратный критерий (что можно вынести в stdlib) — `include/stdlib/MEMORY.md`.

## Facts and invariants

- **⚠ trap: `unique` монополен, guard'а нет.** Без deleter'а — `trust::StaticUnique<T>` (inline,
  move-only, zero-cost); с `@[deleter(D)]` — `trust::Unique<T,D>`. Доступ `*u`/`u.get()`; заёма/alias
  НЕТ.
- **`trust/trusted-cpp.hpp` (plain, ВСЕГДА при ссылке; БЕЗ `<thread>/<mutex>/<shared_mutex>/<chrono>`)**
  даёт Shared/Weak/Locker + `Locker<V,ReadOnly>`; зависит от `trust/interrupt.hpp`. Межпотоковая
  синхронизация — ОТДЕЛЬНО в `trust/trusted-cpp-sync.hpp` (`AccessShared<V,Mutex>`), доступ ТОЛЬКО
  через `lock()/lock_const()`. Политики — ТОЛЬКО `shared`/`weak` (`AccessMutex`/`AccessRwMutex`/
  `AccessSingleThread`); `unique` монополен — политика ФИКСИРОВАНА (эксклюзивный доступ); пользовательских политик/заёма/alias нет, только move/swap.
- **⚠ trap: guard — `detail::GuardBase` (CRTP), non-copyable/movable.** `Locker<V,RO>` — shared-capture
  (наблюдатель; источник не опустошает; для sync держит release-хендл); POLICY-AGNOSTIC. Сырые ссылки
  (`&`/`*`) и `unique` guard'а НЕ имеют.
- **⚠ trap: observer-заём — ТОЛЬКО у `Shared`.** `borrow()/borrow_const()` → `SharedBorrowedRef<V,RO>`
  (над weak_ptr; copyable). НЕ владеет; доступ `auto g = ref.access();` с re-check; после гибели
  владельца `valid()==false`/`access()` бросает. `unique` observer-заёма не имеет.
- **Конверсии владения:** `Unique→Shared` = `to_shared()`/`Shared(Unique&&)`; `Shared→Unique` =
  `to_unique()` (`use_count()==1`; перенос ЗНАЧЕНИЯ → смена идентичности; → `StaticUnique<V>`). `borrow`
  требует `use_count()==1`.
- **⚠ trap: таймаут детектора дедлока — ОДНО значение `syncDeadlockTimeout()`** (дефолт 5s; `>0`
  ждать/истёк→IntMinus, `0` try-once, `<0` блокировать; булева флага НЕТ). Перекрытие:
  `-fsync-deadlock=`, `--trust:f(sync|no-sync)-deadlock`, конструктор `AccessShared(value, timeout)`
  (в `@[reftype]` таймаут НЕ задаётся).
- **⚠ trap: многоуровневая ссылочность — валидный ТИП, но пересечение уровней в ОДНОМ операторе
  запрещено** (цепочечного `**`/`ref.lock().lock()` нет).
- **Прерывания (`trust/interrupt.hpp`):** `IntAny` (база) + `IntPlus` (положительное/сквозной возврат)
  + `IntMinus` (отрицательное/ошибка). **`IntMinus` наследует `std::runtime_error`** — ловится и
  trust-блоком `{- -}`, и C++; `IntPlus` НЕ `std::exception` (control flow). НЕИМЕНОВАННОЕ `++ v ++`/
  `-- e --` всегда бросает; ИМЕНОВАННОЕ `func ++ v ++` — return (валидирует semantic).
- **Защита стека (`trust/stack_check.hpp`):** **`stack_overflow : public IntMinus`** — встроенная
  ошибка, ловится trust-блоком и C++; несёт size/info/frame. `info` — header-only
  `inline thread_local` статика, определяется в `_main.cppt`. Резерв `reserve` (default 8192) ВСЕГДА
  добавляется: `check_overflow(N)` → throw если `free < N + reserve`; `check_stack_limit()` →
  `free < m_stack_limit + reserve` (`m_stack_limit` из `.stack_sizes`, кеш).
- **`trust/resource.hpp` (deleter'ы ресурсов):** встроенные deleter-функторы (`FreeDeleter`,
  `FileDeleter`); используются языковым `@[deleter(D)]` (D → тип `Group::kDeleterPolicy`).
- **Рантайм-парсер CLI (`trust/args.hpp`):** `_main.cppt` отделяет аргументы среды по префиксу
  `--trust:` (в `ParsedArgs::env`), из остальных строит ДВА `trust::Dict`: `argv` (позиционные, пустое
  имя) и `args` (`--key=value`/`--flag`→"true"). `--` — разделитель. argv/args — ПО ЗНАЧЕНИЮ.
  `TRUST_*` рантайм читает сам через `getenv()`.
- **Move-only `Unique` + выбор заголовков:** `trust::Unique` non-copyable; копирование эксклюзивного
  владения диагностируется СЕМАНТИКОЙ (не C++-ошибкой). Рантайм-заголовки ссылок выбирает единая
  `refTypeRuntimeIncludes`.
- **`@[include("@trust/<h>")]` на нативном классе** → рантайм-заголовок подключается on-use (как
  `registerBuiltinType`); для библиотечных типов — `@stdlib/<h>`. `EnableSharedFromThis<V>`: миксин
  самоадресации; `shared_from_this()` → IntMinus если не во владении/истёк; `weak_from_this()` →
  `Weak<Shared<V>>`; копия ссылку НЕ наследует.
- **Кодоген:** `kShared`/`kWeak`/`kLocker` → `trust::Shared`/`Weak`/`Locker<T>`; `kUnique` →
  `StaticUnique<T>` (`*u`→`(u.get())`), +deleter → `Unique<T,D>` (`*u`→`checked_deref(u.get())`);
  `*ref`→`*(ref.lock())`, `*^ref`→`*(ref.lock_const())`.
- **Концепты:** `AccessGuard` (Locker), `UniqueRef` (get/operator*/bool/reset), `SharedRef` (копируемый +
  weak/lock), `BorrowableRef` (borrow; только `Shared`).
- **GMP — внешняя зависимость** (`rational.hpp`/`big_integer.hpp`): её отсутствие обязано давать
  диагностику, а не ошибку линковки.

- **⚠ `TypedValue` хранит ПОЛНЫЙ `TypeId` (64 бита):** старшие 32 — `TypeKind`, младшие — `registry_index` +
  пер-Symbol флаги (`kSymbolFlagsMask`). Заголовок самодостаточен: редекларирует `using TypeId=uint64_t`,
  `makeTypeId/kindOf/structuralTypeId/typeIdSame`. Идентичность сравнивается СТРУКТУРНО (`typeIdSame`,
  флаги сняты) — в т.ч. `Dict::operator==`. Группа/размерность декодируются из `kindOf(typeId)`.

## Decisions

- **Заголовки встроенных типов живут в `include/runtime/trust/`, но публикуются как `trust/<h>.hpp`.**
  Поэтому `${PROJECT_INCLUDE_DIR}/runtime` добавлен во все include-пути, компилирующие код, включающий
  `trust/<h>.hpp` (компилятор, тесты, рантайм-таргеты, PCM-модули).

## Relations

- Заголовки БИБЛИОТЕЧНЫХ типов и prelude (`.src`) — `include/stdlib/MEMORY.md`.
- Инфраструктура C++20-модуля компилятора — `include/trust/MEMORY.md`.
