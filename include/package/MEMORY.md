# MEMORY.md

> scope: include/package
> role: persistent-memory
> last_reviewed: 2026-11-06
> review_period: 30
> max_size: 4500

## Architecture

`.trust` — gzip-tar архив исходников с манифестом `trust.toml` в корне; единый способ доставки кода и
заголовков (stdlib, provider-пакеты, пользовательские пакеты): модель и парсинг манифеста, целостность,
упаковка/распаковка. Общие tar-примитивы — внутренний `package/archive/archive_io.hpp`.

Детали реализации — internal-подкаталог `package/archive/` (gzip-tar I/O); `manifest.hpp` и
`package.hpp` — публичный API корня.

## Facts and invariants

- **Пакет всегда ОДИН:** архив не содержит импортируемых вложенных пакетов (`.trust` внутри — обычный
  файл). `name` из `trust.toml` — идентичность, имя архива совпадает, граф пакетов плоский.
- **Версия — semver ПАКЕТА, не компилятора:** `trust --package` сохраняет авторскую `version`, при её
  отсутствии подставляет версию компилятора как provenance. Версия toolchain — `[build].toolchain`.
- **`module-root` (по умолчанию `.`)** задаёт scope root пакетных файлов/заголовков: путь относительный,
  без `..`; `name` манифеста обязан совпадать с запрошенным именем пакета.
- **⚠ Целостность обязательна:** `[files]` — MD5 каждого файла, кроме `trust.toml`. Нет манифеста или
  таблицы, несовпадение хеша, лишний/отсутствующий файл — ошибка `verifyPackage`, а НЕ пропуск проверки.
- **⚠ `[[artifact]]` — контракт линковки:** `kind` = `source` | `package` | `toolchain` | `system`;
  `requires` — потребность САМОГО артефакта (транзитивно при его линковке), ссылка на собственный
  `provides` — ошибка. `[[link.headers]]` — заголовок → логические библиотеки; заголовки провайдера
  кладутся только в `include/` пакета.
- **⚠ Линковка demand-driven:** извлечение пакета само link-опций не добавляет — только `@[link]` на
  использованной декларации или `[[link.headers]]` использованного заголовка. Коллизия `provides`,
  неизвестный `kind`/capability, отсутствующий файл — ошибка резолвера.
- **Раскладку описывает манифест** (`[[layout.layers]]`): система сборки раскладку не знает и лишь
  вызывает `--package <каталог>`. В архив кладётся копия `trust.toml` с `[build]`/`[files]`.
- **Пакет ресурсов toolchain** — `<share/trust>/<name>.trust` относительно exe (`TRUST_RESOURCE_DIR`);
  кэш извлечения — `<packageCacheDir>/<name>-<md5(archive)[0..12]>/` (маркер `.trust-archive-md5`).
- **⚠ Кэш распаковки stdlib (≠ packageCacheDir):** ensureStdlibCacheEntry — XDG_CACHE_HOME/trust-lang
  /stdlib/<md5>, атомарный rename, LRU 30 сут; сборка (копия в build-каталог) и prelude-чтения;
  пользовательские пакеты НЕ кэшируются.
- **⚠ Пакеты воспроизводимы:** createTarGz — `--sort=name --mtime=@0`, [build].created = дата
  (не время); иначе md5 архива при репаке меняется и ломает ccache (dep-hash stdlib в .cppt).

## Decisions

- Вынесено из `utils` и `pipeline` в отдельный компонент: нужно И pipeline, И module_loader.

## Relations

- ← `utils`; → `pipeline` (режимы `--package`/`--extract`, include-пути, link-plan), `module_loader`
  (исходники из пакета), `stdlib/core` (сборка `stdlib.trust`).
