# MEMORY.md

> scope: include/toolchain (public API)
> role: persistent-memory
> last_reviewed: 2026-11-10
> review_period: 30

## Architecture

Слой C++-сборки (не оркестратор):
- `toolchain/build/build_request.hpp` — `BuildRequest` (пути юнитов, `RuntimeHeaders`, link-libs, entry) +
  `RuntimeHeaders`/`BuildInputs`;
- `toolchain/build.hpp` — `buildEntryMainSource`, `prepareBuildInputs`, `composeBuildConf`,
  `writeBuildFiles`, `compileAndLink`;
- `toolchain/cache.hpp` — `runCacheValid` (чтение записи кеша `--run`; формат — `utils/build_record.hpp`);
- `toolchain/runtime/runtime_locator.hpp` — поиск `trust-runtime` и toolchain-библиотек;
- `toolchain/build/link_plan.hpp` — `LinkPlan`/`resolveLinkPlan` по `[[artifact]]`/`[[link.headers]]`.
Зоны: `build/` (Makefile/compile), `cache/` (чтение кеша; заголовков нет — только реализация; MD5 файла —
`utils::md5HexFile`), `runtime/` (поиск библиотек).

## Facts and invariants

- **⚠ Входы сборки вычисляются ОДИН раз** (`prepareBuildInputs` → `BuildInputs`);
  `composeBuildConf` — чистая функция; `writeBuildFiles`/`compileAndLink` принимают `BuildRequest`.
- **⚠ Кеш `--run` считается по записи, а не по исходникам:** версия + `@opts` + `@deps` (замыкание:
  архив `stdlib`, provider-пакеты, явный `--dsl`) + `файл\tmd5`; пути ОТНОСИТЕЛЬНЫЕ CWD, поэтому
  бинарник из другого каталога кеш не получает (главный файл обязан совпасть).
  Формат записи — `utils/build_record.hpp` (владелец), строка опций — `driver/codegenArgsRecord`;
  `toolchain` только читает (`runCacheValid`), пишет кодоген в шапке `.cppt`.
- **⚠ ccache-обёртка генерируемой C++ (`runMake`):** при наличии `ccache` в PATH make получает
  `CXX='ccache <compiler>'` (кавычки обязательны — значение one word), иначе повторные сборки одного
  `.cppt` перекомпилировались бы заново. Отключить — `TRUST_TOOLCHAIN_CCACHE=0`. Хиты возможны
  только потому, что шапка `.cppt` детерминирована (дата, см. `sourcemap`).
- **⚠ Link plan demand-driven:** извлечение пакета само link-опций не добавляет; правила и ошибки —
  контракт `package`; `kind=toolchain` ищется `locateToolchainLibrary`, `system` → `-l<name>`.
- **Зависимости компонента:** `package`, `driver` (`PipelineOpts`), `utils`; НЕ `semantic`/
  `transpiler`/`ir`/`pipeline` — иначе LLVM-бэкенд тянул бы C++-компилятор.
- **⚠ Стандарт целевого C++ — из контракта, не из шаблона:** `BuildRequest::cppStandard` (приходит из
  `TargetContract::targetInfo()` — владельца целевого контракта) эмитится `composeBuildConf` в `build.conf`
  (`CXXFLAGS += -std=`); шаблон Makefile стандарт НЕ задаёт (единственный источник значения —
  `target::kDefaultCppStandard`).

## Decisions

- `PipelineOpts` берётся из `driver` (нейтральная модель опций), а не из оркестратора: `BuildRequest`
  описывает юнит, параметры сборки остаются в одной модели.

## Relations

- `package` (артефакты/link-контракт), `driver` (опции), `utils` (MD5/io/elf), `location`;
  вызывающий — `pipeline` (`commands/commands.cpp`/`run/run.cpp`), C++-стадия. MIR/HIR не читает.
