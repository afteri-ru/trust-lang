# MEMORY.md

> scope: include/stdlib/core/builtin
> role: persistent-memory
> last_reviewed: 2026-11-10
> review_period: 30

## Architecture

Реализации предобъявленных встроенных типов на самом TrustLang: `string.src`, `rational.src`
(дефолтная реализация `:Rational`/`:BigInteger`, GMP), `rational_openssl.src` (альтернативный
провайдер, OpenSSL) и его C++-заголовок `rational_openssl.hpp`, `dict.src` (`:Dict`), `array.src`
(`:Array`; заголовки `<vector>/<array>` через `array.hpp`), `range.src` (`:Range<Elem>`).
Default-реализации грузит зонтичный `\std();` (`stdlib/std.src`);
альтернативные (`rational_openssl.src`) подключаются явным модулем ВМЕСТО `\std();`, один провайдер на TypeId.
Привязки методов (`name := %member ...;`) для Array/Range тоже здесь: ядро объявляет только
интерфейс (без C++-имён).

## Facts and invariants

- **⚠ Встроенный тип — предобъявление + отдельная реализация:** здесь реализация оператором `=`
  (НЕ создаёт новый тип); `::=` создаёт новый тип и для встроенного имени запрещён.
- **⚠ Одна реализация на builtin-TypeId:** повторное `=` — диагностика `-Wbuiltin-override`
  (дефолт warning); первая реализация сохраняется. Зависимостей реализаций компилятор НЕ знает:
  если `.hpp` включает другой `.hpp`, это свойство заголовка, а нужность реализации типа
  определяется единым provider-gate по факту использования типа. `rational_openssl.src` НЕ грузится
  через `\std();` и подключается явным модулем вместо него.
- **⚠ Методы builtin ШИРЕ ядрового интерфейса объявляются в `=`-теле модуля.** Дескрипторы builtin
  иммутабельны (ядро `BuiltinTypeCore`), поэтому такие методы идут в per-instance overlay
  `MethodTable` (`addBuiltinExtraMethod`; поиск/дубли учитывают overlay, `reset()` чистит). Ядро
  хранит только минимально достаточный интерфейс (пример: `StrChar` — c_str/data/size/length/empty).
- **⚠ Строки ГЕНЕРИРУЮТСЯ из `string.src`** (внешнего `string.hpp` НЕТ): базис в СИНТАКСИСЕ
  (`StrChar = %trust::StrChar, ::, %std::string { ... }`; `::` — публичное наследование, дефолт
  генерируемого класса — private)
  задаёт базис, методы с телами (`chars()`/`toWide()`/`toNarrow()`) становятся членами класса, а
  привязки `%member` объявлений не эмитят (имя члена фиксировано базой). C++-имя остаётся
  `trust::StrChar`/`StrWide` (не `std::string`); UTF-8 и `std::formatter<trust::StrChar>` —
  рантайм-примитивы `@trust/string_support.hpp`. `swap` методом не объявлять (DSL-макрос
  `swap($a,$b)`); `StrWide.c_str/data` невыразимы (CString = `const char*`).



## Decisions

## Relations

- `types` (критерии встроенности), `include/stdlib/core/MEMORY.md` (доставка единым пакетом).
