# MEMORY.md

> scope: include/assets
> role: persistent-memory
> last_reviewed: 2026-09-16
> review_period: 30
> max_size: 6000

## Architecture

Единый каталог встраиваемых артефактов компилятора `asset_catalog.hpp` (X-macro `TRUST_ASSET_LIST`
→ `AssetId`, `assetName`, `AssetSource`, `findAsset`, `isStdlibAsset`) + провайдер содержимого
встроенных ассетов `asset_provider.hpp/.cpp` (`#embed`). Диспетчеризация по ИСТОЧНИКУ: ассеты
стандартной библиотеки вшиты в компилятор (`AssetSource::kCompilerEmbedded`), заголовки встроенных
типов — ELF-секции trust-runtime (`AssetSource::kRuntimeElf`, извлекает `pipeline/runtime_locator.cpp`).

## Facts and invariants

- **Префиксы НЕ смешиваются:** `stdlib/…` ⟺ `kCompilerEmbedded`; `trust/…` ⟺ `kRuntimeElf`.
  Инвариант закреплён `AssetSource` в каталоге и компайлтайм-проверками.
- **ЕДИНЫЙ источник списка ассетов** — X-macro `TRUST_ASSET_LIST`: имя, id и источник описываются
  один раз; опечатка в имени — ошибка компиляции; у каждого id непустое публикуемое имя
  (`static_assert`).
- `embeddedAssetContent(id)` допустим ТОЛЬКО для `kCompilerEmbedded`; для `kRuntimeElf` — FAULT
  (содержимое в компиляторе отсутствует). Пустое содержимое встроенного ассета — FAULT.
- Извлечение заголовка — ТОЛЬКО при использовании; неизвестное имя — ошибка (без неявного поиска
  в рантайме).
- Содержимое рантайм-заголовков лежит в ELF-секциях trust-runtime, имя секции == путь заголовка
  (`@trust/<h>` → `trust/<h>`); см. `include/runtime/MEMORY.md`.

## Decisions

- Каталог и провайдер ассетов вынесены из `stdlib` в отдельный компонент `assets_lib`: он покрывает
  ОБА слоя (stdlib + встроенные типы), поэтому снимает дублирование «двух таблиц» и делает
  диспетчеризацию по источнику явной. `stdlib_lib` стал header-only (INTERFACE) и линкует `assets_lib`.

## Relations

- `assets_lib` ← `stdlib_lib`, `pipeline_lib` (`runtime_locator` диспетчеризует по каталогу), `lsp_lib`.
- Критерии отнесения типа к встроенным — `include/runtime/MEMORY.md`; к stdlib — `include/stdlib/MEMORY.md`.
