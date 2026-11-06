# MEMORY.md

> scope: include/stdlib/core
> role: persistent-memory
> last_reviewed: 2026-11-10
> review_period: 30

## Architecture

Стандартная библиотека — СУЩНОСТИ на механизмах языка: `.src` на TrustLang + C++-заголовки библиотечных
типов, единым пакетом `share/trust/stdlib.trust` (манифест `trust.toml`, `[[layout.layers]]`). Заголовки
встроенных типов, ещё не переведённых на генерацию, лежат рядом со своими `.src` (`builtin/*.hpp`);
СГЕНЕРИРОВАННЫЕ из `.src` реализации (`string.src`) публикуются заголовком своего модуля в build-каталоге.
Распаковка — `<build>/stdlib`, `build.conf` даёт `-Istdlib -Istdlib/include` (+`-Istdlib/include/stdlib`
для коротких include рантайма). Реализации
builtin и библиотечные классы грузятся ЯВНО: зонтичный `std.src` (`\std();`; интерфейс реэкспортирует
`iterator`/`async`, тело грузит `builtin/*.src`) композирует их явным импортом; `dsl.src` — отдельно
(`loadDslMacros`).

## Facts and invariants

- **Stdlib — НЕ категория типов:** реализации предобъявленных builtin (`stdlib/builtin/*.src`) используют `=`
  (НЕ создают тип; `::=` создаёт новый); критерий встроенности — `types`.
- **⚠ Реализации builtin и библиотечные классы подключаются ЯВНО:** `\std();` грузит `builtin/*.src` и
  реэкспортирует `iterator`/`async`; `--stdlib` = корень stdlib в путях ПОИСКА модулей (короткий spelling
  `\std();`, `\\builtin\string();`), `--no-stdlib` его убирает. Prelude-загрузки реализаций нет.
- **⚠ Префиксы НЕ смешиваются:** `@trust/…` — рантайм-примитивы; `@stdlib/…` — стандартная библиотека
  (заголовки builtin — `@stdlib/builtin/*.hpp`); оба — из `stdlib.trust` через `-Istdlib/include`.
- **Итераторы:** `Iterator<T>` ↔ `trust::Iterator<T>` (type-erased курсор; реэкспорт `\std();`); `Generator<T>` —
  корутинный источник. **⚠ `Generator<T>` — только `<coroutine>`** (`std::generator` нет у GCC 13). LSP публикует
  stdlib DSL как `<cppt-каталог>/stdlib/dsl.src`.
- **Async-слой (`trust::async`):** имена — через `\std();`; `#include`/`-pthread` ON-USE по `[[link.headers]]`.
- **⚠ `@[borrowed]` на КЛАССЕ наследуется производными типами** (`Iterator<T>`, `Box<Int32>`) — методы не помечать.
- **Заменяемые реализации:** дефолт — `\std();` (`builtin/rational.src`); альтернатива —
  `stdlib/builtin/rational_openssl.src` явным модулем ВМЕСТО `\std();` (привязка единственна). Повторное
  `=`-определение уже реализованного типа — диагностика `-Wbuiltin-override` (дефолт warning); первая
  реализация сохраняется.
- `:Tensor` — Core-тип: предобъявлен в `types`, реализация — провайдер `tensor.trust`.
- **Типы-обёртки `trusted-cpp`/`trusted-cpp-sync`/`resource`/`interrupts` живут ЗДЕСЬ**
  (`include/stdlib/core/*.hpp`), публикуются `@stdlib/<h>.hpp`.
- **⚠ Рантайм-типы реализуются корневыми модулями `mem`/`exceptions`:** `\mem();` —
  Access*/AccessShared/StaticUnique/FreeDeleter/FileDeleter; `\exceptions();` — StdException
  (std::exception) + IntAny/IntPlus/IntMinus (единая область `Group::kExceptions`; оба грузит
  `\std();`). Кодоген-инъектируемые типы (ref-виды, `try`) требуют их явной загрузки: гейт в
  семантике (ref — по RefType-битам TypeId, `try` — конструкт-чеком `TryCatchStmt`).
- **⚠ C++-шаблонный builtin с КОНКРЕТНЫМ аргументом:** `Complex32 = %std::complex<float> { };`
  (`builtin/complex.src`, грузит `\std();`) — RHS `%cpp<arg>` через `class_item: native LT template_params GT`;
  cppName НЕшаблонного trust-класса = базовое C++-имя + аргументы. Гейт: `\builtin\complex();`.
- **⚠ `Any` (`std::any`) материализуем и гейтится:** `builtin/any.src` (`Any = %std::any { };`), грузит
  `\std();`. Внутренний sentinel вывода — `Auto` (`Group::kAuto`), НЕ гейтится и до кодогена не доходит.


## Decisions

- Раскладку архива описывает сам манифест `trust.toml`; система сборки передаёт только каталог пакета
  и путь архива.

## Relations

- Встроенные типы, рантайм-заголовки, критерий встроенности — `runtime`, `types`.
- Общий механизм пакетов — `package`; C++20-модуль компилятора — `trust`.
