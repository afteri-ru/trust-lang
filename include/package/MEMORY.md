# MEMORY.md

> scope: include/package
> role: persistent-memory
> last_reviewed: 2026-11-13
> review_period: 30

## Architecture

`.trust` — gzip-tar архив исходников с манифестом `trust.toml` в корне; единый способ доставки кода и
заголовков (stdlib, provider-пакеты, пользовательские). Модель/парсинг манифеста, целостность, упаковка.
Общие tar-примитивы — внутренний `package/archive/archive_io.hpp`; `manifest.hpp` и `package.hpp` — публичный
API корня. Зоны реализации: `archive/` (tar-примитивы, создание/распаковка, целостность, пакеты ресурсов)
и `manifest/` (разбор/валидация/запись `trust.toml`).

## Facts and invariants

- **Пакет всегда ОДИН:** `.trust` внутри — обычный файл, импортируемых вложенных пакетов нет. `name` —
  идентичность, имя архива совпадает, граф плоский.
- **Версия — semver ПАКЕТА, не компилятора:** `trust --package` сохраняет авторскую `version`, при отсутствии —
  версию компилятора как provenance. Версия toolchain — `[build].toolchain`.
- **⚠ `[build].runtime-abi` — версия ABI-контракта рантайма** (E14/R48), проставляется упаковщиком из
  `trust::kRuntimeAbiVersion`; резолвер пакета отвергает пакет с чужой версией (несовместимый рантайм —
  ошибка, а не UB).
- **`module-root` (default `.`)** — scope root пакетных файлов; путь относительный, без `..`; `name` обязан
  совпадать с запрошенным.
- **⚠ Целостность обязательна:** `[files]` — MD5 каждого файла, кроме `trust.toml`. Нет манифеста/таблицы,
  несовпадение хеша, лишний/отсутствующий файл — ошибка `verifyPackage`, не пропуск проверки.
- **⚠ `[[artifact]]` — контракт линковки:** `kind` = source|package|toolchain|system; `requires` —
  потребность артефакта (транзитивно), ссылка на свой `provides` — ошибка; `[[link.headers]]` — заголовок →
  логические библиотеки.
- **⚠ Линковка demand-driven:** извлечение пакета link-опций не добавляет — только `@[link]` или
  `[[link.headers]]`. Коллизия `provides`, неизвестный kind/capability, отсутствующий файл — ошибка.
- **Раскладку описывает манифест** (`[[layout.layers]]`); система сборки её не знает и лишь вызывает
  `--package <каталог>`. В архив кладётся копия `trust.toml` с `[build]`/`[files]`.
- **Пакет ресурсов toolchain** — `<share/trust>/<name>.trust` относительно exe (`TRUST_RESOURCE_DIR`); кэш
  извлечения — `<packageCacheDir>/<name>-<md5(archive)[0..12]>/`.
- **⚠ Кэш распаковки stdlib (≠ packageCacheDir):** XDG_CACHE_HOME/trust-lang/stdlib/<md5>, атомарный rename,
  LRU 30 сут; пользовательские пакеты НЕ кэшируются.
- **⚠ Пакеты воспроизводимы:** createTarGz — `--sort=name --mtime=@0`, [build].created — дата (не время);
  иначе md5 архива при репаке меняется и ломает ccache (dep-hash stdlib в `.cppt`).

## Decisions

- Вынесено из `utils`/`pipeline`: нужно И pipeline, И module_loader.

## Relations

- ← `utils`; → `pipeline` (`--package`/`--extract`, include-пути, link-plan), `module_loader` (исходники из
  пакета), `stdlib/core` (сборка `stdlib.trust`).
