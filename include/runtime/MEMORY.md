# MEMORY.md

> scope: include/runtime
> role: persistent-memory
> last_reviewed: 2026-08-28
> review_period: 30
> max_size: 4600

## Architecture

## Facts and invariants

- **`trust/trusted-cpp.hpp` (plain, всегда):** Shared/Weak/Locker + `Locker<V,ReadOnly>` (compile-time
  const); зависит от `trust/interrupt.hpp`; #embed; kShared→`trust::Shared`. МЕЖПОТОКОВАЯ синхронизация -
  ОТДЕЛЬНО: `trust/trusted-cpp-sync.hpp` (`SyncShared<V,Mutex>`), #embed `trust/trusted-cpp-sync.hpp`.
- **Прерывания (`trust/interrupt.hpp`):** `IntAny` (база, `{* *}`/dynamic_cast) + `IntPlus` (положительное/
  сквозной возврат) + `IntMinus` (отрицательное/ошибка). **`IntMinus` наследует `std::runtime_error`** —
  ловится и trust-блоком `{- -}`, и C++ (`catch(std::runtime_error&)`); `IntPlus` НЕ `std::exception`
  (control flow). НЕИМЕНОВАННОЕ `++ v ++`/`-- e --` всегда бросает (независимо от обработчиков);
  ИМЕНОВАННОЕ `func ++ v ++` — return (валидирует semantic).
- **Рантайм-парсер CLI (`trust/args.hpp`):** `_main.cppt` отделяет аргументы среды по префиксу `--trust:`
  (в `ParsedArgs::env`), из остальных строит ДВА `trust::Dict`: `argv` (позиционные, пустое имя) и `args`
  (`--key=value`/`--flag`→"true"). `--` — разделитель (всё после — позиционное). argv/args — ПО ЗНАЧЕНИЮ
  (локальная копия). `TRUST_*` рантайм читает сам через getenv(), в сигнатуру не передаются.
- **Защита стека (`trust/stack_check.hpp`):** **`stack_overflow : public IntMinus`** — встроенная ошибка,
  ловится trust-блоком и C++ (`catch(std::runtime_error&)`); несёт size/info/frame. `info` — header-only
  `inline thread_local` статика, определяется в `_main.cppt` (единственная TU). Резерв `reserve` (default
  8192) ВСЕГДА добавляется: `check_overflow(N)` → throw если `free < N + reserve`; `check_stack_limit()`
  → `free < m_stack_limit + reserve` (`m_stack_limit` из `.stack_sizes`, кеш); `check_reserve()` →
  `free < reserve` **abort** (нельзя создать исключение). Состояние (`reserve`, кеш) — thread_local
  (по-потоково); максимум `.stack_sizes` кешируется глобально. В конструкторе проверки НЕ исполняются.
  Требует `-fstack-size-section` и `-lpthread`; pipeline при использовании дополнительно извлекает
  `trust/interrupt.hpp` (зависимость заголовка).
