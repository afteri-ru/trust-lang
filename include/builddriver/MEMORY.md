# MEMORY.md

> scope: include/builddriver
> role: persistent-memory
> last_reviewed: 2026-11-12
> review_period: 30

## Architecture

Драйвер сборки/запуска: `compileProgram(Pipeline&, const PipelineOpts&, MapperFile)` — оркестрация
поверх артефактов `pipeline`. Слой НИЖЕ `pipeline`/`toolchain` (линкует оба), тем самым `pipeline`
не зависит от `toolchain` (E20/R34) и графа-цикла нет.

## Facts and invariants

- **⚠ Входы сборки вычисляются ОДИН раз:** `Pipeline::runCppToFile` + `generateModuleOutputs` дают
  `.cppt`/`.hpp`/`cpptPaths`; `toolchain::writeBuildFiles`/`compileAndLink` принимают `BuildRequest`
  (см. `toolchain/MEMORY.md`).
- **⚠ Кеш `--run` валидируется по ELF-секции** (`.debug_trust_hash`) через `toolchain::runCacheValid`;
  формат записи — `utils/build_record.hpp` (владелец формата), строку опций даёт `driver::codegenArgsRecord`.
- **⚠ Entry-имя — единственная формула** `moduleEntryIdent(Pipeline::moduleLogicalName(mainIdx))`.

## Decisions

- Оркестрация сборки/запуска живёт в этом нижестоящем компоненте (не в `pipeline`): размещение в `driver`
  дало бы цикл `driver → pipeline → driver` (R34).

## Relations

- `pipeline` (артефакты), `toolchain` (build/link/run cache), `driver` (опции), `package`, `session`,
  `location`, `utils`. Точка входа CLI — `src/trust.cpp`.
