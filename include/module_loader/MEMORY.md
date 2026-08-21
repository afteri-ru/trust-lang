# MEMORY.md

> scope: include/module_loader
> role: persistent-memory
> last_reviewed: 2026-08-19
> review_period: 30
> max_size: 10000

# Компонент module_loader

## Назначение

Оркестрация загрузки модулей и реестр загруженных модулей. Интегрирован с syntax Parser:
`parseSourceModule()` рекурсивно вызывает Parser (`expand_module=true`); `ensureLoaded()` резолвит
файл `<name>.trust` (заглушка) или `<name>.src` и вызывает `parseSourceModule()`.

## Компоненты

### ModuleLoader (`module_loader.hpp`)

Точка входа. Владеет `ModuleRegistry` (по значению), стеком текущих модулей `m_moduleStack` и
картой `m_indexByName` (имя → индекс). Держит `Context&` (невладеющую ссылку на `diag`).

- **Метод A** - `parseSourceModule(moduleId, MapperFile src, range)`: регистрирует модуль,
  парсит из уже зарегистрированного source-файла, сохраняет исходное Term-тело модуля.
- **Метод B** - `ensureLoaded(moduleId, range)`: резолв файла (заглушка/`.src`) + Метод A.
- Lookup без загрузки: `indexOf`, `isLoaded`, `body`, `moduleName`, `currentModuleIndex`.
- Циклическая зависимость → `diag().report(Severity::Fatal, range, ...)`.
- Не копируется и не перемещается (copy/move deleted).

### ModuleRegistry (`module_registry.hpp`)

Реестр на динамическом массиве `std::vector<ModuleRecord>`. Идентификация модулей - **только по
индексу** (`std::size_t`); `moduleId` (`string_view`) используется лишь при загрузке для детекции
циклических зависимостей.

- `ModuleRecord`: `m_moduleId` (канонический путь), `m_handle = variant<MapperFile, void*>`
  (source-файл или бинарный dlopen-хендл), `m_body` (`TermPtr` - Term-тело после макропроцессинга),
  `m_interface` (`vector<TermPtr>` - экспортируемые декларации-термы, заполняет анализатор),
  `m_bodyAst` (`vector<AstNodePtr>` - сконвертированное тело, хранимое один раз).
- API: `getOrLoad`/`isLoaded`/`setBody`/`body`/`moduleName`/`count` + accessor'ы интерфейса
  (`setInterface`/`interface`/`hasInterface`) и тела (`setBodyAst`/`bodyAst`).
- Детекция цикла: запись существует, но тело ещё не заполнено → модуль в процессе загрузки → FAULT.
- Выход индекса за границы → FAULT.

## Владение и безопасность

- **Владелец `ModuleLoader` - Pipeline** (`unique_ptr<ModuleLoader>`), внедряется в `Context`
  невладеющим указателем через `Context::setLoader`. `Context` (diag) loader-ом **не владеет**.
- Обратная связь `ModuleLoader::m_ctx` - невладеющая `Context&`. Цикл по ссылкам, но не по владению
  (единый владелец - Pipeline) - безопасно при порядке создания/уничтожения в Pipeline
  (`Context` живёт не меньше loader-а).
- `ModuleLoader`/`ModuleRegistry` не копируются; `ModuleRegistry` перемещается.
- Терм-дерево (`TermPtr`, shared_ptr) владеется реестром (в AST конвертируется рекурсивно при
  построении ModuleNode); висячих Term-указателей не создаётся.

## Зависимости

- `ast` (`TermPtr` через ModuleRegistry), `syntax` (Parser для разбора), `location` (MapperFile),
  `diag` (`Context`/`DiagnosticEngine`).
- От semantic/transpiler/pipeline прямых зависимостей нет (используется ими сверху).

## Примечание: необъявленная связь syntax ↔ module_loader

`syntax/parser.cpp` при обработке import сам вызывает `m_ctx.loader()` (`ensureLoaded`/`body`/
`indexOf`), но `syntax_lib` не линкует `module_loader_lib` - символы используются транзитивно
через `Context`. Это разрыв инкапсуляции: зависимость на уровне символов не объявлена в CMake
(подробнее - в анализе владения). Целесообразно перенести обработку import в module_loader, чтобы
syntax не знал о существовании loader-а.

## Модель импорта и экспорта (source-модули)

- **Экспорт** (`include/module_loader/module_export.{hpp,cpp}`): по умолчанию экспортируются ВСЕ
  top-level объявления/определения в **не анонимных** областях имён (глобальная `::`, именованные
  `ns::` - с квалификацией). Из анонимной `_` и локальных - не экспортируются. `collectExportedDecls`
  собирает декларации-термы (term()) экспортов из AST-тела модуля; `matchGlob`/`matchesAnyMask` -
  glob-фильтр масок.
- **Фильтр импорта**: аргументы оператора `\module(mod, masks)` - список glob-масок (`*`, `?`,
  через запятую = OR). Пустой список - все экспорты.
- **Интерфейс модуля**: `ModuleRecord::m_interface` - «полный» экспорт (заполняет анализатор);
  `ModuleNode::m_exports` - отфильтрованный интерфейс конкретного сайта импорта.
- **Раздельная компиляция**: каждый импортированный `.src`-модуль генерируется отдельным `.cppt`
  (полное тело - определения) и линкуется с главным файлом (`SRC_MODULES` в Makefile). На сайте
  импорта эмитятся только forward-decl экспортов (прототипы функций / `extern` переменных / алиасы
  типов); `__trust_get_exports`/экспорт-таблица встраиваются ТОЛЬКО в главный файл (иначе дубли
  при линковке).
- **Динамическая библиотека (.trust)**: экспорт-таблица главного файла (`__trust_export_entries`)
  включает экспортированные символы модулей (адреса резолвятся через extern в линкованном `.so`);
  в `__trust_exports` добавлено поле `decls` - строка перечисления экспортируемых имён в стиле
  forward-decl через `;\n` (для парсинга при будущей загрузке модуля как бинарного файла).
