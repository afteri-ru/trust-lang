# Перекрёстные отображения trust ⇄ C++ и правила ховеров (LSP)

> Отдельный документ с правилами создания перекрёстных отображений между файлами
> исходных текстов (`.src`) и сгенерированными (`.cppt`), а также с правилами
> создания и оформления ховеров, применяемых в trust-lsp (LSP).

Документ описывает **как** строятся связи «trust-строка ⇄ cpp-строка» и **как**
эти связи превращаются в кликабельные ссылки в ховере/definition/documentLink.
Рекомендуется читать вместе с `diag/MEMORY.md` (SourceMap / SourceMapReader) и
`lsp/MEMORY.md`.

---

## 1. Виды маппингов

Все маппинги строятся в **mapper space** (`SourceMapWriter`, `Context::source()`)
и читаются в **reader space** (`SourceMapReader`, `ctx.source().toReader()`).

### 1.1 Statement-маппинг `RangeMap { from, to }`

Связывает диапазон исходника с диапазоном выхода:

| Поле      | Смысл |
|-----------|-------|
| `from`    | range в input-файле (trust `.src`) |
| `to`      | range в output-файле (C++ `.cppt`) |

- **forward** (`m_forward`, trust→cpp): строится транспайлером через
  `mapStart`/`mapStop` (`MapperScope`) на каждый statement.
- **backward** (`m_backward`, cpp→trust): зеркалируется автоматически.

`findRangeMap(loc)` выбирает нужную карту по типу файла: input → `m_forward`,
output → `m_backward`. Используется для hover/definition, когда NameMap нет.

### 1.2 Name-маппинг `NameMap { rangeMap, fromName, toName }`

Связывает **объявленное имя** (переменную, тип, функцию, параметр) с его
C++-именем на противоположной стороне.

- Строится через `addNameMapping()`.
- `fromName` - имя в trust, `toName` - имя в C++.
- Читается через `getCppName(trustLoc, word)` (trust→cpp) и
  `getTrustName(cppLoc, word)` (cpp→trust).

> **Инвариант (оффсет):** cpp-оффсет имени нельзя считать от начала вывода.
> `output_prepend` (инклуды) сдвигает `mapStackTop().outputBegin`. Оффсет
> считается только от `outputBegin.offset() + длина префикса` сгенерированной
> строки. (см. memory компонента `transpiler`).

### 1.3 Macro-маппинг `m_macroForward { call_range → def_range }`

Связывает **вызов** макроса (в пользовательском `.src`) с **определением**
(в DSL-исходнике):

- Строится через `addMacroMapping(call_range, def_range)` в макропроцессоре
  (`src/syntax/macro.cpp`).
- `call_range` - реальный диапазон вызова макроса в пользовательском файле.
- `def_range` - диапазон тела определения. Для `@@...@@` берётся по фактическим
  токенам тела (не по `@@`-обёртке).
- Читается через `getMacroDefRange(loc)` (возвращает **полный** диапазон
  определения, без проекции по позиции курсора).

> **Раскрытый код** (тело макроса) после подстановки получает **range вызова**
> (`m_mapperRange = call_range`). Поэтому statement-маппинг вызова макроса
> корректно ведёт в сгенерированный C++ код.

---

## 2. Файлы и навигабельность диапазонов

Ключевое правило навигации: **ссылка навигируема только если целевой файл
существует на диске.** URI строится как `file://<путь>#<line,col>`.

### 2.1 In-memory источники (не навигируемы)

Некоторые input-файлы в source map являются **фиктивными** - их нет на диске.
Имя помечается префиксом `@` (`isInMemoryName`). Пример - встроенный DSL:

- Загружается в `Pipeline::loadDslMacros()` через
  `parser.ParseText(source, "@trust/dsl")` и регистрируется как input `@trust/dsl`.
- **Определения макросов** (`def_range`) лежат именно в `@trust/dsl`.

Если строить ссылку на `@trust/dsl` напрямую - фрагмент `#L..` из координат `@trust/dsl`
применится к пользовательскому `.src` (или несуществующему файлу) и уведёт в
неверное место. Поэтому:

> **Правило:** диапазоны в `@trust/dsl` НЕ должны использоваться как цели ссылок,
> пока `@trust/dsl` не сохранён на диск. Иначе ссылка не навигируема.

### 2.2 Сохранение `dsl.src` на диск (для навигации по макросам)

Чтобы ссылка на определение макроса стала навигируемой, при генерации выходного
файла необходимо сохранить DSL **вместе с остальными заголовками**:

- В `TrustLsp::transpileSource()`, когда `saveToDisk` (`tempDir` задан):
  - из reader (`ctx.source().toReader()`) найти input `@trust/dsl` через `findFile("@trust/dsl")`;
  - его содержимое (`reader->source(dslIdx)`) записать в `<tempDir>/trust/dsl.src`
    (каталог `trust/` рядом с `.cppt` - там же, где раскладываются остальные
    заголовки рантайма, напр. `trust/rational.hpp`).
- Ссылки «Macro:» на определения макросов строятся с `basePath = dslFilePath`
  (см. §4.1), потому что координаты `def_range` относятся именно к `@trust/dsl`.
  Путь к `dsl.src` **выводится из `cppFilePath`** (уже хранящего полный путь к
  `.cppt`): `<каталог cppt>/trust/dsl.src`. Отдельное поле в `CachedSource` не
  нужно - расположение `dsl.src` детерминировано относительно `.cppt`.

> **Инвариант:** содержимое `@trust/dsl` в source map должно побайтово совпадать с
> сохранённым `dsl.src`, иначе строка/колонка фрагмента разъедутся.

> **Про in-memory имя:** в source map DSL всегда остаётся фиктивным источником
> `@trust/dsl` (префикс `@` - `isInMemoryName`, `readFilesFromDisk` его пропускает).
> Это **не** путь на диске; реальное место хранения - `<tempDir>/trust/dsl.src`.
> Менять `@trust/dsl` на реальный путь нельзя - CLI-чтение source map (`.src_map`/ELF)
> полагается на `@`-префикс для пропуска in-memory источников.

---

## 3. Fragment-URI

Формат цели ссылки:

```
file://<путь>#<fragment>
```

`fragment` строится через `SourceMapReader::rangeToFragmentString(range)` и
содержит координаты `line,column` целевого файла. Вспомогательная функция:

```cpp
makeFragmentUri(reader, basePath, range) =
    filePathToUri(basePath) + "#" + reader.rangeToFragmentString(range)
```

> `basePath` - путь файла-цели, а **не** файла, где лежит range. Координаты
> берутся из самого range. Поэтому важно передавать правильный `basePath`
> (путь навигируемого файла), иначе фрагмент укажет не туда.

---

## 4. Правила создания ховеров (`buildHoverContents`)

`buildHoverContents()` возвращает Markdown-массив (`hoverContents`), состоящий из:

- **`[0]`** - базовый блок с кодом противоположной стороны:
  ` ```<lang>\n<text>\n``` `.
- **`[1+]`** - Markdown-ссылки на определения (если курсор на
  идентификаторе/макросе).

Сигнатура (см. `lsp/trust_lsp.h`):

```cpp
buildHoverContents(reader, isCppRequest, cursorLoc, hoverText, hoverLang,
                   trustFilePath, cppFilePath)
```

Путь к `dsl.src` внутри `buildHoverContents` выводится из `cppFilePath`:
`<каталог cppt>/trust/dsl.src`; если файла на диске нет - ссылка «Macro:» не
выводится.

### 4.1 Запрос из trust-файла (`!isCppRequest`) - «→ C++»

1. Выделить слово под курсором: `reader.getWordAt(cursorLoc)`.
   Если курсор не на идентификаторе - вернуть только базовый блок.
2. `reader.getCppName(cursorLoc, word)`:
   - **обычное объявленное имя** (`!macroDefRange.has_value()`):
     ссылка `[→ C++: <fromName>](cppFilePath#fragment)` на `NameMap.rangeMap.to`;
    - **макрос** (`macroDefRange.has_value()`) **или** имя не найдено:
      a. **Ссылка «Macro:»** на определение макроса идёт ПЕРВОЙ:
         `[Macro: <word>](<каталог cppt>/trust/dsl.src#fragment)` по
         `macroDefRange`. Диапазон выделяет **весь макрос целиком** (имя + тело).
      b. **Ссылка «→ C++»** на раскрытый код в `.cppt` - вторая, через
         `reader.findRangeMap(cursorLoc)` → `to` (если это output).
         **Переход по клику на текст идёт только в `.cppt`.** Идёт второй, чтобы
         большое тело макроса не скрывало ссылку на определение.
     c. Если имя не макрос и не нашлось statement-маппинга в output -
        ссылок на C++ не добавляется.

### 4.2 Запрос из C++-файла (`isCppRequest`) - «← Trust»

1. `reader.getTrustName(cursorLoc, word)`:
   - найден NameMap → ссылка `[← Trust: <toName>](trustFilePath#fragment)`
     на `NameMap.rangeMap.from`;
2. если NameMap нет (expression-операторы, embed и т.п.) - **fallback** на
   statement-маппинг `findRangeMap(cursorLoc)` (backward cpp→trust), если `to`
   не является output:
   `[← Trust: <text>](trustFilePath#fragment)`.

### 4.3 Общие правила оформления

- Иконки направлений: `→ C++:` (trust→cpp), `← Trust:` (cpp→trust),
  `Macro:` (определение макроса). Это маркеры, на которые ориентируются тесты.
- Ссылка - Markdown `[label](uri)`. `label` - текст противоположной стороны
  (для NameMap - имя, для statement - текст фрагмента).
- Цель всегда `makeFragmentUri` (§3). Для макроса вторичная ссылка использует
  `<каталог cppt>/trust/dsl.src` как `basePath`.
- Ховер не должен падать на макросе в конце DSL-файла: `getMacroDefRange`
  возвращает **полный** диапазон определения (без проекции по курсору), чтобы
  range не уходил за границы source.

---

## 5. definition и documentLink (кратко)

- `textDocument/definition` - по `RangeMap`/`NameMap`/`macro` строит
  `Location { uri, range }` и возвращает его целиком.
- `textDocument/documentLink` - собирает по файлу все ссылки:
  - cpp→trust: statement-маппинги (backward) + NameMap (to ∈ cpp);
  - trust→cpp: `getTrustFileMappings` + NameMap (from ∈ trust);
    **для макросов и операторов цель одна - раскрытый код в `.cppt`** (клик
    ведёт только в `.cppt`). Определение макроса навигируемо из ховера
    (ссылка «Macro:»), а **не** из documentLink: раньше для макроса цель
    строилась с `basePath=filePath`, но координаты определения из `@trust/dsl`
    применялись к `.src` → переход уводил в конец файла.
- **Фильтр надмножеств:** из documentLink убираются диапазоны, являющиеся
  строгим надмножеством другого диапазона (например, маппинг всей функции
  перекрывал маппинги отдельных операторов и уводил клик к началу функции).
  Остаются только точечные (leaf) диапазоны операторов.

## 6. getWordAt и макросы

`getWordAt` распознаёт `@` как часть слова, поэтому ховер над макросом
(`@assert`/`@while`/`print`) выделяет полное имя с префиксом. Без этого на
позиции `@` слово было пустым и ховер не давал ссылок.

---

## 6. Тесты

Правила покрываются в `test/unit/lsp/lsp_handler_test.cpp` / `test/unit/lsp/lsp_hover_test.cpp`:

- `HandleHover_CppReverseLinkForRationalExample`:
  - cpp→trust обратные ховеры (decl, макрос `@assert`, вложенные выражения);
  - trust→cpp ховеры над макросами `@assert`/`@while`/`print`: проверяется, что
    «→ C++»-ссылка ведёт в `.cppt` **и** что есть вторичная «Macro:»-ссылка,
    ведущая в `dsl.src`.

---

## 7. Чек-лист при изменениях маппинга/ховера

1. Любой новый диапазон в in-memory источнике (`@trust/dsl`) - сделать его
   навигируемым: сохранить файл на диск и использовать его путь как `basePath`.
2. Для макроса всегда давать **обе** ссылки: «→ C++» (в `.cppt`) и «Macro:»
   (в `dsl.src`).
3. `basePath` в `makeFragmentUri` - путь **целевого** файла.
4. Не выдавать ссылку на невалидный/выходящий за source range.
5. Обновить `lsp/MEMORY.md` при изменении структуры ховеров/маппингов.

