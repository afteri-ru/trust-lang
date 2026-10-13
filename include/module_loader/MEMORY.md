# MEMORY.md

> scope: include/module_loader
> role: persistent-memory
> last_reviewed: 2026-11-02
> review_period: 30
> max_size: 8200

## Architecture

Оркестрация загрузки и реестр модулей; предмет — только ИСХОДНИКИ (compiled-форма — тот же текст).
`parseSourceModule()` рекурсивно вызывает Parser и сразу конвертирует Term→AST. ЧИСТАЯ политика пути
(резолв `\file`/`\\group`/`\\\pkg`, форма файл/каталог/любая, обход группы, каноникализация,
`TRUST_PACKAGE_PATH`) — `module_path_policy` (`module_path::collectTargetFiles` /
`collectGroupFiles` / `canonicalPath`), без `Context` и без диагностик (ошибка строкой). Поиск и
извлечение пакетов — `PackageStore` (`resolveRoot`/`ensureExtracted`, кэш
`<packageCacheDir>/<name>-<md5-12>/`, `usedArchives()`), тоже без `Context`. Установка интерфейса
файла (штамп модуля, блок интерфейса, реэкспорт, экспорт-набор и его факты) — `interface_install`
(`stampModuleLevelNode`/`installFileInterface`), шаги выполняются в ФИКСИРОВАННОМ порядке: они
мутируют `AstNodeBase::visibility`, и порядок наблюдаем в эмитируемом C++. Загрузчик остаётся
единственной точкой фатального выхода (`reportFatal`) и единственной точкой диагностики цикла
(`reportImportCycle`). `import_exports` связывает сайты импорта с файлами;
spelling-карты и module-алиасы — состояние загрузчика. Владение: registry — у `ModuleLoader`,
загрузчик — у `Pipeline`. Реализация — в internal-подкаталогах `path/`, `package/`, `interface/`, `registry/` (MEMORY в каждом).

## Facts and invariants

- **⚠ Модуль объявления:** `stampModuleLevelNode` ставит `AstNodeBase::module` ТОЛЬКО объявлениям уровня
  модуля. Модуль — группировка, а не namespace: импорт вставляет объявления в ТЕКУЩИЙ скоуп, области
  `::` явные; повторная регистрация того же узла идемпотентна.
- **⚠ `syntax`/`ast` НЕ зависят от `module_loader`** — только `session/module_resolver.hpp`
  (`no_module_loader_guard`).
- **⚠ Видимость:** top-level — exported; `__::` — private (module-qualified ключ, anonymous namespace);
  `_::` — protected (та же группа/пакет). `installFileInterface` исключает объявления вне блока из
  экспорт-набора: не-exported имена становятся private; определение record, завершающее forward из
  интерфейса, помечается `AstNodeBase::isExportImplementation` (видимость/ключи НЕ меняются — тот же
  TypeId) и клиенту не публикуется (виден forward). `_::` фильтруется импортом и публикуется
  заголовком (`canAccessProtected`).
- **⚠ Реэкспорт = импорт в блоке интерфейса:** маска-фильтр набора владельца добавляется в
  экспорт-набор; владелец (`node->module`) не меняется; реэкспорт ТРАНЗИТИВЕН (фасад публикует имена
  другого фасада).
- **⚠ Загрузка/импорт:** файл парсится один раз; duplicate-import — Error только при том же файле +
  scope + масках; ссылка `\path::name` НЕ загружает (`findLoaded`).
- **⚠ Ссылка/загруженность — ТОЛЬКО spelling-домен:** `lookupSpelling` (module-алиас активного модуля →
  карта импортов его scope; вне загрузки — однозначный поиск по всем scope); путь ФС — лишь
  cycle-детект. Spelling scope-относителен: один spelling в разных корнях — разные файлы; scope root
  фиксирует ПЕРВАЯ загрузка; «первого победителя» нет.
- **⚠ Пакетная идентичность — из логического имени** (`packageNameOf`); пути внутри пакета сохраняют
  пакетный заголовок. Пакет один (`trust.toml`, `name` = идентичность).
- **⚠ Module-алиас — область ВЛАДЕЛЬЦА:** виден только модулю объявления, не экспортируется;
  одинаковый spelling у разных владельцев допустим, но не совпадает со spelling импорта СВОЕГО модуля —
  проверяется в ОБЕ стороны.
- **⚠ Маски — `utils::ImportMasks`:** единственный парсер/матчер (`normalizedKey` — единственная
  нормализация). РЕШЕНИЕ «видно ли имя с сайта и под каким ключом» принимает не загрузчик и не
  `ImportMasks`, а `types/names.hpp`; проекция сайта (`siteProjection` → `ast::SiteProjection`) — это
  ДАННЫЕ сайта, а не второе правило. Несовпавшие — `-Wunmatched-mask`, пустой результат — `-Wempty-import` (namespace-алиас
  считается совпавшим). Непопадание считается по ОБЪЕДИНЁННОМУ набору сайта (`retainMasksPresentInBoth`);
  protected — только по ДОСТУПНЫМ частям. Цикл — `checkImportCycle`.
- **Префиксы — тип ПЕРВОГО компонента:** `\` файл, `\\` группа, `\\\` пакет; `\\dir` — нерекурсивно
  `.src`; `\\\pkg()` — first-level модули + корневые `.src`; `dir.src` + `dir/` — ошибка; build-каталог —
  не корень поиска.
- **⚠ Идентичность — логический путь:** заголовок либо сохранённый путь от scope root для программы без
  заголовка; ФС повторно не читается. Заголовок `\path;` — статический контракт, без него импорт —
  ошибка.
- **Раздельная компиляция:** `.cppt` — при out-of-line определениях ЛИБО при record, скрытом forward'ом;
  имя `<stem>-<md5(путь)[0..12]>.cppt`; `@[include]`/`@[link]` уходят в TU импортёра;
  `ModuleInterfaceFacts` считается один раз.
- **⚠ Относительные якоря:** `\$$` — self-файл; `\\$$`/`\\\$$` — каталог/пакет (пусто для корня);
  standalone-импорт запрещён.

## Relations

- `syntax` (Parser/import), `session` (`ModuleResolver`), `pipeline` (владелец, build.conf,
  `--emit-module-deps`), `package` (tar, `verifyPackage`), `transpiler` (факты из `part.projection->exports`).
