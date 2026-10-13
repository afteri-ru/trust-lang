# MEMORY.md

> scope: include/stdlib/core/async
> role: persistent-memory
> last_reviewed: 2026-11-05
> review_period: 30
> max_size: 3000

## Architecture

Async-слой стандартной библиотеки (header-only, C++): `generator.hpp` (`trust::Generator<T>`),
`mutex.hpp`, `task.hpp`, `thread_pool.hpp` (`trust::async`). Часть пакета `share/trust/stdlib.trust`
(третий слой — заголовки из `include/runtime/trust`; этот каталог — stdlib-слой).

## Facts and invariants

- **⚠ `Generator<T>` реализован на `<coroutine>`:** `std::generator` недоступен в GCC 13
  (дефолтный `c++` для сгенерированных программ), поэтому используется собственная реализация.
- **⚠ `-pthread` и `#include` добавляются ON-USE** по `[[link.headers]]`; типы async-слоя доступны
  без импорта, но линковка без pthread не слинкуется.

## Decisions

## Relations

- `include/stdlib/core/MEMORY.md` (общий контракт stdlib), `runtime/trust` (ABI-заголовки).
