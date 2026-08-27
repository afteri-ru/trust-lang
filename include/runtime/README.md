# Runtime - Среда выполнения

## Назначение

Runtime - это аналог стандартной библиотеки C++ для TrustLang: минимальный набор
средств, необходимый сгенерированному C++ коду. Включает только то, что реально
используется (арифметика произвольной точности, синхронизируемые переменные,
именованные аргументы, опциональная интеграция с тензорами). Лишние механизмы в
runtime не добавляются.

## Библиотеки runtime: `trust-runtime.so` и `trust-runtime.a`

Runtime собирается двумя способами из одних и тех же исходников
(`src/runtime/rational.cpp`, `src/runtime/trust_headers.cpp`,
`src/utils/io.cpp`, `src/utils/backtrace.cpp`):

- **`trust-runtime.so`** - динамическая библиотека (цель CMake `trust_runtime`);
- **`trust-runtime.a`** - статическая библиотека (цель CMake `trust_runtime_static`).

Сейчас в них входит:

- `Rational` - произвольная точность на основе GMP (`src/runtime/rational.cpp`,
  публичный заголовок `include/trust/rational.hpp`).
- ошибки/завершение: `trust::formatMessage` и `trust::trust__abort__`
  (публичный заголовок `include/trust/assert.hpp`), а также глобальные потоки
  `trust::outs()`/`trust::errs()` и `trust::utils::backtrace_string()`
  (`src/utils/io.cpp`, `src/utils/backtrace.cpp`) - они доступны автономным
  сгенерированным программам.
- форматный вывод: `trust::trust__print__` (публичный заголовок
  `include/trust/io.hpp`) - `fmt`-подобная функция (стиль `std::format`),
  пишущая результат в `trust::outs()`; бэкенд DSL-макроса `print(fmt, args...)`.
  `trust::Rational` форматируется как символьная строка `num\den`
  (специализация `std::formatter<trust::Rational>` в `trust/rational.hpp`).

### Встраивание публичных заголовков (#embed)

Публичные рантайм-заголовки (например `trust/rational.hpp`, `trust/assert.hpp`)
встраиваются в объектный код библиотеки через `#embed` в ELF-секцию, названную
путём заголовка (`src/runtime/trust_headers.cpp`). Когда сгенерированная программа
использует такой тип/символ, pipeline:

1. находит рантайм-библиотеку в соответствии с режимом линковки `--link-runtime`
   (см. ниже): `trust-runtime.so` либо `trust-runtime.a`;
2. извлекает секцию по пути заголовка во временный каталог `<build_dir>/trust/`
   (`utils::readSectionFromLibrary`: `.so` читается как ELF напрямую, `.a` - как
   ar-архив, секция ищется в ELF-членах);
3. добавляет в `build.conf` include-путь и линковку рантайм-библиотеки + GMP.

Это позволяет распространять рантайм-библиотеку без дерева исходников компилятора.

## Поиск рантайм-библиотеки

Путь к библиотеке **не зашивается в компилятор** (нет жёсткой привязки к каталогу
сборки). Библиотека ищется динамически при каждом запуске в порядке приоритета
(имя файла зависит от режима линковки: `trust-runtime.so` либо `trust-runtime.a`):

1. рядом с исполняемым файлом `trust` (каталог бинарё, включая разворачивание
   симлинков через `/proc/self/exe`), а также в `lib/` и `../lib/` относительно него;
2. в каталогах из переменной окружения `LD_LIBRARY_PATH`;
3. в текущем рабочем каталоге.

## Режимы линковки сгенерированной программы

Режим задаётся опцией `--link-runtime static|shared` (по умолчанию **`static`**).
В `build.conf` рантайм-зависимости кладутся в переменную `LIBS`, подставляемую в
Makefile после объектных файлов.

- **`static`** (по умолчанию): в `LIBS` передаётся путь к `trust-runtime.a` и `-lgmp`.
  Код рантайма встраивается в исполняемый файл на этапе линковки, поэтому готовая
  программа **самодостаточна** и не требует `trust-runtime.so` во время выполнения
  (нет `DT_NEEDED` на `.so`, нет `DT_RUNPATH`).
- **`shared`**: в `LDFLAGS` добавляется `-Wl,-rpath,.`, в `LIBS` -
  `-Wl,--no-as-needed <path>/trust-runtime.so -lgmp`. Путь к `.so` используется для
  резолва символов и записи `DT_NEEDED`, но каталог сборки **не запекается** в
  бинарник (без абсолютного `-Wl,-rpath`). Во время выполнения готовой программы
  динамический загрузчик ищет `trust-runtime.so` в порядке приоритета:

  1. каталоги из `LD_LIBRARY_PATH` (приоритет выше `DT_RUNPATH`);
  2. каталог запуска - в бинарник записан `DT_RUNPATH = .`;
  3. кэш `ld.so` и системные каталоги по умолчанию.

## Компоненты (header-only)

- `trusted-cpp.hpp` - синхронизируемые разделяемые переменные: `Sync`/`SyncSingleThread`/
  `SyncTimedMutex`/`SyncTimedShared`, RAII-блокировка `Locker`, контейнер `Shared` и
  слабые ссылки `Weak`.
- `trust/dict.hpp` - универсальный гетерогенный словарь `trust::Dict` (элементы
  `(имя, TypedValue{kind, std::any})` - тип элемента закодирован в `kind` (TypeKind:
  группа+размерность), декодируется методами `TypedValue` (`group()`/`data()`/предикаты
  `isNumeric()`/`isString()`/…) **автономно, без TypeRegistry**; значение хранится точным
  C++-типом; доступ по имени/индексу, конвертеры). Встраивается в trust-runtime (секция
  `trust/dict.hpp`) и извлекается pipeline при использовании типа `:Dict`.
- `trust/range.hpp` - универсальный арифметический диапазон `trust::Range<T>` - шаблон по
  элементному типу `T` (интегральные, float, `trust::Rational`, универсальный `std::any` для
  `Any`), инклюзивная семантика `start..stop[..step]` (Kotlin-модель `a..b`). Ленивый: свойства
  `start()/stop()/step()`, `count()/size()`, `empty()`, `contains()`, `at(i)/operator[]`
  (формула `start+i*step`, без материализации), forward-итератор `begin()/end()`, `reversed()`,
  `==`; материализация - `toVector()/toArray()/toList()/toDict()` (в `trust::Dict`),
  `std::formatter`. Ошибки - стандартные исключения (`std::out_of_range`,
  `std::invalid_argument` при `step==0`). Компилятор строит параметризованный тип `Range<Elem>`
  (elementType - join типов операндов) и эмитит `trust::Range<Elem>`. Методы доступны в
  доверенном коде как `r.count()/r.size()/r.length()(алиас→count)/r.empty()/r.at(i)/r.toVector()/
  r.toDict()/...`; сигнатуры элемент-зависимых методов (`at/start/stop/step/contains`) объявлены
  с типовым параметром `T` и подставляются (T→Elem) при резолве - точные типы, а не Any
  (регистрация на `:Range` через `TypeRegistry::addMethod` полным ключом с нативностью `%` и
  константностью `^`, напр. `"%count^"`; алиас `length` → нативное `count` - опциональный
  параметр `aliases` у `addMethod`, с проверкой консистентности). Формы вызова: `r.size()`/`r.%size()`/`r.size^()` - обычный,
  нативный (`%`) и const-вызов (`^` → `const_cast<const T&>(r).method(...)` - гарантированно
  константная перегрузка).
  Встраивается в trust-runtime (секция
  `trust/range.hpp`) и извлекается pipeline при использовании литерала диапазона `..`.
- `trust/any_convert.hpp` - типизированная конверсия из элемента словаря
  `trust::any_to<T>(TypedValue)`: диспетчеризация по `kind` (TypeKind: числовое/строковое,
  декодируется `TypedValue`), конверсия с контролем диапазона (`checked_cast`) и диагностикой.
- `module_api.h` - ABI-структуры экспорта модулей (`__trust_exports`,
  `__trust_get_exports`).
- `tensor.hpp` - обёртка над тензорами LibTorch (`TorchTensor`); собирается как
  отдельный плагин `tensor_cpu` только при `WITH_TORCH`.

## Зависимости

- `trust-runtime.so` - GMP.
- `trusted-cpp.hpp` использует механизм ошибок `FAULT`/`EXPECT`, объявленный в
  `diag/error.hpp`. `trust/dict.hpp` - самодостаточен (только std-заголовки) и
  сообщает об ошибках стандартными исключениями (`std::out_of_range`/`std::bad_any_cast`).
