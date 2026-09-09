# Assets - единый каталог встраиваемых артефактов

## Назначение

Единое описание и провайдер ВСЕХ встраиваемых артефактов компилятора: исходников/заголовков
стандартной библиотеки (`stdlib/…`, вшиты в компилятор) и заголовков встроенных типов
(`trust/…`, ELF-секции trust-runtime).

## Состав

- **`asset_catalog.hpp`** - единый X-macro-каталог `TRUST_ASSET_LIST`: генерирует `AssetId`,
  `assetName`, `AssetSource` (`kCompilerEmbedded`/`kRuntimeElf`), `findAsset`, `isStdlibAsset`.
- **`asset_provider.hpp`/`asset_provider.cpp`** - содержимое compiler-embedded ассетов через `#embed`
  (`embeddedAssetContent`); для runtime-ассетов — FAULT (их хранит trust-runtime).

## Принципы

- Префиксы НЕ смешиваются: `stdlib/…` — компилятор, `trust/…` — ELF-секции trust-runtime.
- Единственный источник списка ассетов — X-macro-каталог (нет «двух таблиц»); имя/id/источник
  описываются один раз; компайлтайм-проверки (`static_assert`) покрывают непустые имена.
- Извлечение — только при использовании; неизвестное имя — ошибка без неявного fallback.
- Диспетчеризация по источнику — в `pipeline/runtime_locator.cpp`.
