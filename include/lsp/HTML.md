# trust-lsp HTML/JSON-контракт (playground)

Документ описывает контракт двухоконного playground (godbolt-стиль) между
`trust-lsp`, отдельным сервером-исполнителем и страницей сайта. Читать вместе с
`lsp/MEMORY.md`.

## Режимы

`trust-lsp` дополнительно к LSP-режимам (interactive / `server[=<port>]`)
поддерживает два режима вывода результата in-process транспиляции Trust→C++:

- `--json [input]` - печатает в stdout JSON-контракт (см. ниже). input - `.src`
  файл; если опущен или `-` - читает stdin.
- `--html [input]` - печатает в stdout godbolt-стиль HTML-фрагмент: два редактора
  Monaco (Trust | C++ read-only) + встроенный glue-JS.
  - `--html-full` - обернуть фрагмент в полную HTML-страницу.
  - `--monaco-url <url>` - базовый путь сборки Monaco (AMD `vs`); по умолчанию
    официальный CDN (`kDefaultMonacoUrl`).
  - `--server-url <url>` - endpoint живого запуска (см. §4).
  - `--examples-dir <dir>` - каталог с `*.src`, встраиваемый в фрагмент как
    статический список примеров (комбобокс выбора). Каждый файл становится
    элементом `{name, source}` (name = basename без расширения).

## JSON-контракт (`resultToJson`, live-контракт страницы)

```json
{
  "source": "<Trust-текст>",
  "cpp": "<сгенерированный C++: autogen-заголовок + include + тело>",
  "ok": true,
  "error": "",
  "trustToCpp": [[], [4,5,6], [4], ...],   // индекс = строка Trust (1-based) → строки C++
  "cppToTrust": [[], [], [], [1], ...],     // индекс = строка C++ (1-based) → строки Trust
  "log": "<stderr trust-lsp: диагностика/предупреждения>"   // опционально (worker)
}
```

Массивы `trustToCpp`/`cppToTrust` строятся проекцией forward (`m_forward`) и
backward (`m_backward`) маппингов source-map на строки через `line_column()`.
Индексы 1-based; сортированы по возрастанию, без дубликатов. `ok=false` при
ошибках транспиляции (`error` - текст диагностики), при этом `cpp`/map могут
быть частичными.

Дополнительные поля (возвращаются балансировщиком/воркером, не входят в
статический HTML-конфиг):

- `log` - stderr субпроцесса `trust-lsp` (полный текст диагностики), если он
  непуст; glue-JS выводит его в окно лога под панелями.
- `/run` НЕ возвращает build-архив и не имеет поля `buildArchiveUrl`/`archive`.
  Build-архив собирается **лениво** - отдельным `POST /download` (см. §4), который
  заново обрабатывает текущий код и сразу отдаёт `tar.gz` (без кеша на
  балансировщике).


## HTML-контракт

Фрагмент - самодостаточен: встроены `<style>` (CSS-переменные `--tpl-*` для
переопределения темы сайта), разметка, конфиг и glue-JS. Внешней остаётся только
библиотека Monaco (`--monaco-url`).

### Структура DOM

```html
<style> ... .tpl-pg { --tpl-bg; --tpl-text; --tpl-gutter; --tpl-border;
                       --tpl-toolbar; --tpl-linked; --tpl-error; } ... </style>
<div class="tpl-pg" id="trust-playground">
  <div class="tpl-row">
    <div class="tpl-pane"><div class="tpl-toolbar">Trust
        <select id="tpl-examples" class="tpl-examples" title="Load example"></select></div>
      <div id="tpl-trust-editor" class="tpl-editor"></div></div>
    <div class="tpl-splitter-v" id="tpl-split-v"></div>
    <div class="tpl-pane"><div class="tpl-toolbar">Generated C++
        <a id="tpl-download" class="tpl-btn" href="#" hidden title="Download build archive">⬇ Build</a></div>
      <div id="tpl-cpp-editor" class="tpl-editor"></div></div>
  </div>
  <div class="tpl-splitter-h" id="tpl-split-h"></div>
  <div id="tpl-log" class="tpl-log"></div>
  <div id="tpl-status" class="tpl-status"></div>
</div>
<script>...</script>
```

ID фиксированы (`trust-playground`, `tpl-trust-editor`, `tpl-cpp-editor`,
`tpl-examples`, `tpl-status`, `tpl-log`, `tpl-download`, `tpl-split-v`,
`tpl-split-h`) - один playground на страницу.
- `#tpl-log` - окно лога под панелями (в него пишется `log` контракта при транспиляции).
- `#tpl-split-v` / `#tpl-split-h` - сплиттеры **изменяемого размера**: вертикальный
  между панелями Trust и Generated C++ (ширина панелей) и горизонтальный над логом
  (высота окна лога). Перетаскивание мышью; после вертикального перетаскивания glue-JS
  вызывает `layout()` у редакторов Monaco.
- `#tpl-download` - кнопка «⬇ Скачать» (ленивый `POST /download`: заново обрабатывает
  текущий код и скачивает build-архив `tar.gz`).

### Встроенный конфиг (глобальное пространство имён `window.__TPG`)

```js
window.__TPG.config = { monacoUrl, serverUrl, source,
                        examples: [ {name, source}, ... ] };
window.__TPG.monarch = function(){ return (<Monarch-токенайзер Trust>); };
window.__TPG.glue   = function(m){ var __MONARCH__ = m; (<glue-JS>); };
window.__TPG.glue(window.__TPG.monarch());
```

`examples` - статический список примеров из `--examples-dir` (может быть
пустым): `name` - label комбобокса, `source` - исходный Trust-текст.

**Трансляция (C++), маппинги и статус (`cpp`, `trustToCpp`, `cppToTrust`,
`ok`, `error`) НЕ встраиваются в конфиг** - правый редактор стартует пустым,
и трансляция получается только от балансировщика (`serverUrl`) при каждом
изменении кода. В конфиге есть только статичные поля: URL Monaco/сервера,
исходный текст стартового примера и список примеров.

Все строки (`source`, `error`, URL, `examples[].name`/`.source`)
экранируются `jsonEscape` с заменой `<` → `\u003c` - это гарантирует, что
пользовательский код не завершит `<script>`-блок (HTML/JS-safety).

### glue-JS (навигация и живая пере-транспиляция)

- Регистрирует язык `trust` и задаёт Monarch-токенайзер (regex-подсветка Trust:
  комментарии `/* */`/`#`, макросы `@…`, переменные `$…`, типы `:…`, нативные
  `%…`, строки, числа, операторы). C++ - встроенный язык Monaco.
- Создаёт два редактора Monaco (`vs-dark`, Trust - редактируемый, C++ - read-only).
- Синхронная навигация: `onMouseDown` по строке → `deltaDecorations` с классом
  `.tpl-linked` на соответствующих строках противоположной панели
  (`trustToCpp`/`cppToTrust`).
- Живая пере-транспиляция: при изменении Trust-кода (debounce 400 мс) отправляет
  `fetch(serverUrl, POST, text/plain)` с текущим текстом; ответ - JSON-контракт;
  обновляет C++-редактор и маппинги. Если `serverUrl` пуст - пере-транспиляция
  отключена (статический фрагмент).
- **Лог и навигация по диагностикам**: окно лога (`#tpl-log`) выводит `log`/`error`
  из контракта. Строки-заголовки диагностик вида `файл:строка:колонка: severity:
  сообщение` (формат stderr `trust-lsp`, см. `diag/diag.cpp`) рендерятся кликабельными
  (классы `.tpl-log-link/.tpl-log-error/.tpl-log-warn`): клик переводит курсор редактора
  Trust на строку в исходнике (`gotoTrustLine`). При наличии в логе ошибки
  (`error`/`fatal`) курсор автоматически встаёт на строку первой ошибки. Контент лога и
  оверлея собирается через `createElement`/`textContent` (не `innerHTML`): строки от
  сервера (`data.error`, `instructionsUrl`) не исполняются как HTML.
- Обработка ошибок связи: правый редактор **стартует пустым** и заполняется
  только из успешного ответа балансировщика. При сетевом сбое, `{unavailable:true}`
  (нет воркеров) или ошибке балансировщика (`!ok` / HTTP-ошибка) glue-JS
  **очищает** правый редактор (`cppEditor.setValue('')`) и показывает по центру
  панели (оверлей `#tpl-cpp-overlay`) сообщение: «Нет связи с сервером песочницы»
  (сетевой сбой) либо сообщение балансировщика (его ошибка). Кнопка «⬇ Скачать»
  неактивна до первого успешного ответа.
- Комбобокс примеров (`#tpl-examples`): наполняется из `cfg.examples`. Начальный
  выбор - пример, чей `source` совпадает с `cfg.source`; если такого нет -
  отключённая опция «Custom». При выборе примера, если текущий текст Trust
  отличается от `source` последнего загруженного примера, glue-JS запрашивает
  подтверждение (`confirm`) перед заменой, иначе заменяет сразу. После замены
  текст редактора устанавливается из `ex.source`, что через существующий debounce
  (при заданном `serverUrl`) запускает пере-транспиляцию.


### URL-параметры (состояние страницы через query-string)

Страница может открываться в заранее заданном состоянии. Все параметры
опциональны; при отсутствии `line` позиция курсора не трогается:

- `file=<имя примера>` - выбрать предопределённый файл из `cfg.examples`
  (загружает его исходник и выбирает в комбобоксе).
- `win=src|cppt` - активное окно, куда ставится курсор (по умолчанию `src`;
  `cppt` = «Generated C++», применяется после первой успешной пере-трансляции).
- `line=<n>`, `col=<m>` - позиция курсора (1-based; `col` по умолчанию 1).
- `toLine=<n>`, `toCol=<m>` - конец диапазона выделения. Если заданы -
  устанавливается выделение от `line:col` до `toLine:toCol`, иначе просто курсор.

**Ссылка-копирование в статус-баре**: при навигации (курсор по строке) glue-JS
выводит в `#tpl-status` текст диапазона (`→ cpp: N` / `→ trust: N`) и сразу за ним
ссылку `.tpl-copy` **«🔗 скопировать ссылку»**. Клик строит URL текущего состояния
(`file` включается только если текст Trust не изменён относительно загруженного
примера; `win`/`line`/`col`/`toLine`/`toCol` - из активного окна) и копирует его
в буфер обмена (`navigator.clipboard.writeText`, фолбэк - `textarea`+`execCommand`),
с временной индикацией «✓ скопировано».

## Протокол с отдельным сервером (§4)

- `POST <serverUrl>` body = Trust-код как `text/plain; charset=utf-8`.
- Ответ `200` + тело = JSON-контракт из §2.
- Сервер реализует endpoint, линкуя `lsp_lib` и вызывая
  `trust::lsp::transpileToResult(code, name, opts)` (или делегируя
  `trust-lsp --json`), затем `resultToJson`. Пере-транспиляция выполняется
  сервером (in-process), а не браузером.
- При отсутствии свободных воркеров балансировщик отвечает `503` + тело
  `{"ok":false,"unavailable":true,"error":"...","instructionsUrl":"https://…/docs/sandbox/"}`.
  glue-JS очищает правый редактор и показывает по центру панели (оверлей) сообщение
  балансировщика со ссылкой на инструкцию; также выводит его в строку статуса/лог.
- `POST <serverUrl>/download` (балансировщик) - тело = Trust-код как `text/plain`.
  Ленивая сборка build-архива: отдельный запрос, заново обрабатывает файл
  (свежая транспиляция + сборка build-каталога самим trust-lsp `--emit-build-dir`,
  без компиляции), без кеша на балансировщике. Ответ `200` + `application/gzip` +
  `Content-Disposition: attachment; filename="trust-lang-<версия>-generated.tar.gz"`;
  иначе `400/429/503/502`. Кнопка «⬇ Скачать» использует этот endpoint.

## Тесты

`test/unit/lsp/html_emit_test.cpp`: маппинг строк, валидность JSON и round-trip,
структура HTML-фрагмента (Monarch/init/редакторы/конфиг), `--html-full`,
HTML-safety `jsonEscape`.
