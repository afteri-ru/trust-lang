# Version history

## [Unreleased]
- **Унификация документирующих комментариев для LSP:** доки (`///`,`##`,`/**`, т.ч. хвостовой
  `///<`/`##<`) перед/после объявлений привязываются ГРАММАТИКОЙ к терму-идентификатора
  (`Term::m_docs`, `attachLeadingDoc`/`attachTrailingDoc`) и переносятся в `AstNodeBase::documentation`
  (`TermToAstConverter::convert`), откуда их читает `SymbolCollectorHook` (hover/док). Трейлинг-док
  больше НЕ цепляется к следующему объявлению. Доки перед не-объявлениями остаются отдельными
  `Document`-узлами; транспилер выводит `documentation` (с нормализацией `##`→`///`). Доки макросов
  захватываются в определение макроса (`recordMacro`: ведущий + хвостовой inline). **LSP hover** выводит
  документирующий комментарий в начало содержимого. Удалены `moduleDocMap`/`attachDocumentation` из
  pipeline; макро-символы унифицированы в `appendMacroSymbols`.
- **Обработка ошибок в trust-lsp:** `publishDiagnostics` публикует только диагностики текущего
  trust-файла (фильтр по `trustReaderIdx`), а не импортированных модулей; при падении `runPipeline`
  публикуется диагностика `internal analysis error` с диапазоном (вместо тихого глотания), а символы
  дособираются из `Context::macroDefs()` (имя макроса не теряется при Fatal до семантического шага).
- **Семантика `-Wcomments`/`-Wno-comments` исправлена:** флаг «comments» включён (`-Wcomments`, по
  умолчанию) = комментарии ВЫВОДЯТСЯ в C++-коде; `-Wno-comments` (флаг выключен) = подавляются.
  Раньше подавление включалось флагом наоборот (код и документация противоречили друг другу).
  `FlagKind::Comments` по умолчанию включён в `Context`; `isSuppressedDoc`/`emitDocumentation`
  подавляют при ВЫКЛЮЧЕННОМ флаге. Тест `doc_comments_suppress` усилен (ведущий `CHECK-NOT`).
- **Деструктуризация `t1, ..., tN := [... ]source;` + истинность Dict в `@while`:** узел `DestructureDecl`.
  **Точная привязка по умолчанию** (Python/Rust): без маркера каждая цель — один элемент; число целей ==
  числу элементов для статически-известного размера (ошибка арности). Суффикс `...` у цели (`rest...`,
  C++-pack) — «остаток» (словарь: остаток Dict; кортеж: под-кортеж через `std::make_tuple`), `_...` —
  извлечь элементы, остаток отбросить, `_` — пропустить один. Проверка типа источника спреда (только
  Dict) и одиночной цели (`x := ... d`) — диагностики вместо тихого C++-сбоя. Источник-выражение
  оценивается один раз (temp). **Per-element типизация целей** (как кортеж): каждый элемент словаря
  типизируется своим runtime-типом (`int64_t c_ti = std::any_cast<int64_t>(...)`, Bool/Double/StrChar
  аналогично; Dict хранит int как int64_t). **В цикле** тип расширяется до МАКСИМАЛЬНОГО среди элементов
  (Bool/Int8 → Integer, float → Double) и присваивается runtime-конвертерами (`anyToInt64/anyToDouble`);
  невыводимый тип → `Any` (предупреждение `-Wwiden-any`, только в циклах). Циклы создают скоупы
  (локальность переменных). Унифицирован хелпер sigil-нормализации (`normalizeLocalSigil`).
  Исправлена грамматика `assign_items` (FinalizeAndTest сплющивал список имён, ломая деструктуризацию 3+).
- **Деструктуризация-присваивание и аннотации типов целей:** `t1, ..., tN = [... ]source;` — цели
  резолвятся как уже существующие переменные (объявление не создаётся; несуществующая цель / цель-константа
  `^` — диагностика), раскрытие — `std::get<N>` (кортеж) / `pop_front` + `any_cast<T>` (словарь). Явная
  аннотация цели `a:Type` фиксирует тип ДЕКЛАРИРУЕМОЙ переменной (кортеж: `int32_t c_a` вместо `auto`;
  словарь: переменная `int32_t`, элемент извлекается по runtime-типу хранения `std::any_cast<int64_t>`,
  т.к. Dict хранит целые как int64_t); без аннотации тип выводится как раньше. Новый
  `DestructureDecl::m_isAssign`, `m_targetTypeNodes`/`m_targetDeclaredTypes`; `term_to_ast::visit_ASSIGN`
  распознаёт многоимённый LHS.
- **`trust::Dict`:** добавлены `operator bool()` (непустой — истинен), `pop_front()` (первый элемент
  как `std::any` его естественного типа + удаление, пустой → out_of_range), `TypedValue::toAny()`
  (значение как `std::any` естественного C++-типа).
- **Константность как ортогональный квалификатор типа (`kConstFlag`):** бит 30 младшей половины
  `TypeId` (по образцу `kInferredFlag`) для признака константности (неизменяемости). Два уровня:
  «константность в типе» (`x^ := 42` → `const int8_t c_x = 42;`; `getCppTypeName`/прототипы берут
  const из типа) и «пер-переменная» (ставится на `Symbol::type` по мере анализа; в тип/сигнатуры не
  входит). Бит не входит в интернирование; `getCanonicalTypeId`/`getIndexFromId` снимают его.
  Константность **объявления** в кодогенерации берётся из `attr::ReadOnly` на узле (`^`/`@[readonly]@`),
  не из `Symbol::type` (became-const-переменная мутировалась до финализации → декларация не-const).
  **Become-const и защита от записи:** LHS с `^` (`x := 42; x^ += 1;`) делает переменную константной,
  обычная запись в константу — ошибка «cannot assign to constant variable». Документ:
  `include/types/CONST.md`.
- **Нетипизированная переменная без выведенного типа — без тихого fallback:** семантика явно
  помечает неопределённый тип типом `std::any` (`VarDecl::inferredType`/`Symbol::type`) — для тип-less
  инициализаторов (C++-вставка, вызов с неизвестным результатом, отрицательный литерал) и для
  нетипизированного forward-объявления (`x := ...;`). `Any` становится обычным выводимым типом,
  транспайлер эмитит его единообразно (`emitTypeName(inferred)`, без ветки «угадывания»); `INVALID`
  тип у переменной — явная диагностика «unable to infer type...». Тихо на `std::any` не падает ничто.
- **Голый тип-имя в `:=` — ошибка:** `x := :Int32` невалидно (в `:=` справа значение; тип объявляется
  через `::=`) → диагностика «cannot assign a type ... to a value variable ...; use '::='...».
  Каст `:T(a)` (CastExpr) не затрагивается.
- **Опция `-Wembed` (default Warning):** предупреждение за сам факт использования C++-вставки
  `{% ... %}` (независимо от имён внутри). Существующая диагностика «имя в embed не объявлено»
  не меняется. Во всех lit-тестах опция отключена (`-Wembed=ignore` в подстановке `%trust`).
- **Опция `-Wsigil` (default Warning):** нормализация простого имени без сигила, объявленного в
  локальном скоупе через `:=`, в локальную переменную `$x` (символ регистрируется как `$x`,
  `x` резолвится в `$x`). На глобальном уровне и для `::=` (типы) не применяется.
- **Единое разрешение простых имён с правилами вывода сигилов** (`resolveSimple`/`resolveSimpleRead`):
  bare-имя `x` → `$x` (локальная), затем `x` (глобал/параметр), затем `%x` (нативная функция);
  `$`-имя `$x` → `$x`, затем bare `x` (`n` и `$n` — одно локальное имя). При попадании на `$x`/`%x`
  текст узла-ссылки нормализуется на эту форму, чтобы манглинг совпал с объявлением. Это позволяет
  вызывать `%fib` как `fib` и ссылаться на параметр `n` как `$n`.
- **`extern "C"` только для forward-деклараций функций:** определения функций (с телом) — обычные
  C++-функции без `extern "C"` (при наличии forward-объявления определение наследует C-линковку по
  [dcl.link]); это убирает warning `-Wreturn-type-c-linkage` для `auto`-возврата кортежа у
  пользовательских функций (`%fib(...):Tuple(...) := {...}`).
- **Компиляйт-тайм проверка явных индексов формат-строки:** `'{N}'(args)` с `N >=` числа аргументов —
  диагностика «format string ... references argument index ...» (вместо сломанного C++ от
  `std::format`).
- **`Symbol::storage`:** месторасположение переменной (Global/Local/Static/ThreadLocal), фиксируется
  анализатором имён при объявлении (ThreadLocal-атрибут → ThreadLocal; имя с `::` → Static;
  внутри функции → Local; иначе Global; параметры → Local).
- **`name_to_cpp` срезает ведущий `$`-сигил** локальной переменной (как `%` у нативных): локальная
  `$x` и embed-ссылка `{% $x %}` согласованно мапятся в `c_x`.
- **Автогенерируемые C++-файлы:** первая строка — сообщение об автогенерации (проект, полная версия
  компилятора, дата/время); текст LICENSE в файл НЕ встраивается, а копируется в каталог сборки
  рядом с Makefile/build.conf.
- **`SourceMapWriter::output_prepend_leading`:** непорядковый «leading» префикс выходного буфера
  (эмитится первой строкой до инклудов), учитывается в source-map через prepend-смещение.
- **Temp-каталог для `--run`/хешбанга:** без `--temp-dir` файлы сборки идут в локальный
  `<cwd>/.trust/<stem>` (при запуске хешбанга cwd = каталог исходника → локальный `.trust/`,
  каталог исходника не загрязняется напрямую).
- **Хешбанг с несколькими аргументами:** Linux передаёт весь текст после интерпретатора одним
  argv-токеном (`--run -Wembed=ignore ...`); `trust` разбивает option-аргументы с пробелами на
  отдельные токены, чтобы `--run` и `-W`-опции распознавались корректно.

- **Проверки `assert`/`verify` включены по умолчанию:** флаг `assert` (DSL-макросы
  `@assert`/`@verify`) активен по умолчанию (безопасность по умолчанию); отключается
  через `-Wno-assert` (ранее `-Wno-assert` был значением по умолчанию).
- **Исправлен обратный маппинг source-map при prepended-инклудах рантайма:** в
  `SourceMapWriter::toReader()` ключи `m_backward` (cpp-begin) не сдвигались на размер
  prepended-инклудов, из-за чего `findRangeMap`/`findRange` (hover/definition/documentLink
  в trust-lsp и DAP) возвращали «соседний» statement при наведении на символ в `.cppt`.
  Ключи теперь пере-выравниваются по `from.begin`. Ховер над вложенным выражением
  (`c_mult += 1`) даёт ровно одну обратную ссылку на внутренний statement, без ссылок
  на внешние родительские range (регрессионный тест в trust_lsp_test).
- **Исправлен range определения макросов DSL (trust-lsp):** `getMacroDefRange` возвращал
  проекцию диапазона определения по позиции курсора (через `findRange`), из-за чего для
  макросов в конце `trust/dsl.src` диапазон уходил за пределы source и прямой ховер
  (trust → cpp) над `@assert`/`@while`/`print` падал («range out of bounds»). Теперь
  возвращается полный диапазон определения макроса (`m_macroForward[..].to`).
- **Системная функция `print(fmt, args...)`:** форматный вывод в `trust::outs()`
- **Системная функция `print(fmt, args...)`:** форматный вывод в `trust::outs()`
- **Рациональный литерал `num\den` как отдельная лексема (`RationalLiteral`):** токен
  `RATIONAL` маппится в собственный `ParserToken::Kind::RationalLiteral` (не IntLiteral);
  транслируется в `trust::Rational("num\den")` — однострочный конструктор Rational парсит
  `num\den` внутри; тип `Rational` (`literalType`). `Rational` получил конструктор
  `Rational(std::string_view value)` и неявный `operator std::string()` (для `:StrChar`).
- **Расширение авто-выведенного `Bool` до `Int64`:** `mult := 1; mult += 1;` — Bool,
  использованный в составной числовой арифметике, расширяется до максимального Int;
  явный `:Bool` при такой арифметике — ошибка (не расширяется).
- **Точка входа `@main():={...}`:** entry-функция (`*__main__`) эмитится с сырым именем,
  типом возврата `int` и инжектом `return 0;`, совпадая с `_main.cppt` из pipeline.
- **`trust/checked_cast.hpp`:** внутренний инклуд `trust/assert.hpp` заменён на
  относительный `assert.hpp` (устранён дубль при совместном использовании с реальным и
  извлечённым в temp заголовком assert.hpp).
- **Унификация реестра рантайм-символов (`types/runtime_symbols.hpp`):** имена и заголовки
  всех рантайм-символов собраны в одном списке X-macro (`TRUST_RUNTIME_SYMBOLS`), из которого
  автоматически генерируются типизированный `RuntimeSymbolId`, массивы заголовков и функции доступа
  (`runtimeSymbolName`/`runtimeSymbolHeaders`); `registerRuntimeSymbol(RuntimeSymbolId)` регистрирует
  символы циклом по таблице. Запись заголовков в транспиляторе — только через `recordRuntimeSymbolHeaders(RuntimeSymbolId)`
  (строковая перегрузка удалена): `visit_CastExpr` передаёт id напрямую
  (`RuntimeSymbolId::kAnyTo`/`kCheckedCast`), callee резолвится через `findRuntimeSymbolByName`,
  текст EMBED сканируется хелпером `recordRuntimeSymbolsInText` — опечатка в имени символа теперь
  ошибка компиляции, а не «молчаливый» пропуск инклуда. Добавлен EXPECT-инвариант: рантайм-символ
  не должен дублировать тип из `registerBuiltinType` (типы Dict/Rational покрываются по-типу и как
  символы не регистрируются); `reset()` больше не накапливает дубли `m_runtimeSymbols`.

  в стиле `std::format` (публичный рантайм-заголовок `trust/io.hpp`, символ
  `trust::trust__print__`; реестр рантайм-символов). `Rational` форматируется как
  символьная строка `num\den` (специализация `std::formatter<trust::Rational>`
  в `trust/rational.hpp`).
- **Модули-исходники как отдельные единицы компиляции:** на сайте импорта `\module(mod, masks)`
  эмитится только forward-decl экспортов (прототипы функций / `extern` переменных / алиасы типов),
  а определения генерируются отдельным `.cppt` модуля и линкуются с главным файлом (`SRC_MODULES`).
- **Экспорт-интерфейс модуля:** анализатор собирает экспортируемые декларации (top-level в не
  анонимных областях имён, включая типы) в `ModuleRegistry::m_interface` и отфильтрованный список —
  в `ModuleNode::m_exports`. Фильтр импорта — glob-маски (`*`/`?`, через запятую) в аргументах
  `\module(...)`.
- **`__trust_exports`:** добавлено поле `decls` — строка перечисления экспортируемых имён в стиле
  forward-decl через `;\n`; при сборке `.trust` в `__trust_export_entries` попадают экспортированные
  символы модулей.
- **Source-map:** подавление маппинга для синтетических forward-decl (чтобы не конфликтовать с
  диапазоном модуля в его собственном `.cppt`).
- **Грамматика (синтаксис):** shift/reduce-конфликты устранены полностью (было 9, стало 0; `%expect 0`).
  `{% %}` (EMBED) — обычное выражение с обязательной `;`; хвостовой док `///<`/`##<` — отдельный токен
  `DOCUMENT_INLINE` (термин остаётся `TermID::DOCUMENT`, объявлен в `TERMS` как `NotApplicable`);
  соседние доки не сливаются (`doc_list` — одиночный `DOCUMENT`); убрана самовложенность `separator`
  (`;;` в sequence по-прежнему валиден как пустой оператор).

## [Release v0.5 - current version](https://github.com/afteri/trust-lang/releases/v0.5.0))

Complete rework of the project's code base with a name change (**TrustLang -> TrustLang**).
This release is the first after the switch to agent-driven development and is intended
to capture the state after a fundamental architecture refactoring.

Currently only a minimal set of language instructions is implemented (variable and
function declarations, built-in code blocks, etc.), as the main focus is on building
a full ecosystem: a VS Code plugin, and LSP and DAP servers for code debugging.

### Standard features (as in conventional compiled languages)
- Transpilation to C++ followed by build via generated Makefile.
- Lexical and syntax analysis using Flex/Bison.
- Macroprocessor and DSL macros.

### Project-specific features
- **Transparent display of generated code fragments:** a built-in source map (trust <-> C++)
  embedded into the ELF `.debug_trust_map` section; LSP and DAP allow navigation between
  the source trust code and the generated C++, and setting variable values in both files.
- **VS Code extension:** syntax highlighting and LSP integration.
- **LSP server (`trust-lsp`):** Go to Definition, hover, inlay hints, document links,
  incremental synchronization and debounce.
- **DAP server (`trust-dap`):** prototype debugging via GDB/MI with trust <-> C++ position translation.
- **Solver (SMT-LIB 2):** prototype formula generation and optional Z3 integration for formal
  verification (Trust Checking).


## [Release v0.4 (23.03.2024) - current version](https://github.com/afteri-ru/trust-lang/releases/tag/v0.4.0))

## New features and changes in the syntax of TrustLang v0.4
- Reworked the definition of object types using [prefix naming (sigils)](https://trust-lang.net/docs/syntax/naming/)
- Interrupting the execution flow and returning can now be done for [named code blocks](https://trust-lang.net/docs/ops/throw/).
- Simplified the syntax for importing [native variables and functions (C/C++)](https://trust-lang.net/docs/types/native/)
- Stabilized the syntax for [initializing tensor, dictionary, and function argument values](https://trust-lang.net/docs/ops/create/#comprehensions) with initial data.
- Added built-in macros for writing code using keywords in a [DSL style](https://trust-lang.net/docs/syntax/dsl/)

## New compiler features (nlc)
- Completely redesigned the macroprocessor.
- Reworked the compiler architecture with division into parser, macroprocessor, syntax analyzer, interpreter, and code generator.

## Miscellaneous
- The documentation [website](http://trust-lang.net) has been translated to [Hugo](https://gohugo.io/) and made bilingual.
- Instead of binary builds, a section [Playground and example code](https://trust-lang.net/playground/) has been added to the website for small experiments.
- Transition to clang-16 has been completed (transition to clang-17 and newer is planned after full implementation of coroutines and support for extended floating-point number formats).
- The number of project contributors has increased to more than one!

------

## [Релиз v0.3 (07.11.2022)](https://github.com/afteri-ru/trust-lang/releases/tag/v0.3.0)

### Новые возможности и изменения в синтаксисе TrustLang v0.3

- Простые чистые функции удалены.
- Зафиксирован синтаксис операторов проверки [условия](https://trust-lang.net/ru/ops.html#условный-оператор) и [циклов](https://trust-lang.net/ru/ops.html#операторы-циклов). 
- Оператор цикла **while** теперь поддерживает конструкцию [**else**](https://trust-lang.net/ru/ops.html#операторы-циклов).
- В синтаксис TrustLang добавлены [пространства имен](https://trust-lang.net/ru/syntax.html#пространства-имен).
- Реализована часть концепции ООП и добавлена поддержка [определения классов](https://trust-lang.net/ru/type_oop.html).
- Переработана идеология [возвратов из функции и обработки исключений](https://trust-lang.net/ru/newlang_doc.html#операторы-прерывания-выполнения-оператор-возврата).

### Разное версии 0.3

- Выполнен переход на clang 15
- Реализован вызов функций с помощью libffi
- Сделана полноценная поддержка Windows

------

## [Релиз 0.2 (11.08.2022)](https://github.com/afteri-ru/trust-lang/releases/tag/v0.2.0)

### Новые возможности и изменения в синтаксисе TrustLang 0.2

- Добавлены макросы (появилась возможность использовать более привычный синтаксис на основе ключевых слов)
- Реализованы итераторы
- Добавлен новый тип данных - рациональные числа не ограниченной точности
- Многострочные комментарии стали вложенными
- Имена встроенных типов переименованы с указанием размерности

### Другие важные изменения в версии 0.2

- Вместо использования gcc перешел на clang, а libffi замененил на JIT компиляцию вызова для нативных функций
- В релиз добавлены бинарные сборки для Linux
- Начало портирования кода на Windows

------

## [Релиз 0.1 (24.06.2022) - первая публичная версия](https://github.com/afteri-ru/trust-lang/releases/tag/v0.1.0)

- Представление общей концепции языка
- Сборка тестов и примеров под Linux из исходников
