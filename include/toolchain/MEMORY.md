# MEMORY.md

> scope: include/toolchain (public API)
> role: persistent-memory
> last_reviewed: 2026-11-06
> review_period: 30
> max_size: 6000

## Architecture

Слой C++-сборки (не оркестратор):
- `toolchain/build/build_request.hpp` — `BuildRequest` (пути юнитов, `RuntimeHeaders`, link-libs, entry) +
  `RuntimeHeaders`/`BuildInputs`;
- `toolchain/build.hpp` — `buildEntryMainSource`, `prepareBuildInputs`, `composeBuildConf`,
  `writeBuildFiles`, `compileAndLink`;
- `toolchain/hash.hpp` — `fileHash` (MD5 файла);
- `toolchain/cache.hpp` — `codegenArgsRecord` + запись кеша `--run` (`composeRunCacheRecord`,
  `runCacheValid`, секция `.debug_trust_hash`);
- `toolchain/runtime/runtime_locator.hpp` — поиск `trust-runtime` и toolchain-библиотек;
- `toolchain/build/link_plan.hpp` — `LinkPlan`/`resolveLinkPlan` по `[[artifact]]`/`[[link.headers]]`.
Зоны: `build/` (Makefile/compile), `cache/` (hash + запись кеша; заголовков нет — только реализация),
`runtime/` (поиск библиотек).

## Facts and invariants

- **⚠ Входы сборки вычисляются ОДИН раз** (`prepareBuildInputs` → `BuildInputs`);
  `composeBuildConf` — чистая функция; `writeBuildFiles`/`compileAndLink` принимают `BuildRequest`.
- **⚠ Кеш `--run` считается по записи, а не по исходникам:** версия + `@opts` + `@deps` (замыкание:
  архив `stdlib`, provider-пакеты, явный `--dsl`) + `файл\tmd5`; пути ОТНОСИТЕЛЬНЫЕ CWD, поэтому
  бинарник из другого каталога кеш не получает (главный файл обязан совпасть).
- **⚠ ccache-обёртка генерируемой C++ (`runMake`):** при наличии `ccache` в PATH make получает
  `CXX='ccache <compiler>'` (кавычки обязательны — значение one word), иначе повторные сборки одного
  `.cppt` перекомпилировались бы заново. Отключить — `TRUST_TOOLCHAIN_CCACHE=0`. Хиты возможны
  только потому, что шапка `.cppt` детерминирована (дата, см. `sourcemap`).
- **⚠ Link plan demand-driven:** извлечение пакета само link-опций не добавляет; правила и ошибки —
  контракт `package`; `kind=toolchain` ищется `locateToolchainLibrary`, `system` → `-l<name>`.
- **Зависимости компонента:** `package`, `driver` (`PipelineOpts`), `utils`; НЕ `semantic`/
  `transpiler`/`ir`/`pipeline` — иначе LLVM-бэкенд тянул бы C++-компилятор.

## Decisions

- `PipelineOpts` берётся из `driver` (нейтральная модель опций), а не из оркестратора: `BuildRequest`
  описывает юнит, параметры сборки остаются в одной модели.

## Relations

- `package` (артефакты/link-контракт), `driver` (опции), `utils` (MD5/io/elf), `location`;
  вызывающий — `pipeline` (`commands.cpp`/`run.cpp`), C++-стадия. MIR/HIR не читает.
