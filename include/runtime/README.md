# Runtime - Среда выполнения

## Назначение

Аналог стандартной библиотеки C++ для TrustLang: минимальный набор средств, необходимый сгенерированному
C++ коду (арифметика произвольной точности, синхронизируемые переменные, форматный вывод, опциональные
тензоры). Лишние механизмы не добавляются.

## Библиотеки

Runtime собирается из одних и тех же исходников в двух формах:

- **`trust-runtime.so`** — динамическая (цель `trust_runtime`);
- **`trust-runtime.a`** — статическая (цель `trust_runtime_static`).

Состав: ошибки/завершение (`trust::formatMessage`, `trust::trust__abort__`), глобальные потоки
`trust::outs()`/`trust::errs()` и `trust::utils::backtrace_string()`, форматный вывод
`trust::trust__print__`. Арифметика произвольной точности (`Rational`/`BigInteger` на GMP) — header-only
(`stdlib/core/builtin/{rational,big_integer}.hpp`), в саму библиотеку не входит.

## Доставка заголовков

Рантайм-заголовки доставляются пакетом стандартной библиотеки `share/trust/stdlib.trust`: примитивы —
как `include/trust/*.hpp`, заголовки встроенных типов (`rational.hpp`, `big_integer.hpp`, `dict.hpp`,
`range.hpp`) — как `include/stdlib/builtin/*.hpp`. Тензорные заголовки доставляет провайдер-пакет
`tensor.trust`. При сборке сгенерированной программы pipeline распаковывает `stdlib.trust` в `<build>/stdlib`
и добавляет `-Istdlib -Istdlib/include` в `build.conf`.

## Поиск и режимы линковки

Путь к библиотеке не зашит в компилятор: она ищется динамически (рядом с бинарём `trust` и в `lib/`/`../lib/`,
затем `LD_LIBRARY_PATH`, затем CWD). Режим линковки — `--link-runtime static|shared` (по умолчанию
`static`). Runtime-backed типы (`Dict`, `Range`, тензоры, ссылочные обёртки) описаны в своих заголовках;
модель типов — `runtime/trust/type_state.hpp` (общий ABI-заголовок).

## Зависимости

`trust-runtime.so` — GMP. `stdlib/trusted-cpp.hpp` — самодостаточен (только std);
`stdlib/trusted-cpp-sync.hpp` подключает threading-заголовки; ошибки — стандартные C++-исключения.
