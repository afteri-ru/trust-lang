# MEMORY.md

> scope: include/trust
> role: persistent-memory
> last_reviewed: 2026-08-31
> review_period: 30
> max_size: 4096

## Architecture

## Facts and invariants

- **`trust/trusted-cpp.hpp` (plain, ВСЕГДА при ссылке)** - ссылочные типы БЕЗ межпотоковой синхронизации:
  `Shared<V>` (лёгкий, `shared_ptr<V>`, прямой доступ `*`/`->`/`get()`), `Weak<T>`, единый RAII-охранник
  `Locker<V,ReadOnly>` (результат `lock()/lock_const()`; shared-захват, НЕ опустошает источник, throw
  `IntMinus` на nullptr). Самодостаточен (std-заголовки, БЕЗ `<thread>/<mutex>/<shared_mutex>/<chrono>`).
- **`trust/trusted-cpp-sync.hpp` (только при межпотоковой синхронизации)** - `SyncShared<V,Policy>`
  (policy-параметр, НЕ mutex-тип), доступ ТОЛЬКО через `lock()/lock_const()`. Встроенные политики
  (`trust::SyncMutexPolicy` - эксклюзив, timed_mutex; `trust::SyncRwMutexPolicy` - read/write,
  shared_timed_mutex; `trust::SyncSingleThreadPolicy` - объект только в потоке создания). Все три
  регистрируются в TypeRegistry как типы группы `Group::kSyncPolicy` (cppName `trust::Sync*Policy`,
  инклуд `@trust/trusted-cpp-sync.hpp`, признак kHasAttrsFlag). Встраивается в trust-runtime (#embed,
  секция `trust/trusted-cpp-sync.hpp`).
- **Детектор взаимной блокировки встроен во ВСЕ политики; таймаут — ОДНО значение `syncDeadlockTimeout()`**
  (дефолт — константа `SyncTimeoutDeadlock` = 5s, ЕДИНЫЙ источник и для рантайма, и для опции) с семантикой
  0/−1/+: `>0` — ждать до таймаута (истёк → `trust::IntMinus`, дидлок); `0` — без ожидания (try-once);
  `<0` (`SyncTimeoutBlockForever`) — ждать до конца (детектор выкл, классический mutex). Булева флага
  «включён» НЕТ. Перекрывается: compile-time `-fsync-deadlock=<ms|s|nano>` (встраивается в main через
  `setSyncDeadlockFromString`) и на старте программы системной опцией
  `--trust:fsync-deadlock=...`/`--trust:fno-sync-deadlock` (→ `SyncTimeoutBlockForever`) через
  `applySystemEnv` (`ParsedArgs::env` префикса `--trust:`). Пер-объектный таймаут - третий аргумент
  `@[reftype("shared"/"weak", <policy>, <timeout>)@]` (конструктор SyncShared; строка парсится
  `trust::runtime::syncTimeoutFromString`, ЕДИНЫЙ источник).
- **⚠ trap: Locker POLICY-AGNOSTIC** - НЕ знает и не должен знать способ синхронизации (mutex/таймаут);
  политика - во владеющем типе (`SyncShared`); Locker лишь охраняет доступ (const-correctness) и держит
  непрозрачный release-хендл. Locker ТОЛЬКО для Shared/Weak; сырые ссылки (`&`/`*`) и `unique_ptr`
  локера НЕ имеют (другая идеология - прямой доступ без guard). Null-безопасность - отдельная ось
  (контракт типа), а не guard-объект.
- **⚠ trap: ссылочность многоуровневая, НО пересечение уровней в ОДНОМ операторе запрещено**
  (каждый уровень - отдельный тип/операция; цепочечного `**`/`ref.lock().lock()` нет).
- Кодоген: `kShared`→`trust::Shared<T>`, `kWeak`→`trust::Weak<trust::Shared<T>>`,
  `kLocker`→`trust::Locker<T>` (getCppTypeName); `*ref`→`*(ref.lock())`, `*^ref`→`*(ref.lock_const())`;
  транспилятор записывает `@trust/trusted-cpp.hpp` при этих видах/операторе take.

## Decisions

## Relations
